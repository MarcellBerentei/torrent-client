#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include "peer.h"

#define POLL_TIMEOUT 50

static int send_handshake(PeerConnection *peer, const uint8_t info_hash[20], const uint8_t peer_id[20]) {
    HandshakeMessage handshake;
    handshake.pstrlen = 19;
    memcpy(handshake.pstr, "BitTorrent protocol", 19);
    memset(handshake.reserved, 0, sizeof(handshake.reserved));
    memcpy(handshake.info_hash, info_hash, 20);
    memcpy(handshake.peer_id, peer_id, 20);

    int send_result = send(peer->socket, (char *)&handshake, sizeof(handshake), 0);
    if (send_result == SOCKET_ERROR) {
        printf("Sending of handshake failed.");
        return 1;
    }

    peer->state = PEER_HANDSHAKING;
    return 0;
}

void mark_peer_dead(PeerConnection *peer) {
    if (peer->state == PEER_DEAD) {
        return;
    }

    peer->state = PEER_DEAD;
    if (peer->socket != INVALID_SOCKET) {
        closesocket(peer->socket);
        peer->socket = INVALID_SOCKET;
    }
}

// Function to clean up the swarm and free resources
void cleanup_swarm(PeerConnection *swarm, int swarm_count) {
    if (swarm == NULL) {
        return;
    }

    for (int i = 0; i < swarm_count; i++) {
        PeerConnection *peer = &swarm[i];
        
        free(peer->rx_buffer);
        free(peer->bitfield);
        peer->rx_buffer = NULL;
        peer->bitfield = NULL;
    }
}

static int ensure_message_buffer(PeerConnection *peer, size_t desired_size) {
    if (peer->rx_capacity >= desired_size) {
        return 0;
    }

    uint8_t *expanded_buffer = realloc(peer->rx_buffer, desired_size);
    if (expanded_buffer == NULL) {
        printf("Failed to expand a peers buffer.");
        return 1;
    }

    peer->rx_buffer = expanded_buffer;
    peer->rx_capacity = desired_size;
    return 0;
}

static int process_peer_handshake(PeerConnection *peer, const uint8_t info_hash[20]) {
    // Ensure the message buffer is large enough to hold the handshake
    int bytes_to_read = 68 - peer->rx_used;
    int received = recv(peer->socket, (char *)peer->rx_buffer + peer->rx_used, bytes_to_read, 0);
    if (received > 0) {
        peer->rx_used += received;
        if (peer->rx_used < 68) {
            return 0;
        }

        // Now we have the full handshake message, let's verify it
        HandshakeMessage response;
        memcpy(&response, peer->rx_buffer, sizeof(response));

        if
        (
            response.pstrlen != 19 ||
            memcmp(response.pstr, "BitTorrent protocol", 19) != 0 ||
            memcmp(response.info_hash, info_hash, 20) != 0
        )
        {
            printf("Peer | handshake verification failed\n");
            mark_peer_dead(peer);
            return -1;
        }

        printf("Peer | handshake verified\n");
        peer->state = PEER_ESTABLISHED;
        peer->rx_used = 0;
        return 1;
    }

    if (received == 0) {
        printf("Peer | disconnected during handshake\n");
        mark_peer_dead(peer);
        return -1;
    }

    int error = WSAGetLastError();
    if (error != WSAEWOULDBLOCK) {
        printf("Peer | recv() failed during handshake: %d\n", error);
        mark_peer_dead(peer);
        return -1;
    }

    return 0;
}

