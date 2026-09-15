// This whole thing is a mess, someone needs to organize this code and make it more readable.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include "bencode.h"
#include "sha1.h"
#include "tracker.h"
#include "peer.h"

typedef struct {
    uint64_t connection_id;
    struct sockaddr_in addr;
    int addr_len;
    int is_resolved;
} Tracker;

int collect_peers(struct addrinfo *result, struct addrinfo *hints, uint8_t hash[20], uint8_t peer_id[20], int accepted_count, char **hosts, char **ports, Peer **swarm, int *swarm_count) {
    // Setting up UDP socket
    SOCKET TrackerSocket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (TrackerSocket == INVALID_SOCKET) {
        printf("Failed to create tracker socket | Error: %d\n", WSAGetLastError());
        return -1;
    }

    Tracker *trackers = calloc(accepted_count, sizeof(Tracker));
    if (trackers == NULL) {
        printf("Failed to allocate tracker array\n");
        closesocket(TrackerSocket);
        return -1;
    }

    DWORD timeout = 2000;
    setsockopt(TrackerSocket, SOL_SOCKET, SO_RCVTIMEO, (char*)&timeout, sizeof(timeout));

    if (swarm == NULL || swarm_count == NULL) {
        free(trackers);
        closesocket(TrackerSocket);
        return -1;
    }

    *swarm = NULL;
    *swarm_count = 0;  // Initialize peer count


    for (int i = 0; i < accepted_count; i++) {
        int ws2_result = getaddrinfo(hosts[i], ports[i], hints, &result);
        if (ws2_result) {
            trackers[i].is_resolved = 0;
            printf("Failed to resolve %s:%s: %d\n", hosts[i], ports[i], ws2_result);
            continue;
        } else {
            trackers[i].is_resolved = 1;
            trackers[i].addr_len = (int)result->ai_addrlen;
            memcpy(&trackers[i].addr, result->ai_addr, result->ai_addrlen);
            printf("Successfully resolved %s:%s\n", hosts[i], ports[i]);
        }

        freeaddrinfo(result);
    }


    printf("\n\nContacting trackers...\n\n");


    for (int i = 0; i < accepted_count; i++) {
        if (!trackers[i].is_resolved) continue;

        printf("\n\n----------------------------------\n### %s:%s ###\n\n", hosts[i], ports[i]);

        // --- Sending --- //
        ConnectRequest request;
        request.connection_id  = htonll(0x41727101980LL); // Magic number for bittorrent
        request.action         = htonl(0);
        request.transaction_id = htonl(rand());

        int bytes_sent = sendto(TrackerSocket, (char*)&request, sizeof(request), 0, (struct sockaddr*)&trackers[i].addr, trackers[i].addr_len);

        if (bytes_sent == SOCKET_ERROR) {
            printf("Failed to send connect request | Error: %d\n", WSAGetLastError());
            continue;
        }

        // --- Receiving --- //
        ConnectResponse response;
        int sender_addr_len = sizeof(trackers[i].addr);

        int bytes_received = recvfrom(TrackerSocket, (char*)&response, sizeof(response), 0, (struct sockaddr*)&trackers[i].addr, &sender_addr_len);

        if (bytes_received == SOCKET_ERROR) {
            int err = WSAGetLastError();
            if (err == WSAETIMEDOUT) {
                printf("Timed out, maybe ignored us (rude)\n\n");
            } else {
                printf("Receive failed | Error: %d\n\n", err);
            }
            continue;
        }

        uint32_t response_transaction_id = ntohl(response.transaction_id);

        if (response_transaction_id != ntohl(request.transaction_id)) {
            printf("Mismatched transaction id, dropping response!\n\n");
            continue;
        }

        trackers[i].connection_id = response.connection_id;
        //printf("SUCCESS! Received connection ID | %llu\n\n", trackers[i].connection_id);

        // --- Sending Announce Request--- //
        AnnounceRequest announce_request;

        announce_request.connection_id = response.connection_id;
        announce_request.action = htonl(1);
        announce_request.transaction_id = htonl(rand());

        memcpy(announce_request.info_hash, hash, 20);
        memcpy(announce_request.peer_id, peer_id, 20);

        announce_request.downloaded = htonll(0);
        announce_request.left = htonll(0);
        announce_request.uploaded = htonll(0);
        announce_request.event = htonl(2);
        announce_request.ip_address = htonl(0);
        announce_request.key = htonl(rand());
        announce_request.num_want = htonl(-1);
        announce_request.port = htons(6881);

        bytes_sent = sendto(TrackerSocket, (char*)&announce_request, sizeof(announce_request), 0, (struct sockaddr*)&trackers[i].addr, trackers[i].addr_len);
        if (bytes_sent == SOCKET_ERROR) {
            printf("Failed to send announce-request to %s:%s | Error: %d\n", hosts[i], ports[i], WSAGetLastError());
            continue;
        }

        // --- Receiving Announce Response --- //
        unsigned char announce_buffer[65536];
        memset(announce_buffer, 0, sizeof(announce_buffer));
        sender_addr_len = sizeof(trackers[i].addr);

        bytes_received = recvfrom(TrackerSocket, announce_buffer, sizeof(announce_buffer), 0, (struct sockaddr*)&trackers[i].addr, &sender_addr_len);
        if (bytes_received == SOCKET_ERROR) {
            int err = WSAGetLastError();
            if (err == WSAETIMEDOUT) {
                printf("Announce response timed out\n\n");
            } else if (err == WSAEMSGSIZE) {
                printf("Message too long! (This should not have happened!)\n\n");
            } else {
                printf("Receive failed | Error: %d\n\n", err);
            }
            continue;
        }

        // Parse out the header action code to make sure it isn't an error packet
        uint32_t res_action = ntohl(*(uint32_t*)&announce_buffer[0]);
        printf("[DEBUG] Raw Bytes Received: %d | Action Code from Tracker: %u\n", bytes_received, res_action);
        if (res_action == 3) {
            printf("Tracker sent an error string: %s\n\n", &announce_buffer[8]);
            continue;
        }


        int peer_bytes = (bytes_received - 20);
        if (peer_bytes < 6) {
            printf("Received %d bytes. Tracker returned header metadata but no peers!\n\n", bytes_received);
            continue;
        }

        int peers_found = peer_bytes / 6;
        printf("SUCCESS! Received %d peers data!\n", peers_found);

        // --- Parsing and storing the peers --- //
        unsigned char *peer_data = announce_buffer + 20;

        for (int p = 0; p < peers_found; p++) {
            if (*swarm_count >= 1024) break;

            unsigned char *raw_ip = &peer_data[p * 6];
            uint16_t raw_port = *(uint16_t*)&peer_data[p * 6 + 4];

            struct sockaddr_in peer_addr;
            ZeroMemory(&peer_addr, sizeof(peer_addr));
            peer_addr.sin_family = AF_INET;
            peer_addr.sin_port = raw_port;
            memcpy(&peer_addr.sin_addr, raw_ip, 4);

            int is_duplicate = 0;
            for (int k = 0; k < *swarm_count; k++) {
                if ((*swarm)[k].address.sin_addr.s_addr == peer_addr.sin_addr.s_addr &&
                    (*swarm)[k].address.sin_port == peer_addr.sin_port) {
                        is_duplicate = 1;
                        printf("  [Duplicate Peer]\n");
                        break;
                    }
            }

            if (!is_duplicate) {
                Peer *tmp = realloc(*swarm, (size_t)(*swarm_count + 1) * sizeof(Peer));
                if (tmp == NULL) {
                    printf("Failed to allocate memory for peers\n");
                    break;
                }
                *swarm = tmp;

                (*swarm)[*swarm_count].address = peer_addr;
                (*swarm)[*swarm_count].socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

                (*swarm)[*swarm_count].is_connected = 0;
                (*swarm)[*swarm_count].is_connecting = 0;                
                (*swarm)[*swarm_count].is_dead = 0;

                (*swarm)[*swarm_count].sent_handshake = 0;
                (*swarm)[*swarm_count].sent_interested = 0;

                (*swarm)[*swarm_count].handshake_complete = 0;
                (*swarm)[*swarm_count].has_bitfield = 0;
                (*swarm)[*swarm_count].is_chocking = 1;

                (*swarm)[*swarm_count].message_buffer = calloc(MAX_MESSAGE_BUFFER, sizeof(uint8_t));
                (*swarm)[*swarm_count].message_buffer_size = MAX_MESSAGE_BUFFER;

                u_long nonblocking = 1;
                ioctlsocket((*swarm)[*swarm_count].socket, FIONBIO, &nonblocking); // Set non-blocking mode

                (*swarm_count)++;
                printf("  [Peer Saved] %d.%d.%d.%d:%d\n",
                       (int)raw_ip[0],
                       (int)raw_ip[1],
                       (int)raw_ip[2],
                       (int)raw_ip[3],
                       (int)ntohs(raw_port));
            }

            printf("  -> Total unique peers in swarm so far: %d\n\n", *swarm_count);
        }

        Sleep(200);
    }

    closesocket(TrackerSocket);
    free(trackers);

    return 0;
}