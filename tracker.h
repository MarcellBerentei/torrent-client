#pragma once

#include <stdint.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include "peer.h"

int collect_peers(struct addrinfo *result, struct addrinfo *hints, uint8_t hash[20], uint8_t peer_id[20], int accepted_count, char **ip_names, char **ports, Peer **swarm, int *swarm_count);