static int process_peer_message(PeerConnection *peer) {
    uint8_t *buffer = peer->rx_buffer;
    if (peer->rx_used < 4) {
        return 0;
    }

    uint32_t length = 0;
    memcpy(&length, buffer, 4);
    length = ntohl(length);

    if (length == 0) {
        peer->rx_used = 0;
        return 0;
    }

    uint32_t total_bytes = length + 4;
    // Ensure the message buffer is large enough to hold the entire message
    if (ensure_message_buffer(peer, total_bytes)) {
        printf("Peer | failed to allocate message buffer (%u bytes)\n", total_bytes);
        mark_peer_dead(peer);
        return 1;
    }

    // The whole message didn't arrive just yet
    if (peer->rx_used < total_bytes) {
        return 0;
    }

    // Process the message based on its ID
    uint8_t message_id = buffer[4];

    switch (message_id) {

        case 5: // Bitfield
            uint32_t bitfield_length = length - 1; // Subtract 1 for the message ID byte
            if (peer->bitfield == NULL) { // Allocate the bitfield buffer if it hasn't been allocated yet (What happens if the peer sends a new bitfield message?)
                peer->bitfield = calloc(bitfield_length, 1);
                if (peer->bitfield == NULL) {
                    printf("Peer | failed to allocate bitfield buffer\n");
                    mark_peer_dead(peer);
                    return -1;
                }
            }

            // Copy the bitfield data from the message buffer to the peer's bitfield
            memcpy(peer->bitfield, &buffer[5], bitfield_length);
            printf("Peer | full bitfield parsed (%u bytes)\n", bitfield_length);

            // If the message buffer is larger than MAX_MESSAGE_BUFFER, we should shrink it back down to avoid excessive memory usage
            if (peer->rx_capacity > MAX_MESSAGE_BUFFER) {
                uint8_t *normal_buffer = realloc(peer->rx_buffer, MAX_MESSAGE_BUFFER);
                if (normal_buffer != NULL) {
                    peer->rx_buffer = normal_buffer;
                    peer->rx_capacity = MAX_MESSAGE_BUFFER;
                }
            }

            // After receiving the bitfield, we can send an "interested" message to the peer if we haven't already
            if (peer->am_interested == 0) {
                PeerMessage interested;
                interested.length = htonl(1);
                interested.message_id = 2;

                int send_result = send(peer->socket, (char *)&interested, sizeof(interested), 0);
                if (send_result == SOCKET_ERROR) {
                    printf("Peer | failed to send interested message: %d\n", WSAGetLastError());
                    mark_peer_dead(peer);
                    return -1;
                }

                peer->am_interested = 1;
                printf("Peer | sent interested message\n");
            }
            break;

        case 1:
            printf("Peer | received unchoke message\n");
            peer->am_choked = 0;
            break;

        default:
            printf("Peer | received message id %u\n", message_id);
            break;
    }


    // Shift the remaining data in the message buffer to the front
    if (peer->rx_used > (int)total_bytes) {
        int remaining = peer->rx_used - total_bytes;
        memmove(peer->rx_buffer, peer->rx_buffer + total_bytes, remaining);
        peer->rx_used = remaining;
    } else {
        peer->rx_used = 0;
    }

    return 1;
}

static int receive_peer_data(PeerConnection *peer) {
    // Ensure there's enough space in the message buffer
    size_t available = peer->rx_capacity - peer->rx_used;
    if (available == 0) {
        // Expand the buffer if it's full (This feels really dangerous, will need to make sure this doesn't cause issues)
        if (!ensure_message_buffer(peer, peer->rx_capacity + MAX_MESSAGE_BUFFER)) {
            printf("Peer | unable to expand receive buffer\n");
            mark_peer_dead(peer);
            return -1;
        }
        available = peer->rx_capacity - peer->rx_used;
    }

    // Receive data from the peer
    int bytes = recv(peer->socket, (char*)peer->rx_buffer + peer->rx_used, (int)available, 0);
    if (bytes > 0) {
        peer->rx_used += bytes;
        return 1;
    }

    // If recv returns 0, it means the peer has closed the connection
    if (bytes == 0) {
        printf("Peer | disconnected\n");
        mark_peer_dead(peer);
        return -1;
    }

    // If recv returns SOCKET_ERROR, we need to check the error code
    int error = WSAGetLastError();
    if (error != WSAEWOULDBLOCK) {
        printf("Peer | recv() error: %d\n", error);
        mark_peer_dead(peer);
        return -1;
    }

    return 0;
}

