#include "tweak.h"

#define LBA_SIZE 512

#define true 1
#define false 0
typedef uint8_t bool;
typedef uint8_t lba[LBA_SIZE];
typedef uint8_t uuid[16];

struct uuid {
    uint32_t time_low; // Little-endian.
    uint16_t time_mid; // Little-endian.
    uint16_t time_high_and_version; // Little-endian.
    uint8_t clock_seq_high_and_reserved;
    uint8_t clock_seq_low;
    uint8_t node[6];
};

struct gpt_header {
    char signature[8];
    uint32_t revision;
    uint32_t header_size; // must be >= 92
    uint32_t header_crc32;
    // assume this field zero then compute over the first header_size bytes
    uint32_t reserved; // must be zero
    uint64_t my_lba;
    uint64_t alternate_lba;
    uint64_t first_usable_lba;
    uint64_t last_usable_lba;
    uuid disk_uuid;
    uint64_t partition_entry_lba;
    uint32_t number_of_partition_entries;
    uint32_t size_of_partition_entry; // must be a multiple of 8
    uint32_t partition_entry_array_crc32;
    // computed over number_of_partition_entries * size_of_partition_entry bytes
    // Remaining bytes must all be zero.
};

void tweak(int fd);
void read_lba(int fd, uint64_t index, lba *data);
void hexdump_lba(lba *data);
bool validate_gpt_header(lba *header);
uint32_t crc32(uint32_t *data, size_t length);



int main(int argc, char **argv) {
    cutoff_detail = 5;
    describe_failures = 1;
    describe_successes = 1;
    describe_trivia = 1;

    char *devicename = NULL;

    bool usage_okay = true;
    int i;
    for(i = 1; i < argc; i++) {
	if(argv[i][0] == '-') {
	    int j;
	    for(j = 1; argv[i][j]; j++) {
		char c = argv[i][j];
		if(isdigit(c)) {
		    cutoff_detail = c - '0';
		} else switch(argv[i][j]) {
		case 'F': describe_failures = true; break;
		case 'f': describe_failures = false; break;
		case 'S': describe_successes = true; break;
		case 's': describe_successes = false; break;
		case 'T': describe_trivia = true; break;
		case 't': describe_trivia = false; break;
		default: usage_okay = false; break;
		}
	    }
	} else if(!devicename) {
	    devicename = argv[i];
	} else {
	    usage_okay = false;
	}
    }
    if(!devicename) usage_okay = false;
    if(!usage_okay) {
	fprintf(stderr, "Usage: gpt-tweak [-fstFST0123456789] device|file\n");
	return 1;
    }

    printf("Okay, my verbosity is %c%c%c%i.  Just so you know.\n",
	   describe_failures ? 'F' : 'f',
	   describe_successes ? 'S' : 's',
	   describe_trivia ? 'T' : 't',
	   cutoff_detail);
    current_detail = 1;
    
    int fd = open64(devicename, O_RDONLY);
    if(fd == -1) {
	fprintf(stderr, "Unable to open %s: %s\n", devicename, strerror(errno));
	return 1;
    }

    tweak(fd);

    close(fd);

    return 0;
}


void tweak(int fd) {
    /*
    lba legacy_mbr;
    read_lba(fd, 0, &legacy_mbr);
    printf("Legacy MBR:\n");
    hexdump_lba(&legacy_mbr);
    */

    lba header;
    read_lba(fd, 1, &header);
    if(!validate_gpt_header(&header)) {
	describe_failure("The header doesn't validate.\n");
	return;
    } else describe_success("The header validates.\n");
}


void read_lba(int fd, uint64_t index, lba *data) {
    ssize_t result = pread64(fd, data, sizeof(lba), index*sizeof(lba));
    if(result == -1) {
	fprintf(stderr, "Unable to read LBA %Li: %s\n", index, strerror(errno));
	exit(1);
    }
    if(result != sizeof(lba)) {
	fprintf(stderr, "Inexplicably got wrong amount of bytes for LBA %Li\n", index);
	exit(1);
    }
}


