#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>
#include <winsock2.h>
#include <Ws2tcpip.h>
#include <windows.h>
#include "bencodeparser.h"
#include "sha1.h"

#define CONNECT_MAGIC_CONSTANT 0x41727101980ULL
#define CONNECT_ACTION 0
#define ANNOUNCE_ACTION 1
#define ANNOUNCE_EVENT_STARTED 2
#define DEFAULT_PORT 6881
#define MAX_BUFFER_SIZE 10240
#define MAX_RESULTS 16

extern char* GLOBAL_start_of_info;
extern char* GLOBAL_end_of_info;

#pragma pack(push, 1)
void print_error_and_cleanup(const char *msg, SOCKET sock, struct addrinfo *res) {
    perror(msg);
    if (res) freeaddrinfo(res);
    if (sock != INVALID_SOCKET) closesocket(sock);
}

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
    uint64_t connection_id;   // 8 bytes: from connect response
    uint32_t action;          // 4 bytes: 1 = announce
    uint32_t transaction_id;  // 4 bytes: random ID (match response)

    uint8_t  info_hash[20];   // 20 bytes: SHA-1 hash of info dict
    uint8_t  peer_id[20];     // 20 bytes: your 20-byte peer ID

    uint64_t downloaded;      // 8 bytes: total downloaded
    uint64_t left;            // 8 bytes: bytes left to download
    uint64_t uploaded;        // 8 bytes: total uploaded

    uint32_t event;           // 4 bytes: 0 = none, 1 = completed, 2 = started, 3 = stopped
    uint32_t ip_address;      // 4 bytes: 0 = default (use sender IP)
    uint32_t key;             // 4 bytes: random key (any value)
    uint32_t num_want;        // 4 bytes: -1 for default (0xFFFFFFFF)
    uint16_t port;            // 2 bytes: your listening port
} AnnounceRequest;

typedef struct {
    uint32_t action;          // 4 bytes: Should be 1
    uint32_t transaction_id;  // 4 bytes: Must match original
    uint32_t interval;        // 4 bytes: Seconds to wait before next announce
    uint32_t leechers;        // 4 bytes:
    uint32_t seeders;         // 4 bytes:
} AnnounceResponse;

typedef struct {
    uint32_t ip;    // big-endian IPv4
    uint16_t port;  // big-endian port
} PeerInfo;
#pragma pack(pop)

Bencode* parse_torrent_file(char* path) {
    FILE* file = fopen(path, "rb");
    if (file == NULL) {
        printf("Couldn't open/find the file\n");
        return NULL;
    }

    // Figuring out the size of the file
    fseek(file, 0, SEEK_END);
    size_t fileSize = ftell(file);
    rewind(file);

    // Reading the file into an array
    char *fileBuffer = malloc(fileSize);
    fread(fileBuffer, 1, fileSize, file);
    fclose(file);
    const char *p_buffer = fileBuffer;

    Bencode *b = parse_bencode(&p_buffer);
    if (b) {
        printf("Successfully parsed the torrent file!\n");
    } else {
        printf("Failed to parse Bencode\n");
    }

    return b;
}

int start_networking(void) {
    WSADATA wsaData; // Some weird bloated struct from winsock.

    int startResult;

    startResult = WSAStartup(MAKEWORD(2,2), &wsaData); // The MAKEWORD is a macro from windows.h that makes and INT contain the version number for winsock (2.2)
    if (startResult != 0) {
        printf("WSAStartup failed: %d\n", startResult);
        return 1;
    }

    return 0;
}

SOCKET setup_udp_socket(struct addrinfo **addr_result, char *host, char *port) {
    SOCKET sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock == INVALID_SOCKET) {
        char err_msg[256];
        sprintf(err_msg, "Error at socket(): %ld\n", WSAGetLastError());
        print_error_and_cleanup(err_msg, sock, *addr_result);
    }
    DWORD timeout = 5000; // 5000 milliseconds = 5 seconds

    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof(timeout));
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, (const char*)&timeout, sizeof(timeout));

    struct addrinfo hints = {0};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_protocol = IPPROTO_UDP;

    if (getaddrinfo(host, port, &hints, addr_result) != 0) {
        print_error_and_cleanup("getaddrinfo failed", sock, *addr_result);
        return INVALID_SOCKET;
    }

    return sock;
}

