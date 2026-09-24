#pragma once

#include <stdint.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include "peer.h"
#include "torrent.h"

int collect_peers(struct addrinfo *result, struct addrinfo *hints, uint8_t hash[20], uint8_t peer_id[20], int accepted_count, char **hosts, char **ports, PeerConnection **swarm, int *swarm_count);

int tracker_collect_peers(Torrent *torrent, PeerConnection **peers, int *peer_count);