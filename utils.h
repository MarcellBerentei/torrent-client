#pragma once

#include <stdint.h>


void generate_peer_id(uint8_t peer_id[20]);

void hash_copy(uint8_t *destination, uint8_t *source, size_t length);

int hash_equals(uint8_t *a, uint8_t *b, size_t length);


int bitfield_get(uint8_t *bitfield, size_t bitfield_length, size_t bit_index);

void bitfield_set(uint8_t *bitfield, size_t bitfield_length, size_t bit_index);