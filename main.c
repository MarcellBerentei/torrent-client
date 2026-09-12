#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>
#include <stdint.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include "bencode.h"
#include "sha1.h"
#include "tracker.h"
#include "utils.h"
#include "peer.h"
#include "torrent.h"
#include "netmanager.h"
#pragma comment(lib, "ws2_32.lib")

int main(int argc, char *argv[]) {
    srand((unsigned)time(NULL));

    uint8_t peer_id[20];
    generate_peer_id(peer_id);
    printf("Your Client Peer ID: %.*s\n\n", 20, peer_id);

    const char *torrent_path = (argc > 1) ? argv[1] : "test.torrent";
    if (argc < 2) {
        printf("No torrent file was entered, using default.\n\n");
    }

    char *file_buffer = NULL;
    size_t file_size = 0;
    if (load_torrent_file(torrent_path, &file_buffer, &file_size) != 0) {
        printf("Failed to open torrent file: %s\n", torrent_path);
        return 1;
    }

    printf("Size of the file read is: %zu bytes.\n\n", file_size);

    Bencode *torrent_meta = parse_torrent_file(file_buffer);
    if (torrent_meta == NULL) {
        printf("Failed to parse Bencode\n\n");
        free(file_buffer);
        return 1;
    }

    printf("Successfully parsed the torrent file!\n\n");

    char **ip_names = NULL;
    char **ports = NULL;
    int accepted_count = 0;
    if (extract_announce_urls(torrent_meta, &ip_names, &ports, &accepted_count) != 0) {
        printf("Failed to extract announce URLs\n\n");
        free_bencode(torrent_meta);
        free(file_buffer);
        return 1;
    }

    printf("Successfully extracted announce URLs!\n\n");

    uint8_t info_hash[20];
    if (compute_info_hash(torrent_meta, info_hash) != 0) {
        printf("Failed to compute torrent info hash\n\n");
        free_bencode(torrent_meta);
        free(file_buffer);
        return 1;
    }

    size_t bitfield_size = compute_bitfield_size(torrent_meta);
    uint8_t *bitfield = calloc(bitfield_size, 1);
    if (bitfield == NULL && bitfield_size > 0) {
        printf("Failed to allocate bitfield\n");
        free_bencode(torrent_meta);
        free(file_buffer);
        return 1;
    }

    init_networking();

    struct addrinfo hints;
    ZeroMemory(&hints, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_protocol = IPPROTO_UDP;

    Peer *swarm = NULL;
    int swarm_count = 0;
    if (collect_peers(NULL, &hints, info_hash, peer_id, accepted_count, ip_names, ports, &swarm, &swarm_count) != 0) {
        printf("Failed to collect peers from trackers\n");
        free(bitfield);
        free_bencode(torrent_meta);
        free(file_buffer);
        for (int i = 0; i < accepted_count; i++) {
            free(ip_names[i]);
            free(ports[i]);
        }
        free(ip_names);
        free(ports);
        WSACleanup();
        return 1;
    }

    // Winsock2 thingy
    WSAPOLLFD socket_poll_array[swarm_count];


    // === Connect Sockets === //
    for (int p = 0; p < swarm_count; p++) {
        // Putting the brand new peer into this thing
        socket_poll_array[p].fd = swarm[p].socket;
        socket_poll_array[p].revents = 0;

        connect(swarm[p].socket, (struct sockaddr*)&swarm[p].address, sizeof(swarm[p].address));
        swarm[p].is_connecting = 1;
        socket_poll_array[p].events = POLLOUT;
    }


    // === Talk to the peers === //
    if (process_swarm(swarm, swarm_count, info_hash, peer_id) != 0) {
        printf("Error processing swarm\n");
    }

    // === Cleanup === //
    cleanup_swarm(swarm, swarm_count);
    free(swarm);
    free_bencode(torrent_meta);
    free(file_buffer);
    for (int i = 0; i < accepted_count; i++) {
        free(ip_names[i]);
        free(ports[i]);
    }
    free(ip_names);
    free(ports);
    free(bitfield);
    WSACleanup();
    return 0;
}