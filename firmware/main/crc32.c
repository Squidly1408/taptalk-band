// Standard CRC-32 (IEEE 802.3, same as zlib crc32), table-driven.
#include "crc32.h"

static uint32_t s_table[256];
static bool s_ready;

static void build_table(void)
{
    for (uint32_t i = 0; i < 256; i++) {
        uint32_t c = i;
        for (int k = 0; k < 8; k++) {
            c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
        }
        s_table[i] = c;
    }
    s_ready = true;
}

uint32_t crc32_update(uint32_t crc, const uint8_t *buf, size_t len)
{
    if (!s_ready) {
        build_table();
    }
    crc = ~crc;
    while (len--) {
        crc = s_table[(crc ^ *buf++) & 0xFF] ^ (crc >> 8);
    }
    return ~crc;
}
