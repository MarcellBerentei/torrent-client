#pragma once

#include <stdint.h>
#include "bencode.h"

int load_torrent_file(const char *path, char **buffer, size_t *size);
Bencode *parse_torrent_file(char *buffer);
int compute_info_hash(Bencode *b, uint8_t hash_out[20]);
size_t compute_bitfield_size(Bencode *b);