void hexdump_lba(lba *data) {
    int i;

    for(i = 0; i < LBA_SIZE; i++) {
	if(i % 16 == 0) printf("%04X:  ", i);
	else if(i % 16 == 8) printf("  ");
	else if(i % 2 == 0) printf(" ");
	
	printf("%02x", (*data)[i]);
	
	if(i % 16 == 15) {
	    int j;

	    printf("    ");
	    for(j = i - 15; j <= i; j++) {
		int c = (*data)[j];
		if(isprint(c))
		    printf("%c", c);
		else
		    printf(".");
	    }
	    printf("\n");
	}
    }
}
/*
XXXX:  aaaa aaaa aaaa aaaa  aaaa aaaa aaaa aaaa    ................
         1         2         3         4         5         6         7         8
12345678901234567890123456789012345678901234567890123456789012345678901234567890
*/


bool validate_gpt_header(lba *gpt_header_lba) {
    current_detail++;
    
    struct gpt_header *header = (struct gpt_header *) gpt_header_lba;
    bool result = true;
    
    if(strncmp(header->signature, "EFI PART", sizeof(header->signature))) {
	describe_failure("GPT header has wrong magic number.\n");
	result = false;
    } else describe_success("Magic number OK.\n");

    if(header->revision != 0x00010000) {
	describe_failure("GPT header claims wrong header-format revision.\n");
	result = false;
    } else describe_success("Header-format revision okay.\n");

    if(header->reserved != 0x00000000) {
	describe_failure("GPT header reserved field is nonzero.\n");
	result = false;
    } else describe_success("Header reserved field okay.\n");

    if(header->my_lba != 1) {
	describe_failure("GPT header mis-identifies its own LBA.\n");
	result = false;
    } else describe_success("Header self-LBA okay.\n");

    size_t i;
    for(i = header->header_size; i < LBA_SIZE; i++)
	if((*gpt_header_lba)[i] != 0x00) break;
    if(i != LBA_SIZE) {
	describe_failure("GPT header followed by trailing garbage at offset %li.\n", i);
	result = false;
    } else describe_success("GPT header correctly followed by zeroes.\n");

    lba header_copy;
    memcpy(header_copy, gpt_header_lba, sizeof(lba));
    ((struct gpt_header *) header_copy)->header_crc32 = 0x00000000;
    uint32_t old_header_crc32 = header->header_crc32;
    size_t size_to_checksum = header->header_size;
    if(size_to_checksum > LBA_SIZE) size_to_checksum = LBA_SIZE;
    uint32_t new_header_crc32 = efi_crc32((uint8_t *) &header_copy, size_to_checksum);

    if(old_header_crc32 != new_header_crc32) {
	describe_failure("Header's self-checksum is invalid.\n");
	result = false;
    } else describe_success("Header's self-checksum is valid.\n");

    if(header->header_size == 92)
	describe_trivium("The header size is %li bytes, which is the original standard.\n",
			 header->header_size);
    else
	describe_trivium("The header size is %li bytes, which is NOT the original standard.\n");
    describe_trivium("Alternate LBA %Li, usable LBAs from %Li to %Li inclusive.\n",
		     header->alternate_lba,
		     header->first_usable_lba,
		     header->last_usable_lba);
    describe_trivium("Partition entries start at LBA %Li; "
		     "there are %li entries of %li bytes each.\n",
		     header->partition_entry_lba,
		     header->number_of_partition_entries,
		     header->size_of_partition_entry);
    size_t total_entry_size
	= header->number_of_partition_entries * header->size_of_partition_entry;
    if(total_entry_size % LBA_SIZE == 0)
	describe_trivium("The entries occupy %li LBAs exactly.\n",
			 total_entry_size / LBA_SIZE);
    else
	describe_trivium("The entries don't end on an LBA boundary, occupying %f LBAs.\n",
			 ((float) total_entry_size) / ((float) LBA_SIZE));
    describe_trivium("Header identifies its own CRC32 as 0x%08x and the entries' as 0x%08x.\n",
		     header->header_crc32,
		     header->partition_entry_array_crc32);

    current_detail--;
    return result;
}
