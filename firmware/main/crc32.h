#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Start with crc = 0. Can be called repeatedly to continue over more data.
uint32_t crc32_update(uint32_t crc, const uint8_t *buf, size_t len);
