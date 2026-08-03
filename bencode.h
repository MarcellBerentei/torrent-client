#pragma once

#include <stdint.h>

#define ALLOC(type) (type*)malloc(sizeof(type))

typedef enum {BENCODE_INT, BENCODE_STRING, BENCODE_LIST, BENCODE_DICT} BencodeType;

typedef struct Bencode {
    BencodeType type;

    char *start;
    size_t len;

    union {
        uint64_t int_val;

        struct {
            char *str;
            uint64_t length;
        } string_val;

        struct {
            struct Bencode **items;
            uint64_t count; 
        } list_val;

        struct {
            struct Bencode **keys;
            struct Bencode **values;
            uint64_t count;
        } dict_val;
    };
} Bencode;

Bencode *parse_bencode(char **input);

Bencode *parse_list(char **input);

Bencode *parse_int(char **input);

Bencode *parse_str(char **input);

Bencode *parse_dict(char **input);

void free_bencode(Bencode *b);

void print_bencode(Bencode *b, int indent);

Bencode *find_value(char* key, Bencode *b);

int extract_announce_urls(Bencode *b, char ***ip_names, char ***ports, int *accepted_count);