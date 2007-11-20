#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

// efi_crc32.c
extern uint32_t efi_crc32(uint8_t *buf, size_t len);
extern uint32_t efi_crc32_start(uint8_t *buf, size_t len);
extern uint32_t efi_crc32_continue(uint8_t *buf, size_t len, uint32_t seed);
extern uint32_t efi_crc32_end(uint32_t seed);

// ui.c
extern int current_detail, cutoff_detail, describe_failures, describe_successes,
    describe_trivia;
