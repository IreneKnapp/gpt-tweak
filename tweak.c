#include "tweak.h"

#define true 1
#define false 0
typedef uint8_t bool;
typedef uint8_t block[512];
typedef uint8_t uuid[16];
typedef uint64_t lba;

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
    lba my_lba;
    lba alternate_lba;
    lba first_usable_lba;
    lba last_usable_lba;
    uuid disk_uuid;
    lba partition_entry_lba;
    uint32_t number_of_partition_entries;
    uint32_t size_of_partition_entry; // must be a multiple of 8
    uint32_t partition_entry_array_crc32;
    // computed over number_of_partition_entries * size_of_partition_entry bytes
    // Remaining bytes must all be zero.
};

void tweak(int fd);
void read_block(int fd, lba lba, block *data);
void hexdump_block(block *data);
bool validate_gpt_header(block *header);
uint64_t total_entry_size(struct gpt_header *header);



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

    printf("Okay, my verbosity is -%c%c%c%i.  Just so you know.\n",
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
    block legacy_mbr;
    read_block(fd, 0, &legacy_mbr);
    printf("Legacy MBR:\n");
    hexdump_block(&legacy_mbr);
    */

    printf("\nLoading the header.\n");
    
    block header_block;
    read_block(fd, 1, &header_block);
    if(!validate_gpt_header(&header_block)) {
	describe_failure("The header doesn't validate.\n");
	return;
    } else describe_success("The header validates.\n");

    struct gpt_header *header = (struct gpt_header *) &header_block;

    printf("\nLoading the entry array.\n");

    uint64_t size = total_entry_size(header);
    lba first_entry_block = header->partition_entry_lba;
    lba last_full_entry_block = header->partition_entry_lba + size / sizeof(block) - 1;
    lba n_entry_blocks = size / sizeof(block);
    if(size % sizeof(block) > 0) n_entry_blocks++;

    block *entry_blocks = malloc(n_entry_blocks * sizeof(block));
       
    lba lba, i;
    uint32_t array_crc = efi_crc32_start(NULL, 0);
    for(i = 0, lba = first_entry_block; lba <= last_full_entry_block; lba++, i++) {
	read_block(fd, lba, &(entry_blocks[i]));
	array_crc = efi_crc32_continue((uint8_t *) &(entry_blocks[i]),
				       sizeof(block), array_crc);
    }
    if(size % sizeof(block) != 0) {
	describe_trivium("There's a last odd block in the entry array, of size %Li.\n");
	read_block(fd, lba, &(entry_blocks[i]));
	array_crc = efi_crc32_continue((uint8_t *) &(entry_blocks[i]),
				       size % sizeof(block), array_crc);
    }
    array_crc = efi_crc32_end(array_crc);
    
    if(array_crc != header->partition_entry_array_crc32) {
	describe_failure("The entry array CRC32 doesn't validate.\n");
	return;
    } else describe_success("The entry array CRC32 validates.\n");
}


void read_block(int fd, lba lba, block *data) {
    ssize_t result = pread64(fd, data, sizeof(block), lba*sizeof(block));
    if(result == -1) {
	fprintf(stderr, "Unable to read LBA %Li: %s\n", lba, strerror(errno));
	exit(1);
    }
    if(result != sizeof(block)) {
	fprintf(stderr, "Inexplicably got wrong amount of bytes for LBA %Li\n", lba);
	exit(1);
    }
}


void hexdump_block(block *data) {
    int i;

    for(i = 0; i < sizeof(block); i++) {
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


bool validate_gpt_header(block *header_block) {
    current_detail++;
    
    struct gpt_header *header = (struct gpt_header *) header_block;
    bool result = true;
    
    if(strncmp(header->signature, "EFI PART", sizeof(header->signature))) {
	describe_failure("GPT header has wrong magic number.\n");
	result = false;
    } else describe_success("Magic number okay.\n");

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
    for(i = header->header_size; i < sizeof(block); i++)
	if((*header_block)[i] != 0x00) break;
    if(i != sizeof(block)) {
	describe_failure("GPT header followed by trailing garbage at offset %li.\n", i);
	result = false;
    } else describe_success("GPT header correctly followed by zeroes.\n");

    block header_copy;
    memcpy(header_copy, header_block, sizeof(block));
    ((struct gpt_header *) header_copy)->header_crc32 = 0x00000000;
    uint32_t old_header_crc32 = header->header_crc32;
    size_t size_to_checksum = header->header_size;
    if(size_to_checksum > sizeof(block)) size_to_checksum = sizeof(block);
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
    
    uint64_t size = total_entry_size(header);
    if(size % sizeof(block) == 0)
	describe_trivium("The entries occupy %li LBAs exactly.\n",
			 size / sizeof(block));
    else
	describe_trivium("The entries occupy %li LBAs completely, plus %li bytes more.\n",
			 size / sizeof(block), size % sizeof(block));
    
    describe_trivium("Header identifies its own CRC32 as 0x%08x and the entries' as 0x%08x.\n",
		     header->header_crc32,
		     header->partition_entry_array_crc32);

    current_detail--;
    return result;
}


uint64_t total_entry_size(struct gpt_header *header) {
    return header->number_of_partition_entries * header->size_of_partition_entry;
}
