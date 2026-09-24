#include <stdlib.h>
#include <time.h>
#include <string.h>
#include <stdint.h>
#include "utils.h"

void generate_peer_id(uint8_t peer_id[20]) {
    const char *prefix = "-TT0001-";
    memcpy(peer_id, prefix, 8);

    const char charset[] = "0123456798"
                           "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
                           "abcdefghijklmnopqrstuvwxyz";
    int charset_size = sizeof(charset) - 1;

    for (int i = 8; i < 20; i++) {
        peer_id[i] = charset[rand() % charset_size];
    }
}

void hash_copy(uint8_t *destination, uint8_t *source, size_t length) {
    memcpy(destination, source, length);
}

int hash_equals(uint8_t *a, uint8_t *b, size_t length) {
    return memcmp(a, b, length) == 0;
}


int bitfield_get(uint8_t *bitfield, size_t bitfield_length, size_t bit_index) {
    // Because C always rounds down divisons, we divide the bit_index by eight
    // to get which byte the bit is in. (10. bit will be in the 2. byte)
    size_t byte_index = bit_index / 8;
    // We get the remainder of the divison with modulo to get which bit is the
    // one we want in the byte.
    size_t bit_offset = bit_index % 8;

    if (byte_index >= bitfield_length) {
        return 0; // We reached the end of the bitfield
    }

    return (bitfield[byte_index] >> (7 - bit_offset)) & 1U;
}

void bitfield_set(uint8_t *bitfield, size_t bitfield_length, size_t bit_index) {
    // Because C always rounds down divisons, we divide the bit_index by eight
    // to get which byte the bit is in. (10. bit will be in the 2. byte)
    size_t byte_index = bit_index / 8;
    // We get the remainder of the divison with modulo to get which bit is the
    // one we want in the byte.
    size_t bit_offset = bit_index % 8;

    if (byte_index >= bitfield_length) {
        return; // We reached the end of the bitfield
    }

    bitfield[byte_index] |= (uint8_t)(1U << (7 - bit_offset));
}

