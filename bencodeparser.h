#pragma once
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdint.h>

typedef enum { BENCODE_INT, BENCODE_STR, BENCODE_LIST, BENCODE_DICT } BencodeType;

typedef struct Bencode { // A struct with a BencodeType and 4 possible structs or long int. Only one of them can be real at a time.
    BencodeType type;
    union {
        uint64_t int_val;
        struct {
            char *str;
            int len;
        } string_val;
        struct {
            struct Bencode **items;
            int count;
        } list_val;
        struct {
            struct Bencode **keys;
            struct Bencode **values;
            int count;
        } dict_val;
    };
} Bencode;

Bencode *parse_bencode(const char **input);

Bencode *parse_list(const char **input);

Bencode *parse_int(const char **input);

Bencode *parse_str(const char **input);

Bencode *parse_dict(const char **input);

Bencode *parse_bencode(const char **input);

void free_bencode(Bencode *b);

// Print Bencode (debugging) (Shamelessly stolen from ChatGPT, please forgive me)
void print_bencode(Bencode *b, int indent);