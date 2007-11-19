#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>


extern uint32_t efi_crc32(uint8_t *buf, size_t len);

typedef uint8_t lba[512];
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

int fd;

void read_lba(uint64_t index, lba *data);
void hexdump_lba(lba *data);
void validate_gpt_header(struct gpt_header *header);
uint32_t crc32(uint32_t *data, size_t length);


int main(int argc, char **argv) {
    if(argc != 2) {
	fprintf(stderr, "Usage: gpt-tweak device|file\n");
	return 1;
    }
    
    char *devicename = argv[1];
    fd = open64(devicename, O_RDONLY);
    if(fd == -1) {
	fprintf(stderr, "Unable to open %s: %s\n", devicename, strerror(errno));
	return 1;
    }

    /*
    lba legacy_mbr;
    read_lba(0, &legacy_mbr);
    printf("Legacy MBR:\n");
    hexdump_lba(&legacy_mbr);
    */

    lba header;
    read_lba(1, &header);
    //printf("Header:\n");
    //hexdump_lba(&header);
    validate_gpt_header((struct gpt_header *) &header);
    
    return 0;
}


void read_lba(uint64_t index, lba *data) {
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

    for(i = 0; i < 512; i++) {
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


void validate_gpt_header(struct gpt_header *header) {
    if(strncmp(header->signature, "EFI PART", sizeof(header->signature))) {
	fprintf(stderr, "GPT header has wrong magic number.\n");
	exit(1);
    } else printf("Magic number OK.\n");

    if(header->revision != 0x00010000) {
	fprintf(stderr, "GPT header claims wrong header-format revision.\n");
	exit(1);
    } else printf("Header-format revision okay.\n");

    printf("The header size is %li bytes.\n", header->header_size);

    if(header->reserved != 0x00000000) {
	fprintf(stderr, "GPT header reserved field is nonzero.\n");
	exit(1);
    } else printf("Header reserved field okay.\n");

    if(header->my_lba != 1) {
	fprintf(stderr, "GPT header mis-identifies its own LBA.\n");
	exit(1);
    } else printf("Header self-LBA okay.\n");

    printf("Alternate LBA %Li, usable LBAs from %Li to %Li inclusive.\n",
	   header->alternate_lba,
	   header->first_usable_lba,
	   header->last_usable_lba);
    printf("Partition entries start at LBA %Li; there are %li entries of %li bytes each.\n",
	   header->partition_entry_lba,
	   header->number_of_partition_entries,
	   header->size_of_partition_entry);

    printf("Header identifies its own CRC32 as 0x%08x and the entries' as 0x%08x.\n",
	   header->header_crc32,
	   header->partition_entry_array_crc32);

    uint32_t old_header_crc32 = header->header_crc32;
    header->header_crc32 = 0x00000000;
    uint32_t new_header_crc32 = efi_crc32((uint8_t *) header, header->header_size/4);

    printf("\nShould be 0x%08x; is 0x%08x.\n",
	   old_header_crc32, new_header_crc32);
    if(old_header_crc32 == new_header_crc32) {
	int i;
	for(i = 0; i < 10; i++)
	    printf("*** YES!  YES!  YES!  You fixed the CRC32!\n");
    } else printf("*** Sorry.  Hang in there.  It's something simple.\n");
    
    //printf("Yup, the header validates!  Well, that's something.\n");
}
