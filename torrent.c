// Bunch of helper functions and stuff placed here so as to not clutter the main.c file.

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include "torrent.h"
#include "bencode.h"
#include "utils.h"
#include "sha1.h"

int load_torrent_file(char *path, char **buffer, size_t *size) {
    FILE *file = fopen(path, "rb");
    if (file == NULL) {
        return -1;
    }

    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return -1;
    }

    long length = ftell(file);
    if (length < 0) {
        fclose(file);
        return -1;
    }

    rewind(file);

    char *data = malloc((size_t)length + 1);
    if (data == NULL) {
        fclose(file);
        return -1;
    }

    size_t read_bytes = fread(data, 1, (size_t)length, file);
    fclose(file);

    if (read_bytes != (size_t)length) {
        free(data);
        return -1;
    }

    *buffer = data;
    *size = read_bytes;
    return 0;
}

Bencode *parse_torrent_file(char *buffer) {
    if (buffer == NULL) {
        return NULL;
    }

    char *pointer = buffer;
    return parse_bencode(&pointer);
}

int compute_info_hash(Bencode *b, uint8_t hash_out[20]) {
    if (b == NULL || hash_out == NULL) {
        return -1;
    }

    Bencode *info_node = find_value("info", b);
    if (info_node == NULL) {
        return -1;
    }

    sha1((const uint8_t *)info_node->start, info_node->len, hash_out);
    return 0;
}

static uint64_t compute_total_size(Bencode *info_node) {
    Bencode *length_node = find_value("length", info_node);
    if (length_node != NULL) {
        return length_node->int_val;
    }

    Bencode *files_node = find_value("files", info_node);
    if (files_node == NULL || files_node->type != BENCODE_LIST) {
        return 0;
    }

    uint64_t total = 0;
    for (uint64_t i = 0; i < files_node->list_val.count; i++) {
        Bencode *file_entry = files_node->list_val.items[i];
        if (file_entry->type != BENCODE_DICT) {
            continue;
        }

        Bencode *file_length = find_value("length", file_entry);
        if (file_length != NULL) {
            total += file_length->int_val;
        }
    }

    return total;
}

size_t compute_bitfield_size(Bencode *b) {
    if (b == NULL) {
        return 0;
    }

    Bencode *info_node = find_value("info", b);
    if (info_node == NULL) {
        return 0;
    }

    Bencode *piece_length_node = find_value("piece length", info_node);
    if (piece_length_node == NULL || piece_length_node->int_val == 0) {
        return 0;
    }

    uint64_t piece_length = piece_length_node->int_val;
    uint64_t total_size = compute_total_size(info_node);
    if (total_size == 0) {
        return 0;
    }

    uint64_t total_pieces = (total_size + piece_length - 1) / piece_length;
    return (size_t)((total_pieces + 7) / 8);
}


int torrent_init(Torrent *torrent, char *torrent_path) {
    // Generating a unique peer ID for every session
    uint8_t peer_id[20];
    generate_peer_id(torrent->peer_id);

    // Opening the file, figureing out the size of it and then closeing the file.
    if (load_torrent_file(torrent_path, &torrent->file_buffer, &torrent->file_size) != 0) {
        printf("Failed to open torrent file: %s\n", torrent_path);
        return 1;
    }

    // Parsing the torrent file into a Bencode structure
    torrent->torrent_meta = parse_torrent_file(torrent->file_buffer);
    if (torrent->torrent_meta == NULL) {
        printf("Failed to parse Bencode\n\n");
        return 1;
    }

    // Computing the info hash of the torrent file
    if (compute_info_hash(torrent->torrent_meta, torrent->info_hash) != 0) {
        printf("Failed to compute torrent info hash\n\n");
        return 1;
    }

    // Computing the size of the bitfield
    torrent->bitfield_size = compute_bitfield_size(torrent->torrent_meta);
    torrent->bitfield = calloc(torrent->bitfield_size, 1);
    if (torrent->bitfield == NULL && torrent->bitfield_size > 0) {
        printf("Failed to allocate bitfield\n");
        return 1;
    }

    // Extracting the announce URLs from the torrent file. (From the announce-list, we skip the simple announce field)
    if (extract_announce_urls(torrent->torrent_meta, &torrent->hosts, &torrent->ports, &torrent->number_of_active_trackers) != 0) {
        printf("Failed to extract announce URLs\n\n");
        return 1;
    }


    return 0;
}