uint32_t generate_transaction_id(void) {
    return (uint32_t)rand();
}

void build_connect_request(ConnectRequest *req, uint32_t transaction_id) {
    req->connection_id = htonll(CONNECT_MAGIC_CONSTANT);
    req->action = htonl(CONNECT_ACTION);
    req->transaction_id = htonl(transaction_id);
}

void build_announce_request(AnnounceRequest *req, uint64_t connection_id,
                            uint32_t transaction_id, uint8_t *info_hash,
                            uint8_t *peer_id, uint64_t left_bytes) {
    memset(req, 0, sizeof(AnnounceRequest));
    req->connection_id = htonll(connection_id);
    req->action = htonl(ANNOUNCE_ACTION);
    req->transaction_id = htonl(transaction_id);
    memcpy(req->info_hash, info_hash, 20);
    memcpy(req->peer_id, peer_id, 20);
    req->downloaded = htonll(0);
    req->left = htonll(left_bytes);
    req->uploaded = htonll(0);
    req->event = htonl(ANNOUNCE_EVENT_STARTED);
    req->ip_address = htonl(0);
    req->key = htonl(rand());
    req->num_want = htonl(-1);
    req->port = htons(DEFAULT_PORT);
}

int bencode_key_equals(Bencode *key, const char *str) {
    if (!key || key->type != BENCODE_STR || !str) return 0;
    return (key->string_val.len == (int)strlen(str)) &&
           (memcmp(key->string_val.str, str, key->string_val.len) == 0);
}

int bencode_collect_all(Bencode *node, const char *key, Bencode **out, int max_out, int *found_count) {
    if (!node || !found_count) return 0;

    if (node->type == BENCODE_DICT) {
        for (int i = 0; i < node->dict_val.count; i++) {
            Bencode *dict_key = node->dict_val.keys[i];
            Bencode *dict_value = node->dict_val.values[i];

            if (bencode_key_equals(dict_key, key)) {
                if (*found_count < max_out) {
                    out[*found_count] = dict_value;
                    (*found_count)++;
                }
            }

            bencode_collect_all(dict_value, key, out, max_out, found_count);
        }
    } else if (node->type == BENCODE_LIST) {
        for (int i = 0; i < node->list_val.count; i++) {
            bencode_collect_all(node->list_val.items[i], key, out, max_out, found_count);
        }
    }
    // no recursion into int or string types

    return *found_count;
}

void bencode_get_value_from_key(Bencode *node, const char *key, Bencode **out) {
    if (!node || *out) return;

    if (node->type == BENCODE_DICT) {
        for (int i = 0; i < node->dict_val.count; i++) {
            Bencode *dict_key = node->dict_val.keys[i];
            Bencode *dict_value = node->dict_val.values[i];

            if (bencode_key_equals(dict_key, key)) {
                    *out = dict_value;
            }

            bencode_get_value_from_key(dict_value, key, out);
        }
    } else if (node->type == BENCODE_LIST) {
        for (int i = 0; i < node->list_val.count; i++) {
            bencode_get_value_from_key(node->list_val.items[i], key, out);
        }
    }
    // no recursion into int or string types
}

void dump_packet_to_file(char *buffer, size_t length, char *name) {
    FILE* file = fopen(name, "wb");
    fwrite(buffer, 1, length, file);
    fclose(file);
}

void add_to_peer_list(PeerInfo **peer_list, size_t *peer_list_length, char* resp, int number_of_peers) {
    size_t current_count = *peer_list_length / sizeof(PeerInfo);

    for (int i = 0; i < number_of_peers; i++) {
        PeerInfo *p = (PeerInfo *)(resp + sizeof(AnnounceResponse) + i * 6);
        int duplicate = 0;

        for (size_t j = 0; j < current_count; j++) {
            if ((*peer_list)[j].ip == p->ip && (*peer_list)[j].port == p->port) {
                duplicate = 1;
                break;
            }
        }

        if (!duplicate) {
            // Add new peer
            *peer_list = realloc(*peer_list, sizeof(PeerInfo) * (current_count + 1));
            (*peer_list)[current_count] = *p;
            current_count++;
        }
    }

    *peer_list_length = current_count * sizeof(PeerInfo);
}

