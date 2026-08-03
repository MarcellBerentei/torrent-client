#pragma once

#include <stdint.h>

void sha1(const uint8_t *message, size_t length, uint8_t hash[20]);

void print_hash(uint8_t hash[20]);

void print_URLhash(uint8_t hash[40]);