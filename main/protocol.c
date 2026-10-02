#include "protocol.h"
#include <math.h>
#include <string.h>

static void put16(uint8_t *out, uint16_t value)
{
    out[0] = (uint8_t)value;
    out[1] = (uint8_t)(value >> 8);
}

bool rw_parse_command(const uint8_t *bytes, size_t size, rw_command_t *out)
{
    if (out == NULL) return false;
    memset(out, 0, sizeof(*out));
    if (bytes == NULL || size == 0 || size > RW_COMMAND_MAX) return false;
    for (size_t i = 0; i < size; ++i) {
        if (bytes[i] < 32 || bytes[i] > 126) return false;
    }
    if (size >= 4 && memcmp(bytes, "MSG:", 4) == 0) {
        out->kind = RW_COMMAND_MESSAGE;
        memcpy(out->message, bytes + 4, size - 4);
        out->message[size - 4] = '\0';
        return true;
    }
    if (size != 15 || memcmp(bytes, "TIME:", 5) != 0) return false;
    int64_t seconds = 0;
    for (size_t i = 5; i < size; ++i) {
        if (bytes[i] < '0' || bytes[i] > '9') return false;
        seconds = seconds * 10 + (bytes[i] - '0');
    }
    /* PCF85063 uses a two-digit year; support 2024 through 2099. */
    if (seconds < 1704067200LL || seconds >= 4102444800LL) return false;
    out->kind = RW_COMMAND_TIME;
    out->unix_seconds = seconds;
    return true;
}

void rw_pack_telemetry(uint8_t out[RW_TELEMETRY_SIZE], uint8_t flags,
                       float volts, uint32_t steps, const float axes[6])
{
    if (out == NULL) return;
    memset(out, 0, RW_TELEMETRY_SIZE);
    out[0] = 1; /* Wire-format version, unchanged from B1. */
    out[1] = flags;
    if (isfinite(volts) && volts > 0.0f && volts < 6.0f) {
        put16(out + 2, (uint16_t)lroundf(volts * 1000.0f));
    }
    for (unsigned i = 0; i < 4; ++i) out[4 + i] = (uint8_t)(steps >> (8 * i));
    if (axes == NULL) return;
    for (unsigned i = 0; i < 6; ++i) {
        float value = isfinite(axes[i]) ? axes[i] * (i < 3 ? 1000.0f : 10.0f) : 0.0f;
        value = fmaxf(-32768.0f, fminf(32767.0f, value));
        put16(out + 8 + 2 * i, (uint16_t)(int16_t)lroundf(value));
    }
}