int announce(Bencode *b, char *host, char *port, PeerInfo **peer_list, size_t *peer_list_length) {
    struct addrinfo* results;
    SOCKET udp_socket = setup_udp_socket(&results, host, port);
    if (udp_socket == INVALID_SOCKET) return 1;

    // --- CONNECT REQUEST ---

    // Crafting the packet
    uint32_t connect_tx_id = generate_transaction_id();
    ConnectRequest connect_packet;
    build_connect_request(&connect_packet, connect_tx_id);

    // Send
    int sent_bytes = sendto(
        udp_socket,
        (char*)&connect_packet,
        sizeof(connect_packet),
        0,
        results->ai_addr,
        (int)results->ai_addrlen
    );
    if (sent_bytes == SOCKET_ERROR) {
        char err_msg[256];
        sprintf(err_msg, "Failed to send connect request (%ld)", WSAGetLastError());
        print_error_and_cleanup(err_msg, udp_socket, results);
        return 1;
    }

    // Receive connect response
    char recv_buffer[MAX_BUFFER_SIZE];
    struct sockaddr_in from_addr;
    socklen_t from_len = sizeof(from_addr);
    int recv_bytes = recvfrom(
        udp_socket,
        recv_buffer,
        sizeof(recv_buffer),
        0,
        (struct sockaddr*)&from_addr,
        &from_len
    );
    if (recv_bytes == SOCKET_ERROR || recv_bytes < (int)sizeof(ConnectResponse)) {
        char err_msg[256];
        sprintf(err_msg, "Failed to receive connect response (%ld)", WSAGetLastError());
        print_error_and_cleanup(err_msg, udp_socket, results);
        return 1;
    }

    ConnectResponse* conn_resp = (ConnectResponse*)recv_buffer;
    uint32_t resp_action = ntohl(conn_resp->action);
    uint32_t resp_tx = ntohl(conn_resp->transaction_id);
    if (resp_action != 0 || resp_tx != connect_tx_id || recv_bytes < sizeof(ConnectResponse)) {
        print_error_and_cleanup("Invalid connect response", udp_socket, results);
        return 1;
    }

    uint64_t connection_id = ntohll(conn_resp->connection_id);
    //printf("Connection ID: %016llx\n", (unsigned long long)connection_id);

    // --- ANNOUNCE REQUEST ---

    AnnounceRequest announce_req;

    uint32_t announce_tx_id = rand();

    uint8_t info_hash[20];
    memset(info_hash, 0, 20);
    sha1(GLOBAL_start_of_info, GLOBAL_end_of_info-GLOBAL_start_of_info, info_hash);
    //print_hash(info_hash);

    char peer_id[21];
    snprintf(peer_id, sizeof(peer_id), "-PC0001-%012d", rand() % 1000000000000);

    uint64_t left = 0;
    Bencode *values_found[MAX_RESULTS];
    int count = 0;
    int found = bencode_collect_all(b, "length", values_found, MAX_RESULTS, &count);

    for (int i = 0; i < found; i++) {
        left += values_found[i]->int_val;
    }

    build_announce_request(&announce_req, connection_id, announce_tx_id, info_hash, peer_id, left);
    //dump_packet_to_file((char*)&announce_req, sizeof(announce_req), "announcequery.bin");

    sent_bytes = sendto(
        udp_socket,
        (char*)&announce_req,
        sizeof(announce_req),
        0,
        results->ai_addr,
        (int)results->ai_addrlen
    );
    if (sent_bytes == SOCKET_ERROR) {
        char err_msg[256];
        sprintf(err_msg, "Announce send failed (%ld)", WSAGetLastError());
        print_error_and_cleanup(err_msg, udp_socket, results);
        return 1;
    }

    recv_bytes = recvfrom(
        udp_socket,
        recv_buffer,
        sizeof(recv_buffer),
        0,
        (struct sockaddr*)&from_addr,
        &from_len
    );
    if (recv_bytes == SOCKET_ERROR) {
        char err_msg[256];
        sprintf(err_msg, "Announce receive failed (%ld)", WSAGetLastError());
        print_error_and_cleanup(err_msg, udp_socket, results);
        return 1;
    }

    if (recv_bytes >= (int)sizeof(AnnounceResponse)) {
        AnnounceResponse* resp = (AnnounceResponse*)recv_buffer;

        if (ntohl(resp->action) == 1 && ntohl(resp->transaction_id) == announce_tx_id) {
            printf("Interval: %u\n", ntohl(resp->interval));
            printf("Leechers: %u\n", ntohl(resp->leechers));
            printf("Seeders: %u\n", ntohl(resp->seeders));

            int numPeers = (recv_bytes - sizeof(AnnounceResponse)) / 6;
            printf("Number of peers: %d\n", numPeers);
            add_to_peer_list(peer_list, peer_list_length, recv_buffer, numPeers);
        } else {
            printf("Announce response transaction mismatch\n");
        }
    } else {
        printf("Announce response too short: %d bytes\n", recv_bytes);
    }
    //dump_packet_to_file(recv_buffer, recv_bytes, "announceresponse.bin");


    freeaddrinfo(results);
    closesocket(udp_socket);
}

