#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include "sha1.h"

#define LEFTROTATE(x, c) (((x) << (c)) | ((x) >> (32 - (c))))

void sha1(const uint8_t *message, size_t len, uint8_t hash[20]) {

    // These starting variables are specified by the SHA-1 standard.
    uint32_t h0 = 0x67452301;
    uint32_t h1 = 0xEFCDAB89;
    uint32_t h2 = 0x98BADCFE;
    uint32_t h3 = 0x10325476;
    uint32_t h4 = 0xC3D2E1F0;

    // Pre-processing
    size_t original_len_bits = len * 8; // Original length in bits
    size_t new_len = len + 1;
    while ((new_len * 8) % 512 != 448) new_len++; // Perhaps a bit slow, but works

    uint8_t *msg = calloc(new_len + 8, 1); // +8 for the 64-bit length
    memcpy(msg, message, len);
    msg[len] = 0x80; // Add the 10000000

    // Append original length
    for (int i = 0; i < 8; i++) {
        msg[new_len + i] = (original_len_bits >> ((7 - i) * 8)) & 0xFF;
    }

    for (size_t offset = 0; offset < new_len + 8; offset += 64) {
        uint32_t w[80];
        for (int i = 0; i < 16; i++) {
            w[i] = (msg[offset + i * 4 + 0] << 24) |
                   (msg[offset + i * 4 + 1] << 16) |
                   (msg[offset + i * 4 + 2] << 8) |
                   (msg[offset + i * 4 + 3]);
        }
        for (int i = 16; i < 80; i++) {
            w[i] = LEFTROTATE(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
        }

        uint32_t a = h0;
        uint32_t b = h1;
        uint32_t c = h2;
        uint32_t d = h3;
        uint32_t e = h4;

        for (int i = 0; i < 80; i++) {
            uint32_t f, k;
            if (i < 20) {
                f = (b & c) | ((~b) & d);
                k = 0x5A827999;
            } else if (i < 40) {
                f = b ^ c ^ d;
                k = 0x6ED9EBA1;
            } else if (i < 60) {
                f = (b & c) | (b & d) | (c & d);
                k = 0x8F1BBCDC;
            } else {
                f = b ^ c ^ d;
                k = 0xCA62C1D6;
            }

            uint32_t temp = LEFTROTATE(a, 5) + f + e + k + w[i];
            e = d;
            d = c;
            c = LEFTROTATE(b, 30);
            b = a;
            a = temp;
        }

        h0 += a;
        h1 += b;
        h2 += c;
        h3 += d;
        h4 += e;
    }

    free(msg);

    // Output result
    uint32_t hash_parts[5] = { h0, h1, h2, h3, h4 };
    for (int i = 0; i < 5; i++) {
        hash[i * 4 + 0] = (hash_parts[i] >> 24) & 0xff;
        hash[i * 4 + 1] = (hash_parts[i] >> 16) & 0xff;
        hash[i * 4 + 2] = (hash_parts[i] >> 8) & 0xff;
        hash[i * 4 + 3] = (hash_parts[i]) & 0xff;
    }
}

void print_hash(uint8_t hash[20]) {
    for (int i = 0; i < 20; i++) {
        printf("%02X", hash[i]);
    }
    printf("\n");
}

void print_URLhash(uint8_t hash[40]) {
    for (int i = 0; i < 40; i++) {
        if (hash[i] == 0x25) {
            printf("%c", 0x25);
        } else {
            printf("%02X", hash[i]);
        }
    }
    printf("\n");
}
