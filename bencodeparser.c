#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <ctype.h>
#include "bencodeparser.h"

#define ALLOC(type) (type *)malloc(sizeof(type)) // Total mystery. Some kind of macro magic.

char* GLOBAL_start_of_info = NULL;
char* GLOBAL_end_of_info = NULL;

Bencode *parse_list(const char **input) {
    if (**input != 'l') return NULL;
    (*input)++; // Skip over the 'l'

    Bencode *b = ALLOC(Bencode);
    b->type = BENCODE_LIST;
    b->list_val.items = NULL;
    b->list_val.count = 0;

    while (**input && **input != 'e') {
        Bencode *item = parse_bencode(input);
        if (!item) return NULL;

        b->list_val.items = realloc(b->list_val.items, (b->list_val.count + 1) * sizeof(Bencode *)); // Reallocation, same as in parse_dict.
        b->list_val.items[b->list_val.count++] = item; // Some voodoo magic. I'm guessing it means to add one to it and also it the new value.
    }

    if (**input != 'e') return NULL;
    (*input)++;
    return b;
}

Bencode *parse_int(const char **input) {
    if (**input != 'i') return NULL;
    (*input)++; // Skip over the 'i'

    char *end;
    size_t val = strtol(*input, &end, 10);
    if (*end != 'e') return NULL;
    *input = end + 1;

    Bencode *b = ALLOC(Bencode);
    b->type = BENCODE_INT;
    b->int_val = val;
    return b;
}

Bencode *parse_str(const char **input) {
    if (!isdigit(**input)) return NULL;

    char *end;
    int len = strtol(*input, &end, 10); // Convert the input string (which is the entire .torrent file) to a long. Presumably it knows to stop at the first char which isn't a number.
    if (*end != ':') return NULL;
    end++;

    Bencode *b = ALLOC(Bencode);            // Allocate enough space for the struct
    b->type = BENCODE_STR;
    b->string_val.len = len;
    b->string_val.str = malloc(len + 1);    // Allocate space for the string, + 1?
    memcpy(b->string_val.str, end, len);    // Copy to the str slot from the end pointer (start of the str).
    b->string_val.str[len] = '\0';          // End it off with this so it's a proper string.

    *input = end + len;                     // Skip ahead to the end of the string.
    return b;
}

Bencode *parse_dict(const char **input) {
    if (**input != 'd') return NULL;
    (*input)++; // Skip over the 'd'

    Bencode *b = ALLOC(Bencode);
    b->type = BENCODE_DICT;
    b->dict_val.keys = NULL;
    b->dict_val.values = NULL;
    b->dict_val.count = 0;

    while (**input && **input != 'e') { // As long as the current char we're looking at is something and not 'e', loop.
        Bencode *key = parse_str(input);
        if (!key || key->type != BENCODE_STR) return NULL; // If key is invalid OR if key is not of type BENCODE_STR, run away!.
        if (!strcmp((key->string_val.str), "info")) {
            GLOBAL_start_of_info = (char*)*input;
        }

        Bencode *val = parse_bencode(input); // So far we decoded the key because it's always a string. Now we have to find out what type is the value.
        if (!val) return NULL;
        if (!strcmp((key->string_val.str), "info")) {
            GLOBAL_end_of_info = (char*)*input;
        }

        // We allocate some more memory for the dictionary.
        b->dict_val.keys = realloc(b->dict_val.keys, (b->dict_val.count + 1) * sizeof(Bencode *));
        b->dict_val.values = realloc(b->dict_val.values, (b->dict_val.count + 1) * sizeof(Bencode *));
        
        b->dict_val.keys[b->dict_val.count] = key;
        b->dict_val.values[b->dict_val.count] = val;

        b->dict_val.count++;

        // So in this loop we don't actually step forward, all of that is done by the other parser funtions.
    }

    if (**input != 'e') return NULL;
    (*input)++;
    return b;
}

Bencode *parse_bencode(const char **input) {
    if (**input == 'i') return parse_int(input);
    if (**input == 'l') return parse_list(input);
    if (**input == 'd') return parse_dict(input);
    if (isdigit(**input)) return parse_str(input);
    return NULL;
}

void free_bencode(Bencode *b) {
    if (!b) return;
    switch (b->type) {
        case BENCODE_STR:
            free(b->string_val.str);
            break;
        case BENCODE_LIST:
            for (int i = 0; i < b->list_val.count; i++)
                free_bencode(b->list_val.items[i]);
            free(b->list_val.items);
            break;
        case BENCODE_DICT:
            for (int i = 0; i < b->dict_val.count; i++) {
                free_bencode(b->dict_val.keys[i]);
                free_bencode(b->dict_val.values[i]);
            }
            free(b->dict_val.keys);
            free(b->dict_val.values);
            break;
        default:
            break;
    }
    free(b);
}

// Print Bencode (debugging) (Shamelessly stolen from ChatGPT, please forgive me)
void print_bencode(Bencode *b, int indent) {
    for (int i = 0; i < indent; i++) printf("  ");
    switch (b->type) {
        case BENCODE_INT:
            printf("int: %ld\n", b->int_val);
            break;
        case BENCODE_STR:
            printf("str: %.*s\n", b->string_val.len, b->string_val.str);
            break;
        case BENCODE_LIST:
            printf("list:\n");
            for (int i = 0; i < b->list_val.count; i++)
                print_bencode(b->list_val.items[i], indent + 1);
            break;
        case BENCODE_DICT:
            printf("dict:\n");
            for (int i = 0; i < b->dict_val.count; i++) {
                print_bencode(b->dict_val.keys[i], indent + 1);
                print_bencode(b->dict_val.values[i], indent + 2);
            }
            break;
    }
}