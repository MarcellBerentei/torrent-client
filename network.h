#pragma once

typdef struct {
    uint8_t *buffer;
    size_t length;
    size_t capacity;
} PacketBuffer;

int make_handshake_packet(PacketBuffer *buffer, const uint8_t *info_hash, const uint8_t peer_id);

int make_keepalive_packet(PacketBuffer *buffer);

int make_chocke_packet(PacketBuffer *buffer);

int make_unchocke_packet(PacketBuffer *buffer);

int make_interested_packet(PacketBuffer *buffer);

int make_not_interested_packet(PacketBuffer *buffer);

int make_bitfield_packet(PacketBuffer *buffer, const uint8_t *bitfield, size_t bitfield_len);

int make_request_packet(PacketBuffer *buffer, uint32_t index, uint32_t begin, uint32_t length);

int make_piece_packet(PacketBuffer *buffer, uint32_t index, uint32_t begin, const uint8_t *data, size_t data_length);

int make_cancel_packet(PacketBuffer *buffer, uint32_t index, uint32_t begin, uint32_t length);