int peer_run_swarm(PeerConnection *swarm, int swarm_count, const uint8_t info_hash[20], const uint8_t peer_id[20]) {
    if (swarm == NULL || swarm_count <= 0) {
        printf("Swarm didn't, or no longer exists.");
        return 1;
    }

    // Allocate an array of WSAPOLLFD structures for polling the swarm
    WSAPOLLFD *pollfds = calloc(swarm_count, sizeof(WSAPOLLFD));
    if (pollfds == NULL) {
        printf("Failed to allocate poll array\n");
        return 1;
    }

    // Initialize the poll array with the swarm's sockets
    for (int i = 0; i < swarm_count; i++) {
        pollfds[i].fd = swarm[i].socket;
    }

    // Main loop to process the swarm
    int active_connections = 0;
    while (1) {
        active_connections = 0;
        for (int i = 0; i < swarm_count; i++) {
            // Skip dead peers
            PeerConnection *peer = &swarm[i];
            if (peer->state == PEER_DEAD) {
                pollfds[i].events = 0;
                continue;
            }

            active_connections++;
            
            // If the peer is connecting, we want to check for writability (POLLOUT) to know when the connection is established.
            // Otherwise, we check for readability (POLLIN) to receive data.
            if (peer->state == PEER_CONNECTING) {
                pollfds[i].events = POLLOUT;
            } else {
                pollfds[i].events = POLLIN;
            }
            
        }
        if (active_connections == 0) break;

        // Poll the sockets with a timeout of 100 milliseconds
        int poll_result = WSAPoll(pollfds, swarm_count, POLL_TIMEOUT);
        if (poll_result == SOCKET_ERROR) {
            printf("WSAPoll failed with error: %d\n", WSAGetLastError());
            break;
        }
        // If no sockets are ready, continue to the next iteration
        if (poll_result == 0) continue;


        // Process each peer based on the poll results
        for (int i = 0; i < swarm_count; i++) {
            PeerConnection *peer = &swarm[i];
            if (peer->state == PEER_DEAD || pollfds[i].revents == 0) {
                continue;
            }

            // Handle errors and disconnections
            if (pollfds[i].revents & (POLLERR | POLLHUP | POLLNVAL)) {
                printf("Peer %d | disconnected or errored\n", i);
                mark_peer_dead(peer);
                continue;
            }

            // If the peer is connecting and the socket is writable, the connection has been established.
            if (peer->state == PEER_CONNECTING && (pollfds[i].revents & POLLOUT)) {
                // Check for socket errors after connect
                int socket_error = 0;
                int opt_len = sizeof(socket_error);
                getsockopt(peer->socket, SOL_SOCKET, SO_ERROR, (char *)&socket_error, &opt_len);
                if (socket_error != 0) {
                    printf("Peer %d | connect failed %d\n", i, socket_error);
                    mark_peer_dead(peer);
                    continue;
                }

                peer->state = PEER_CONNECTED;
                printf("Peer %d | connected\n", i);

                // This was wrapped in an if statement, but I don't think it was really needed
                if (send_handshake(peer, info_hash, peer_id) != 0) {
                    printf("Peer %d | handshake send failed\n", i);
                    mark_peer_dead(peer);
                    continue;
                }
            
            }

            // If the peer is connected and the socket is readable, we can receive data.
            if (peer->state >= PEER_CONNECTED && (pollfds[i].revents & POLLIN)) {
                if (peer->state == PEER_HANDSHAKING) {
                    int result = process_peer_handshake(peer, info_hash);
                    if (result < 0) {
                        continue;
                    }
                } else {
                    // Process regular peer messages
                    int recv_result = receive_peer_data(peer);
                    if (recv_result < 0) {
                        continue;
                    }

                    // If we have enough data to process a message, do so
                    if (peer->rx_used >= 4) {
                        int decode_result = process_peer_message(peer);
                        if (decode_result < 0) {
                            continue;
                        }
                    }
                }
            }
        }
    }

    free(pollfds);
    return 0;
}

int peer_connect_all(PeerConnection **peers, int peer_count) {
    // === Connect Sockets === //
    for (int p = 0; p < peer_count; p++) {
        connect((*peers)[p].socket, (struct sockaddr*)(&(*peers)[p].address), sizeof((*peers)[p].address));
        (*peers)[p].state = PEER_CONNECTING;
    }

    return 0;
}