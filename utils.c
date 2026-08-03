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