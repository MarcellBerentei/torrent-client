#pragma once

#include <stdint.h>
#include "bencode.h"

int load_torrent_file(char *path, char **buffer, size_t *size);
Bencode *parse_torrent_file(char *buffer);
int compute_info_hash(Bencode *b, uint8_t hash_out[20]);
size_t compute_bitfield_size(Bencode *b);

typedef struct {
    uint8_t peer_id[20];
    uint8_t info_hash[20];
    uint8_t *bitfield;
    char *torrent_path;
    char *file_buffer;
    char **hosts;
    char **ports;
    int number_of_active_trackers;
    size_t file_size;    
    size_t bitfield_size;
    Bencode *torrent_meta;    
} Torrent;

int torrent_init(Torrent *torrent, char *torrent_path);