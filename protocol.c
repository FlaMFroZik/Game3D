#include "protocol.h"

#include <string.h>

/* Протокол требует IEEE-754 binary32 и 32-битные u32 — проверим на
 * этапе компиляции, а не на первом же пакете. */
_Static_assert(sizeof(float) == 4, "протокол требует 32-битный float");
_Static_assert(sizeof(uint32_t) == 4, "протокол требует 32-битный uint32_t");

void protocol_write_u32_be(unsigned char *out, uint32_t value) {
    out[0] = (unsigned char)(value >> 24);
    out[1] = (unsigned char)(value >> 16);
    out[2] = (unsigned char)(value >> 8);
    out[3] = (unsigned char)value;
}

uint32_t protocol_read_u32_be(const unsigned char *in) {
    return ((uint32_t)in[0] << 24) | ((uint32_t)in[1] << 16) |
           ((uint32_t)in[2] << 8) | (uint32_t)in[3];
}

void protocol_write_f32_be(unsigned char *out, float value) {
    uint32_t bits;
    memcpy(&bits, &value, sizeof bits);
    protocol_write_u32_be(out, bits);
}

float protocol_read_f32_be(const unsigned char *in) {
    const uint32_t bits = protocol_read_u32_be(in);
    float value;
    memcpy(&value, &bits, sizeof value);
    return value;
}

int protocol_is(const unsigned char *packet, size_t length, unsigned char num) {
    return packet != NULL && length == 1 && packet[0] == num;
}

int protocol_parse_transform(const unsigned char *packet, size_t length,
                             uint32_t *id, float state[6]) {
    int i;

    if (packet == NULL || length != SRV_TRANSFORM_LENGTH ||
        packet[0] != SRV_MSG_TRANSFORM) {
        return 0;
    }

    *id = protocol_read_u32_be(packet + 1);
    for (i = 0; i < 6; i++) {
        state[i] = protocol_read_f32_be(packet + 5 + 4 * (size_t)i);
    }
    return 1;
}

int protocol_parse_players_request(const unsigned char *packet, size_t length,
                                   uint32_t *id) {
    if (packet == NULL || length != 5 || packet[0] != SRV_MSG_PLAYERS) {
        return 0;
    }
    *id = protocol_read_u32_be(packet + 1);
    return 1;
}

size_t protocol_build_info(unsigned char *out, size_t capacity,
                           uint8_t max_players, uint8_t players,
                           uint32_t tick, const char *name, const char *map) {
    const size_t name_length = strlen(name);
    const size_t map_length = strlen(map);
    const size_t total = 7 + name_length + 1 + map_length + 1;
    size_t offset = 0;

    if (out == NULL || capacity < total) return 0;

    out[offset++] = SRV_MSG_INFO;
    out[offset++] = max_players;
    out[offset++] = players;
    protocol_write_u32_be(out + offset, tick);
    offset += 4;
    /* memcpy копирует строки вместе с завершающим нулём. */
    memcpy(out + offset, name, name_length + 1);
    offset += name_length + 1;
    memcpy(out + offset, map, map_length + 1);
    offset += map_length + 1;

    return offset;
}

size_t protocol_build_join_id(unsigned char *out, uint32_t id) {
    out[0] = SRV_MSG_JOIN;
    protocol_write_u32_be(out + 1, id);
    return 5;
}

size_t protocol_build_join_full(unsigned char *out) {
    out[0] = SRV_MSG_JOIN;
    return 1;
}

size_t protocol_build_transform_ack(unsigned char *out) {
    out[0] = SRV_MSG_TRANSFORM;
    return 1;
}

void protocol_players_list_begin(ProtocolPlayersList *list,
                                 unsigned char *buffer, size_t capacity) {
    list->buffer = buffer;
    list->capacity = capacity;
    list->count = 0;
    if (capacity >= 1) {
        buffer[0] = SRV_MSG_PLAYERS;
        list->length = 1;
    } else {
        list->length = 0;
    }
}

int protocol_players_list_add(ProtocolPlayersList *list,
                              const float state[6]) {
    const size_t separator = (list->count > 0) ? 1u : 0u;
    int i;

    if (list->count >= SRV_PLAYERS_PER_PACKET ||
        list->length + separator + 24 > list->capacity) {
        return 0;
    }

    if (separator) {
        list->buffer[list->length++] = 0;
    }
    for (i = 0; i < 6; i++) {
        protocol_write_f32_be(list->buffer + list->length, state[i]);
        list->length += 4;
    }
    list->count++;
    return 1;
}
