#ifndef SRV_PLAYER_COL_H
#define SRV_PLAYER_COL_H

/* ------------------------------------------------------------------
 * Коллекция игроков: активные сессии сервера.
 *
 * Одна сессия = один UDP endpoint. Сессия создаётся успешным [2],
 * живёт до [5], kick или тайм-аута и хранит последнее состояние из
 * [3]. Слоты фиксированной ёмкости (max_players), активность помечена
 * флагом active, поэтому поля только что отключённого игрока ещё
 * доступны для лога.
 * ------------------------------------------------------------------ */

#include <stdint.h>

#include "socket.h"

typedef struct SrvPlayer {
    int active;                          /* слот занят */
    SrvEndpoint endpoint;                /* UDP endpoint сессии */
    char endpoint_text[SRV_ENDPOINT_TEXT_MAX + 1]; /* «хост:порт» для лога */
    uint32_t id;                         /* ID из ответа [2] */
    int has_state;                       /* пришёл хотя бы один [3] */
    float x, y, z;                       /* последняя позиция */
    float look_x, look_y, look_z;        /* последний вектор взгляда */
    double last_seen;                    /* монотонное время [2]/[3]/[4] */
    double joined_at;
} SrvPlayer;

typedef struct {
    SrvPlayer *slots;
    int capacity;   /* == max_players */
    int count;      /* активных слотов */
} PlayerCol;

/* Выделяет таблицу на capacity слотов. Возвращает 1, 0 — не хватило
 * памяти (вызывающий код завершает работу). */
int col_init(PlayerCol *col, int capacity);
void col_free(PlayerCol *col);

SrvPlayer *col_find_endpoint(const PlayerCol *col, const SrvEndpoint *endpoint);
SrvPlayer *col_find_id(const PlayerCol *col, uint32_t id);

int col_id_used(const PlayerCol *col, uint32_t id);

/* Новый случайный u32 ID, которого нет ни у одной активной сессии.
 * Ноль не выдаётся: он удобен как «ID ещё нет». */
uint32_t col_generate_id(const PlayerCol *col);

/* Занимает свободный слот под endpoint (поиском по существующим не
 * занимается — это задача вызывающего). NULL — свободных слотов нет. */
SrvPlayer *col_add(PlayerCol *col, const SrvEndpoint *endpoint, double now);

/* Помечает слот свободным; count уменьшается, поля игрока не стираются. */
void col_remove(PlayerCol *col, SrvPlayer *player);

/* Закрывает сессии, молчавшие дольше timeout секунд, с записью в лог.
 * Вызывается регулярно из главного цикла: UDP не гарантирует доставку
 * пакета выхода [5]. */
void col_sweep(PlayerCol *col, double now, double timeout);

#endif /* SRV_PLAYER_COL_H */
