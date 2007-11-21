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

struct partition_entry {
    uuid partition_type_uuid;
    uuid unique_partition_uuid;
    lba starting_lba;
    lba ending_lba;
    uint64_t attributes;
    uint8_t partition_name[72];
};

struct predefined_type_uuid {
    uuid uuid;
    char name[32];
};

// The bytes in this list are given in "presentation order" as defined by EFI; this means
// that, with respect to the on-disk representation, they are swabbed.  It also means that
// if you're adding another entry, you can pretty much just transcribe it directly from the
// explanatory document that tells you what the entry is, and not worry about byte order...
// If it's written hyphenated as in C12A7328-F81F-11D2-BA4B-00A0C93EC93B, you can be pretty
// sure it's in the aforementioned order.  If you hyper-correct by swabbing when you
// transcribe, it will be wrong and you will end up sad.
struct predefined_type_uuid predefined_type_uuids[] = {
    { { 0xC1, 0x2A, 0x73, 0x28, 0xF8, 0x1F, 0x11, 0xd2,
	0xBA, 0x4B, 0x00, 0xA0, 0xC9, 0x3E, 0xC9, 0x3B },
      "EFI system partition" },
    { { 0x48, 0x46, 0x53, 0x00, 0x00, 0x00, 0x11, 0xAA,
	0xAA, 0x11, 0x00, 0x30, 0x65, 0x43, 0xEC, 0xAC },
      "Apple HFS+ (or just HFS)" },
    { { 0x55, 0x46, 0x53, 0x00, 0x00, 0x00, 0x11, 0xAA,
	0xAA, 0x11, 0x00, 0x30, 0x65, 0x43, 0xEC, 0xAC },
      "Apple UFS" },
    { { 0x42, 0x6F, 0x6F, 0x74, 0x00, 0x00, 0x11, 0xAA,
	0xAA, 0x11, 0x00, 0x30, 0x65, 0x43, 0xEC, 0xAC },
      "Apple Boot (meant to hold legacy drivers)" },
    { { 0x52, 0x41, 0x49, 0x44, 0x00, 0x00, 0x11, 0xAA,
	0xAA, 0x11, 0x00, 0x30, 0x65, 0x43, 0xEC, 0xAC },
      "Apple RAID" },
    { { 0x52, 0x41, 0x49, 0x44, 0x5F, 0x4F, 0x11, 0xAA,
	0xAA, 0x11, 0x00, 0x30, 0x65, 0x43, 0xEC, 0xAC },
      "Apple RAID, offline" },
    { { 0x4C, 0x61, 0x62, 0x65, 0x6C, 0x00, 0x11, 0xAA,
	0xAA, 0x11, 0x00, 0x30, 0x65, 0x43, 0xEC, 0xAC },
      "Apple Label (a wrapper format used by legacy bootloaders)" },
    { { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
      "Empty partition" }
};

void tweak(int fd);
void read_block(int fd, lba lba, block *data);
void hexdump_block(block *data);
char *uuid_to_ascii(uuid uuid);
bool validate_gpt_header(block *header);
block *load_entry_array(int fd, struct gpt_header *header);
bool validate_entry_array(struct gpt_header *header, block *entry_blocks);
uint32_t compute_header_crc32(block *header);
uint32_t compute_entry_crc32(struct gpt_header *header, block *entry_blocks);
uint64_t total_entry_size(struct gpt_header *header);
char *get_type_uuid_name(uuid uuid);
void swab_and_copy_uuid(uuid *target, uuid *source);
void swab_uuid(uuid *uuid);
void swab32(uint8_t *bytes);
void swab16(uint8_t *bytes);



int main(int argc, char **argv) {
    cutoff_detail = 9;
    describe_failures = true;
    describe_successes = true;
    describe_trivia = true;

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

    block *entry_blocks = load_entry_array(fd, header);

    if(!validate_entry_array(header, entry_blocks)) {
	describe_failure("The entry array doesn't validate.\n");
	return;
    } else describe_success("The entry array validates.\n");
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


char *uuid_to_ascii(uuid uuid) {
    static char result[37];
    int i, j;
    for(i = 0, j = 0; i < 16; i++, j+=2) {
	switch(i) {
	case 4:
	case 6:
	case 8:
	case 10:
	    result[j] = '-';
	    j++;
	}
	sprintf(result + j, "%02x", uuid[i]);
    }
    result[36] = '\0';
    return result;
}


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
	describe_failure("GPT header mis-identifies its own LBA, as %Li.\n", header->my_lba);
	result = false;
    } else describe_success("Header self-LBA okay.\n");

    size_t i;
    for(i = header->header_size; i < sizeof(block); i++)
	if((*header_block)[i] != 0x00) break;
    if(i != sizeof(block)) {
	describe_failure("GPT header followed by trailing garbage at offset %li.\n", i);
	result = false;
    } else describe_success("GPT header correctly followed by zeroes.\n");

