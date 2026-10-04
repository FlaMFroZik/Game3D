#include "player/col.h"

#include "log.h"

#include <inttypes.h>
#include <stdlib.h>
#include <string.h>

int col_init(PlayerCol *col, int capacity) {
    col->slots = calloc((size_t)capacity, sizeof *col->slots);
    col->capacity = (col->slots != NULL) ? capacity : 0;
    col->count = 0;
    return col->slots != NULL;
}

void col_free(PlayerCol *col) {
    free(col->slots);
    col->slots = NULL;
    col->capacity = 0;
    col->count = 0;
}

SrvPlayer *col_find_endpoint(const PlayerCol *col, const SrvEndpoint *endpoint) {
    int i;

    for (i = 0; i < col->capacity; i++) {
        SrvPlayer *player = &col->slots[i];
        if (player->active && srv_endpoint_equal(&player->endpoint, endpoint)) {
            return player;
        }
    }
    return NULL;
}

SrvPlayer *col_find_id(const PlayerCol *col, uint32_t id) {
    int i;

    for (i = 0; i < col->capacity; i++) {
        SrvPlayer *player = &col->slots[i];
        if (player->active && player->id == id) return player;
    }
    return NULL;
}

int col_id_used(const PlayerCol *col, uint32_t id) {
    return col_find_id(col, id) != NULL;
}

uint32_t col_generate_id(const PlayerCol *col) {
    for (;;) {
        /* rand() гарантирует не меньше 15 бит (RAND_MAX >= 32767),
         * поэтому двух вызовов хватает на всё пространство u32 даже
         * на Windows. */
        const uint32_t id = (((uint32_t)rand() << 16)) ^ ((uint32_t)rand());
        if (id != 0 && !col_id_used(col, id)) return id;
    }
}

SrvPlayer *col_add(PlayerCol *col, const SrvEndpoint *endpoint, double now) {
    int i;

    for (i = 0; i < col->capacity; i++) {
        SrvPlayer *player = &col->slots[i];
        if (player->active) continue;

        memset(player, 0, sizeof *player);
        player->endpoint = *endpoint;
        srv_endpoint_format(&player->endpoint, player->endpoint_text,
                            sizeof player->endpoint_text);
        player->joined_at = now;
        player->last_seen = now;
        player->active = 1;
        col->count++;
        return player;
    }
    return NULL;
}

void col_remove(PlayerCol *col, SrvPlayer *player) {
    if (player->active) {
        player->active = 0;
        col->count--;
    }
}

void col_sweep(PlayerCol *col, double now, double timeout) {
    int i;

    for (i = 0; i < col->capacity; i++) {
        SrvPlayer *player = &col->slots[i];
        if (!player->active) continue;
        if (now - player->last_seen <= timeout) continue;

        col_remove(col, player);
        srv_log(SRV_LOG_INFO, "тайм-аут игрока id=%" PRIu32 ": %s молчал "
                "%.0f с (%d/%d)", player->id, player->endpoint_text, timeout,
                col->count, col->capacity);
    }
}
