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
    int peer_count = 0;

    // Think of this as a list of buckets, where each bucket is representative of how many peers posess the
    // pieces that are inside that bucket. This array of arrays is dynamic and will expand on demand.
    long **availability_index;
    availability_index = calloc(AVAILABILITY_INDEX_STARTER_BUCKETS, sizeof(long*));

    for (int i = 0; i < AVAILABILITY_INDEX_STARTER_BUCKETS; i++) {
        availability_index[i] = calloc(AVAILABILITY_INDEX_STARTER_BUCKET_SIZE, sizeof(long*));
    }

    long **piece_map;

    if (torrent_init(&torrent, torrent_path, &piece_map) != 0) return 1;
    if (tracker_collect_peers(&torrent, &peers, &peer_count) != 0) return 1;
    if (peer_connect_all(&peers, peer_count) != 0) return 1;
    if (peer_run_swarm(peers, peer_count, torrent.info_hash, torrent.peer_id) != 0) return 1;


    /*
    // === Talk to the peers === //
    if (process_swarm(peers, peer_count, torrent.info_hash, torrent.peer_id) != 0) {
        printf("Error processing swarm\n");
    }
    */

    return 0;
}