    uint32_t old_header_crc32 = header->header_crc32;
    uint32_t new_header_crc32 = compute_header_crc32(header_block);

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


block *load_entry_array(int fd, struct gpt_header *header) {
    uint64_t size = total_entry_size(header);
    lba first_entry_block = header->partition_entry_lba;
    lba last_full_entry_block = header->partition_entry_lba + size / sizeof(block) - 1;
    lba n_entry_blocks = size / sizeof(block);
    if(size % sizeof(block) > 0) n_entry_blocks++;

    block *entry_blocks = malloc(n_entry_blocks * sizeof(block));
       
    lba entry_lba, i;
    for(i = 0, entry_lba = first_entry_block;
	entry_lba <= last_full_entry_block;
	entry_lba++, i++)
	{
	    read_block(fd, entry_lba, &(entry_blocks[i]));
	}
    if(size % sizeof(block) != 0) {
	describe_trivium("There's a last odd block in the entry array, of size %Li.\n");
	read_block(fd, entry_lba, &(entry_blocks[i]));
    }

    return entry_blocks;
}


bool validate_entry_array(struct gpt_header *header, block *entry_blocks) {
    current_detail++;
    
    bool result = true;
    
    if(compute_entry_crc32(header, entry_blocks) != header->partition_entry_array_crc32) {
	describe_failure("The entry array CRC32 doesn't validate.\n");
	result = false;
    } else describe_success("The entry array CRC32 validates.\n");

    lba n_zero_entries = 0;
    lba i;
    for(i = 0; i < header->number_of_partition_entries; i++) {
	struct partition_entry *entry
	    = (struct partition_entry *) (((uint8_t *) entry_blocks)
					  + i*header->size_of_partition_entry);
	lba j;
	for(j = 0; j < header->size_of_partition_entry; j++)
	    if(((uint8_t *) entry)[j] != 0) break;
	if(j == header->size_of_partition_entry) {
	    n_zero_entries++;
	    continue;
	}

	uuid type_uuid, partition_uuid;
	swab_and_copy_uuid(&type_uuid, &entry->partition_type_uuid);
	swab_and_copy_uuid(&partition_uuid, &entry->unique_partition_uuid);
	
	describe_trivium("Partition entry %Li, type uuid %s:\n",
			 i, uuid_to_ascii(type_uuid));
	char *type_name = get_type_uuid_name(type_uuid);
	if(type_name)
	    describe_trivium("  That's %s.\n", type_name);
	else
	    describe_trivium("  That's an unknown type, to me.\n");
	describe_trivium("  Partition uuid %s.\n",
			 uuid_to_ascii(partition_uuid));
	describe_trivium("  From lba %Li to %Li, attributes 0x%016Lx.\n",
			 entry->starting_lba, entry->ending_lba,
			 entry->attributes);
	describe_trivium("  And it has a name, too.\n");
    }
    describe_trivium("There are a total of %Li entries which are just zeroes.\n",
		     n_zero_entries);

    current_detail--;
    return result;
}


uint32_t compute_header_crc32(block *header_block) {
    block header_copy;
    memcpy(&header_copy, header_block, sizeof(block));
    
    ((struct gpt_header *) header_copy)->header_crc32 = 0x00000000;

    struct gpt_header *header = (struct gpt_header *) header_block;
    size_t size_to_checksum = header->header_size;
    if(size_to_checksum > sizeof(block))
	size_to_checksum = sizeof(block);
    
    return efi_crc32((uint8_t *) &header_copy, size_to_checksum);
}


uint32_t compute_entry_crc32(struct gpt_header *header, block *entry_blocks) {
    return efi_crc32((uint8_t *) entry_blocks, total_entry_size(header));
}


uint64_t total_entry_size(struct gpt_header *header) {
    return header->number_of_partition_entries * header->size_of_partition_entry;
}


char *get_type_uuid_name(uuid to_be_identified) {
    uuid null_uuid;

    bzero(&null_uuid, sizeof(null_uuid));
    
    struct predefined_type_uuid *i = predefined_type_uuids;
    while(1) {
	if(!memcmp(to_be_identified, i->uuid, sizeof(uuid)))
	    return i->name;
	if(!memcmp(null_uuid, i->uuid, sizeof(uuid)))
	    return NULL;
	i++;
    }
}


void swab_and_copy_uuid(uuid *target, uuid *source) {
    memcpy(target, source, sizeof(uuid));
    swab_uuid(target);
}


void swab_uuid(uuid *uuid) {
    uint8_t *bytes = (uint8_t *) uuid;
    swab32(bytes);
    swab16(bytes+4);
    swab16(bytes+6);
}


void swab32(uint8_t *bytes) {
    uint8_t temp[4];

    memcpy(temp, bytes, 4);
    bytes[0] = temp[3];
    bytes[1] = temp[2];
    bytes[2] = temp[1];
    bytes[3] = temp[0];
}


void swab16(uint8_t *bytes) {
    uint8_t temp;

    temp = bytes[0];
    bytes[0] = bytes[1];
    bytes[1] = temp;
}
