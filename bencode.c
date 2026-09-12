// Recursive evil lies ahead, beware traveller, for few have returned from the depths of this code unscathed.

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#include "bencode.h"

Bencode *parse_int(char **input) {
    if (**input != 'i') return NULL;
    (*input)++;

    char *end;
    uint64_t val = strtol(*input, &end, 10); // This sets end to the char after the last number char.
    if (*end != 'e') return NULL;            // If there isn't an e there something went wrong.
    *input = end + 1;                        // Set the input to the char after 'e'.

    Bencode *b = ALLOC(Bencode);
    b->type = BENCODE_INT;
    b->int_val = val;
    return b;
}

Bencode *parse_str(char **input) {
    if (!isdigit(**input)) return NULL;

    char *end;
    uint64_t length = strtol(*input, &end, 10); // This sets end to the char after the last number char.
    if (*end != ':') return NULL;               // If there isn't an : there something went wrong.
    end++;                                      // Set the input to the char after ':'.

    Bencode *b = ALLOC(Bencode);
    b->type = BENCODE_STRING;
    b->string_val.length = length;
    b->string_val.str = malloc(length + 1);  // +1 because of \0.
    memcpy(b->string_val.str, end, length);
    b->string_val.str[length] = '\0';           // Top it of with a null terminator.

    *input = end + length;
    return b;
}

Bencode *parse_list(char **input) {
    if (**input != 'l') return NULL;
    (*input)++;

    Bencode *b = ALLOC(Bencode);
    b->type = BENCODE_LIST;
    b->list_val.items = NULL;
    b->list_val.count = 0;

    while (**input && **input != 'e') {
        Bencode *item = parse_bencode(input);
        if (!item) return NULL;

        b->list_val.items = realloc(b->list_val.items, (b->list_val.count + 1) * sizeof(Bencode*));
        b->list_val.items[b->list_val.count++] = item;
    }

    (*input)++;
    return b;
}

Bencode *parse_dict(char **input) {
    if (**input != 'd') return NULL;
    (*input)++;

    Bencode *b = ALLOC(Bencode);
    b->type = BENCODE_DICT;
    b->dict_val.keys = NULL;
    b->dict_val.values = NULL;
    b->dict_val.count = 0;

    while (**input && **input != 'e') {
        Bencode *key = parse_bencode(input);
        if (!key || key->type != BENCODE_STRING) return NULL;

        Bencode *value = parse_bencode(input);
        if (!value) return NULL;

        b->dict_val.keys = realloc(b->dict_val.keys, (b->dict_val.count + 1) * sizeof(Bencode*));
        b->dict_val.values = realloc(b->dict_val.values, (b->dict_val.count + 1) * sizeof(Bencode*));

        b->dict_val.keys[b->dict_val.count] = key;
        b->dict_val.values[b->dict_val.count] = value;

        b->dict_val.count++;
    }

    if (**input != 'e') return NULL;
    (*input)++;
    return b;
}

Bencode *parse_bencode(char **input) {
    char *start_ptr = *input;

    Bencode *b = NULL;
    if (**input == 'i')   b =  parse_int(input);
    else if (isdigit(**input)) b =  parse_str(input);
    else if (**input == 'l')   b =  parse_list(input);
    else if (**input == 'd')   b =  parse_dict(input);

    if (b != NULL) {
        b->start = start_ptr;
        b->len   = *input - start_ptr;

        return b;
    } else {
        return NULL;
    }
}

void free_bencode(Bencode *b) {
    switch (b->type) {
        case BENCODE_STRING:
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

void print_bencode(Bencode *b, int indent) {
    for (int i = 0; i < indent; i++) printf("   ");
    switch (b->type) {
        case BENCODE_INT:
            printf("Int: %ld\n", b->int_val);
            break;
        case BENCODE_STRING:
            printf("String: %.*s\n", b->string_val.length, b->string_val.str);
            break;
        case BENCODE_LIST:
            printf("List:\n");
            for (int i = 0; i < b->list_val.count; i++) {
                print_bencode(b->list_val.items[i], indent + 1);
            }
            break;
        case BENCODE_DICT:
            printf("Dictionary:\n");
            for (int i = 0; i < b->dict_val.count; i++) {
                print_bencode(b->dict_val.keys[i], indent + 1);
                print_bencode(b->dict_val.values[i], indent + 2);
            }
            break;
        default:
            break;
    }

}

Bencode *find_value(char* key, Bencode *b) {
    Bencode *value = NULL;

    switch (b->type) {
        case BENCODE_LIST:
            for (int i = 0; i < b->list_val.count; i++) {
                value = find_value(key, b->list_val.items[i]);
                if (value) return value;
            }
            break;

        case BENCODE_DICT:
            for (int i = 0; i < b->dict_val.count; i++) {
                if (strcmp(b->dict_val.keys[i]->string_val.str, key) == 0) {
                    return b->dict_val.values[i];
                }
                value = find_value(key, b->dict_val.values[i]);
                if (value) return value;
            }
            break;

        default:
            break;
    }

    return NULL;
}

// Extracting and filtering announce URLs
int extract_announce_urls(Bencode *b, char ***ip_names, char ***ports, int *accepted_count) {
    Bencode *ips = find_value("announce-list", b);
    if (!ips) {
        printf("No announce-found in torrent file\n\n");
        return 1;
    }

    char **ip_hosts = malloc(ips->list_val.count * sizeof(char*));
    *accepted_count = 0;

    for (int i = 0; i < ips->list_val.count; i++) {
        char *str = ips->list_val.items[i]->list_val.items[0]->string_val.str;
        int str_len = ips->list_val.items[i]->list_val.items[0]->string_val.length;

        // Skip WebSocket protocols
        if (strncmp(str, "wss://", 6) == 0 || strncmp(str, "ws://", 5) == 0) continue;

        ip_hosts[(*accepted_count)] = malloc(str_len + 1);
        memcpy(ip_hosts[(*accepted_count)], str, str_len);
        ip_hosts[(*accepted_count)][str_len] = '\0';
        (*accepted_count)++;
    }

    // Separating the host and port
    *ip_names = malloc(*accepted_count * sizeof(char*));
    *ports = malloc(*accepted_count * sizeof(char*));

    for (int i = 0; i < *accepted_count; i++) {
        char *trimmed_ip = strstr(ip_hosts[i], "://") + 3;
        char *port_ptr = strchr(trimmed_ip, ':');
        
        if (port_ptr) {
            *port_ptr = '\0'; // Terminate the host string at the colon
            (*ip_names)[i] = strdup(trimmed_ip);
            (*ports)[i] = strdup(port_ptr + 1);
        }
    }
    return 0;
}