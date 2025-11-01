#pragma once
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

void sha1(const uint8_t *message, size_t len, uint8_t hash[20]);

void print_hash(uint8_t hash[20]);

void print_URLhash(uint8_t hash[40]);
