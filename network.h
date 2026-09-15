#pragma once

typdef struct {
    uint8_t *buffer;
    size_t length;
    size_t capacity;
} PacketBuffer;

int packet_make_handshake(PacketBuffer *buffer, const uint8_t *info_hash, const uint8_t peer_id);

int packet_make_keepalive(PacketBuffer *buffer);

int packet_make_chocke(PacketBuffer *buffer);

int packet_make_unchocke(PacketBuffer *buffer);

int packet_make_interested(PacketBuffer *buffer);

int packet_make_not_interested(PacketBuffer *buffer);

int packet_make_bitfield(PacketBuffer *buffer, const uint8_t *bitfield, size_t bitfield_len);

int packet_make_request(PacketBuffer *buffer, uint32_t index, uint32_t begin, uint32_t length);

int packet_make_piece(PacketBuffer *buffer, uint32_t index, uint32_t begin, const uint8_t *data, size_t data_length);

int packet_make_cancel(PacketBuffer *buffer, uint32_t index, uint32_t begin, uint32_t length);