#include "protocol.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
static bool parse(const char *s, rw_command_t *c)
{ return rw_parse_command((const uint8_t *)s, strlen(s), c); }
int main(void)
{
    rw_command_t c;
    assert(parse("MSG:Hello wrist", &c));
    assert(c.kind == RW_COMMAND_MESSAGE && strcmp(c.message, "Hello wrist") == 0);
    assert(parse("MSG:", &c) && c.message[0] == 0);
    assert(parse("TIME:1704067200", &c) && c.unix_seconds == 1704067200LL);
    assert(parse("TIME:4102444799", &c));
    assert(!parse("TIME:4102444800", &c));
    assert(!parse("TIME:1704067199", &c));
    assert(!parse("TIME:+1704067200", &c));
    assert(!parse("TIME:1704067200x", &c));
    assert(!parse("MSG:bad\nline", &c));
    assert(!parse("", &c));
    assert(!rw_parse_command(NULL, 1, &c));
    assert(!rw_parse_command((const uint8_t *)"MSG:a\0b", 7, &c));
    char max[121]; memset(max, 'a', 120); memcpy(max, "MSG:", 4); max[120] = 0;
    assert(parse(max, &c) && strlen(c.message) == 116);
    assert(!rw_parse_command((const uint8_t *)max, 121, &c));
    uint8_t b[20]; float axes[6] = {1.0f, -1.0f, 0.0f, 90.0f, -90.0f, NAN};
    rw_pack_telemetry(b, 7, 3.7f, 0x12345678U, axes);
    assert(b[0] == 1 && b[1] == 7 && b[2] == 0x74 && b[3] == 0x0e);
    assert(b[4] == 0x78 && b[5] == 0x56 && b[6] == 0x34 && b[7] == 0x12);
    assert(b[8] == 0xe8 && b[9] == 0x03 && b[10] == 0x18 && b[11] == 0xfc);
    assert(b[14] == 0x84 && b[15] == 0x03 && b[16] == 0x7c && b[17] == 0xfc);
    assert(b[18] == 0 && b[19] == 0);
    rw_pack_telemetry(b, 0, NAN, 0, NULL);
    for (int i = 1; i < 20; ++i) assert(b[i] == 0);
    puts("PASS: protocol validation, bounds, byte order, invalid input and telemetry encoding");
    return 0;
}
