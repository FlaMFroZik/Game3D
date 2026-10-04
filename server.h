#ifndef SRV_SERVER_H
#define SRV_SERVER_H

/* ------------------------------------------------------------------
 * Ядро сервера: состояние и главный цикл.
 *
 * server.c владеет конфигурацией, сокетом, коллекцией игроков и
 * картой геометрии; здесь же обработчики датаграмм [1]..[5] из
 * README.md. Точка входа — main.c, команды stdin — commands.c.
 * ------------------------------------------------------------------ */

#include <stdint.h>

#include "config.h"
#include "map/map.h"
#include "player/col.h"
#include "socket.h"

typedef struct SrvServer {
    SrvConfig config;
    int socket_open;
    SrvSocket socket;
    char bound_endpoint[SRV_ENDPOINT_TEXT_MAX + 1];
    PlayerCol players;      /* активные сессии */
    SrvMap map;             /* кубы .tfm для видимости */
    int map_loaded;
    double started_at;      /* монотонное время запуска */
} SrvServer;

/* Открывает сокет, загружает карту (--map-file) и коллекцию игроков.
 * Возвращает 1 при успехе, 0 при ошибке (сообщение уже в логе;
 * частично созданное освобождает самостоятельно). */
int  server_init(SrvServer *server, const SrvConfig *config);

/* Главный цикл: приём датаграмм, команды stdin, тайм-ауты сессий.
 * Завершается по server_request_stop (Ctrl+C, SIGTERM, команда quit). */
void server_run(SrvServer *server);

void server_free(SrvServer *server);

/* Просит server_run завершиться. Безопасно для вызова из обработчика
 * сигнала: пишет только sig_atomic_t. */
void server_request_stop(void);

/* Монотонные часы сервера (секунды с неопределённой точки отсчёта). */
double server_monotonic_seconds(void);

/* Время работы в тиках по 1/60 c — то, что уходит в ответ [1]. */
uint32_t server_tick(const SrvServer *server);

/* Человекочитаемый аптайм: «5м 3с», «2ч 41м», «3д 4ч». */
void server_uptime_text(const SrvServer *server, char *out, size_t out_size);

#endif /* SRV_SERVER_H */