int main(int argc, char *argv[]) {
    // --- PARSING THE .TORRENT FILE ---
    Bencode *b = parse_torrent_file(argv[1]);

    Bencode *value_found = NULL;
    int number_of_trackers;
    bencode_get_value_from_key(b, "announce-list", &value_found);
    if (value_found == NULL) {
        bencode_get_value_from_key(b, "announce", &value_found);
        number_of_trackers = 1;
    } else {
        number_of_trackers = value_found->list_val.count;
    }
    char *array_of_trackers[number_of_trackers];
    

    if (number_of_trackers == 1) {
        array_of_trackers[0] = value_found->string_val.str;
    } else {
        for (int i = 0; i < number_of_trackers; i++) {
            for (int y = 0; y < value_found->list_val.items[i]->list_val.count; y++) {
                array_of_trackers[i] = value_found->list_val.items[i]->list_val.items[y]->string_val.str;
            }
        }
    }

    char tracker_ips[number_of_trackers][64];
    char tracker_ports[number_of_trackers][64];
    for (int i = 0; i < number_of_trackers; i++) {
        if(strncmp(array_of_trackers[i], "udp", 3)) continue;

        char *port = strchr(array_of_trackers[i]+6, ':')+1;
        char *end_of_port = strchr(array_of_trackers[i]+6, '/');

        if (end_of_port == NULL) {
            strncpy(tracker_ports[i], port, 4);
            tracker_ports[i][4] = '\0';
        } else {
            int length_of_port = end_of_port-port;
            strncpy(tracker_ports[i], port, length_of_port);
            tracker_ports[i][length_of_port] = '\0';
        }

        int lenght_of_host = (port-1)-(array_of_trackers[i]+6);
        strncpy(tracker_ips[i], array_of_trackers[i]+6, lenght_of_host);
        tracker_ips[i][lenght_of_host] = '\0';
    }

    for (int i = 0; i < number_of_trackers; i++) {
        printf("Host: %s, port: %s\n", tracker_ips[i], tracker_ports[i]);
    }

    // --- PREPARING THE UDP CONNECTION ---
    start_networking();
    srand(time(NULL));
    PeerInfo *peer_list = NULL;
    size_t peer_list_length = 0;

    for (int i = 0; i < number_of_trackers; i++) {
        printf("%s\n", tracker_ips[i]);
        announce(b, tracker_ips[i], tracker_ports[i], &peer_list, &peer_list_length);
        printf("\n\n");
    }

    for (int i = 0; i < (peer_list_length / sizeof(PeerInfo)); i++) {
        struct in_addr ip_addr;
        ip_addr.s_addr = peer_list[i].ip;
        printf("Peer ip: %s, port: %hu\n", inet_ntoa(ip_addr), ntohs(peer_list[i].port));
        fflush(stdout);
    }
    
    WSACleanup();
    return 0;
}
