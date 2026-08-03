#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include "torrent.h"
#include "sha1.h"

int load_torrent_file(const char *path, char **buffer, size_t *size) {
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
