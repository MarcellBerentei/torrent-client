#pragma once

#include <stdint.h>
#include <winsock2.h>
#include <ws2tcpip.h>

#define MAX_MESSAGE_BUFFER 16397

#pragma pack(push, 1)
typedef struct {
    uint64_t connection_id;
    uint32_t action;
    uint32_t transaction_id;
} ConnectRequest;

typedef struct {
    uint32_t action;
    uint32_t transaction_id;
    uint64_t connection_id;
} ConnectResponse;

typedef struct {
    uint64_t connection_id;
    uint32_t action;          // (1 = Announce)
    uint32_t transaction_id;  // A random 4-byte integer
    uint8_t  info_hash[20];
    uint8_t  peer_id[20];     // Unique 20-byte client string (e.g., "-MY0001-123456789012")
    uint64_t downloaded;      // 0 for initial testing
    uint64_t left;            // Set to total torrent size for initial testing
    uint64_t uploaded;        // Set to 0 for initial testing
    uint32_t event;           // 0 = none, 1 = completed, 2 = started, 3 = stopped
    uint32_t ip_address;      // Our IP. Set to 0 to let the tracker automatically detect it
    uint32_t key;             // A random 4-byte string/int used to identify your client session
    int32_t  num_want;        // Number of peers wanted. Set to -1 for the default maximum (50)
    uint16_t port;            // The TCP port number your client listens on for peer connections
} AnnounceRequest;

typedef struct {
    uint8_t pstrlen;           // Length of the protocol string (should be 19 for "BitTorrent protocol")
    char pstr[19];             // The protocol string "BitTorrent protocol"
    uint8_t reserved[8];       // Reserved bytes (should be all zeros)
    uint8_t info_hash[20];     
    uint8_t peer_id[20];      
} HandshakeMessage;

typedef struct {
    uint32_t length;
    uint8_t  message_id;
    uint8_t  payload[];
} PeerMessage;
#pragma pack(pop)

typedef struct {
    struct sockaddr_in address;
    uint8_t peer_id[20];
    SOCKET socket;

    int is_connected;
    int is_connecting;
    int is_dead;

    int sent_handshake;
    int sent_interested;

    int handshake_complete;
    int has_bitfield;
    int is_chocking;

    int bytes_received;
    uint8_t *message_buffer;
    size_t message_buffer_size;
    uint8_t *bitfield;
    size_t bitfield_written;
} Peer;

int process_swarm(Peer *swarm, int swarm_count, const uint8_t info_hash[20], const uint8_t peer_id[20]);
void cleanup_swarm(Peer *swarm, int swarm_count);
void mark_peer_dead(Peer *peer);
