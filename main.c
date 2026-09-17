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
#pragma comment(lib, "ws2_32.lib")

int main(int argc, char *argv[]) {
    srand((unsigned)time(NULL));

    Torrent torrent = {0};
    char *torrent_path = (argc > 1) ? argv[1] : "test.torrent";
    PeerConnection *peers = {0};

    if (torrent_init(&torrent, torrent_path) != 0) return 1;

    // From here, network.c should take over
    // Extracting the announce URLs from the torrent file. (From the announce-list, we skip the simple announce field)
    char **hosts = NULL;
    char **ports = NULL;
    int accepted_count = 0;
    if (extract_announce_urls(torrent.torrent_meta, &hosts, &ports, &accepted_count) != 0) {
        printf("Failed to extract announce URLs\n\n");
        return 1;
    }


    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        printf("WSAStartup failed\n");
        return 1;
    }

    // Bunch of winsock2 boilerplate that should be hidden in a wrapper
    struct addrinfo hints;
    ZeroMemory(&hints, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_protocol = IPPROTO_UDP;

    // This talks to all the trackers and collects the peers from them.
    // This should be called from network.c
    Peer *swarm = NULL;
    int swarm_count = 0;
    if (collect_peers(NULL, &hints, torrent.info_hash, torrent.peer_id, accepted_count, hosts, ports, &swarm, &swarm_count) != 0) {
        printf("Failed to collect peers from trackers\n");
        for (int i = 0; i < accepted_count; i++) {
            free(hosts[i]);
            free(ports[i]);
        }
        free(hosts);
        free(ports);
        WSACleanup();
        return 1;
    }

    // Winsock2 thingy
    // I'm really not sure if this is the best way to do this, but it works for now. I might change this later.
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

    // End of network.c, should be handed over to peer.c

    // === Talk to the peers === //
    if (process_swarm(swarm, swarm_count, torrent.info_hash, torrent.peer_id) != 0) {
        printf("Error processing swarm\n");
    }

/*
    // This can stay here
    // === Cleanup === //
    cleanup_swarm(swarm, swarm_count);
    free(swarm);
    free_bencode(torrent_meta);
    free(file_buffer);
    for (int i = 0; i < accepted_count; i++) {
        free(hosts[i]);
        free(ports[i]);
    }
    free(hosts);
    free(ports);
    free(bitfield);
    WSACleanup();
    */
    return 0;
}