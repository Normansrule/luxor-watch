#pragma once
/* Portable protocol code: no ESP-IDF dependency. All integers are little-endian. */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define RW_TELEMETRY_SIZE 20
#define RW_COMMAND_MAX 120
#define RW_MESSAGE_MAX 116

typedef enum { RW_COMMAND_INVALID, RW_COMMAND_MESSAGE, RW_COMMAND_TIME } rw_command_kind_t;
typedef struct {
    rw_command_kind_t kind;
    int64_t unix_seconds;
    char message[RW_MESSAGE_MAX + 1];
} rw_command_t;
/* Strict printable-ASCII protocol. No embedded NUL, control characters or signs. */
bool rw_parse_command(const uint8_t *bytes, size_t size, rw_command_t *out);
void rw_pack_telemetry(uint8_t out[RW_TELEMETRY_SIZE], uint8_t flags,
                       float volts, uint32_t steps, const float axes[6]);
