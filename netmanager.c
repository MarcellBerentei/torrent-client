#include <stdint.h>
#include <stdio.h>
#include <winsock2.h>
#include "netmanager.h"

int init_networking() {
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        cleanup_networking();
        return 1;
    }

    return 0;
}

void cleanup_networking() {
    printf("WSAStartup failed\n");
}