/* select() и monotonic-часы видны при строгом -std=c11 только после
 * feature test macro — до любых системных заголовков. */
#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#  define _POSIX_C_SOURCE 200112L
#endif

#include "server.h"

#include "commands.h"
#include "log.h"
#include "player/rayvisible.h"
#include "protocol.h"

#include <errno.h>
#include <inttypes.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>

#ifndef _WIN32
#  include <sys/select.h>
#  include <time.h>
#endif

/* tick в ответе [1] — время работы сервера в тиках по 1/60 c, как шаг
 * физики клиента. Клиент число только показывает, сервер на него не
 * завязан. */
#define SRV_TICKS_PER_SECOND 60.0

/* Гранулярность главного цикла: между select() проверяем stdin-команды
 * и тайм-ауты сессий. */
#define SRV_SELECT_SECONDS 0.2

/* Сбрасывается обработчиком сигнала; тип sig_atomic_t — единственный
 * способ записи из обработчика без гонок. */
static volatile sig_atomic_t g_stop_requested = 0;

void server_request_stop(void) {
    g_stop_requested = 1;
}

double server_monotonic_seconds(void) {
#ifdef _WIN32
    /* GetTickCount64 переполняется через 49 суток счётчика, но тип
     * ULONGLONG — нет; точности 10-16 мс хватает для тайм-аутов. */
    return (double)GetTickCount64() / 1000.0;
#else
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        return (double)time(NULL);
    }
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1.0e-9;
#endif
}

uint32_t server_tick(const SrvServer *server) {
    return (uint32_t)((server_monotonic_seconds() - server->started_at) *
                      SRV_TICKS_PER_SECOND);
}

void server_uptime_text(const SrvServer *server, char *out, size_t out_size) {
    const long total =
        server_monotonic_seconds() - server->started_at < 0.0
            ? 0
            : (long)(server_monotonic_seconds() - server->started_at + 0.5);
    const long days = total / 86400;
    const long hours = (total % 86400) / 3600;
    const long minutes = (total % 3600) / 60;
    const long seconds = total % 60;

    if (days > 0) {
        snprintf(out, out_size, "%ldд %ldч %ldм", days, hours, minutes);
    } else if (hours > 0) {
        snprintf(out, out_size, "%ldч %ldм %ldс", hours, minutes, seconds);
    } else if (minutes > 0) {
        snprintf(out, out_size, "%ldм %ldс", minutes, seconds);
    } else {
        snprintf(out, out_size, "%ldс", seconds);
    }
}

/* ------------------------------------------------------------------ */
/* Обработка датаграмм                                                */
/* ------------------------------------------------------------------ */

/* [1] — сведения о сервере точного размера. */
static void server_handle_info(SrvServer *server, const unsigned char *packet,
                               size_t length, const SrvEndpoint *from,
                               const char *endpoint_text) {
    unsigned char reply[SRV_PACKET_MAX];
    size_t reply_length;

    if (!protocol_is(packet, length, SRV_MSG_INFO)) {
        srv_log(SRV_LOG_DEBUG, "[1] от %s: лишние байты (%u), пакет пропущен",
                endpoint_text, (unsigned)length);
        return;
    }

    reply_length = protocol_build_info(reply, sizeof reply,
                                       (uint8_t)server->config.max_players,
                                       (uint8_t)server->players.count,
                                       server_tick(server),
                                       server->config.name,
                                       server->config.map);
    if (reply_length == 0) return; /* невозможно при валидных настройках */

    (void)srv_socket_send(server->socket, reply, reply_length, from);
    srv_log(SRV_LOG_DEBUG, "[1] запрос сведений от %s", endpoint_text);
}

/* [2] — вход. При players >= max_players клиенту отдаётся ровно [2]
 * («сервер полон»), иначе [2][id]. Повторный [2] с того же endpoint
 * возвращает уже выданный ID: UDP мог потерять первый ответ. */
static void server_handle_join(SrvServer *server, const unsigned char *packet,
                               size_t length, const SrvEndpoint *from,
                               const char *endpoint_text, double now) {
    unsigned char reply[5];
    size_t reply_length;
    SrvPlayer *player;

    if (!protocol_is(packet, length, SRV_MSG_JOIN)) {
        srv_log(SRV_LOG_DEBUG, "[2] от %s: лишние байты (%u), пакет пропущен",
                endpoint_text, (unsigned)length);
        return;
    }

    player = col_find_endpoint(&server->players, from);
    if (player != NULL) {
        player->last_seen = now;
        reply_length = protocol_build_join_id(reply, player->id);
        (void)srv_socket_send(server->socket, reply, reply_length, from);
        srv_log(SRV_LOG_DEBUG, "[2] повторный запрос от %s: тот же id %"
                PRIu32, endpoint_text, player->id);
        return;
    }

    if (server->players.count >= server->config.max_players) {
        reply_length = protocol_build_join_full(reply);
        (void)srv_socket_send(server->socket, reply, reply_length, from);
        srv_log(SRV_LOG_INFO, "[2] отказано %s: сервер полон (%d/%d)",
                endpoint_text, server->players.count,
                server->config.max_players);
        return;
    }

    player = col_add(&server->players, from, now);
    if (player == NULL) {
        /* Не должно случиться: свободное место проверено выше. Но лучше
         * честный отказ, чем второй игрок в занятом слоте. */
        reply_length = protocol_build_join_full(reply);
        (void)srv_socket_send(server->socket, reply, reply_length, from);
        return;
    }

    player->id = col_generate_id(&server->players);
    reply_length = protocol_build_join_id(reply, player->id);
    (void)srv_socket_send(server->socket, reply, reply_length, from);
    srv_log(SRV_LOG_INFO, "игрок id=%" PRIu32 " подключился: %s (%d/%d)",
            player->id, player->endpoint_text, server->players.count,
            server->config.max_players);
}

/* [3][id][x][y][z][lx][ly][lz] — состояние игрока. ID сверяется с
 * сессией этого endpoint; чужой или потерянный ID игнорируется. */
static void server_handle_transform(SrvServer *server,
                                    const unsigned char *packet, size_t length,
                                    const SrvEndpoint *from,
                                    const char *endpoint_text, double now) {
    unsigned char reply[1];
    const size_t reply_length = protocol_build_transform_ack(reply);
    uint32_t id;
    float state[6];
    SrvPlayer *player;

    if (!protocol_parse_transform(packet, length, &id, state)) {
        srv_log(SRV_LOG_DEBUG, "[3] от %s: неверная длина %u, пакет пропущен",
                endpoint_text, (unsigned)length);
        return;
    }

    player = col_find_endpoint(&server->players, from);
    if (player == NULL) {
        srv_log(SRV_LOG_DEBUG, "[3] от %s без сессии, пакет пропущен",
                endpoint_text);
        return;
    }
    if (id != player->id) {
        srv_log(SRV_LOG_DEBUG, "[3] от %s с чужим id, пакет пропущен",
                endpoint_text);
        return;
    }

    player->x = state[0];
    player->y = state[1];
    player->z = state[2];
    player->look_x = state[3];
    player->look_y = state[4];
    player->look_z = state[5];
    player->has_state = 1;
    player->last_seen = now;

    (void)srv_socket_send(server->socket, reply, reply_length, from);
    srv_log(SRV_LOG_DEBUG, "[3] id=%" PRIu32 " %s: (%.2f, %.2f, %.2f)",
            player->id, player->endpoint_text,
            (double)player->x, (double)player->y, (double)player->z);
}

/* [4][id] — видимые игроки. Ответ собирается из остальных игроков с
 * известным состоянием, до которых дошёл луч (player/rayvisible.c).
 * Про самого спрашивающего не рассказываем: клиент рисует силуэты
 * только чужих игроков. Пустой список — просто [4]. */
static void server_handle_players(SrvServer *server,
                                  const unsigned char *packet, size_t length,
                                  const SrvEndpoint *from,
                                  const char *endpoint_text, double now) {
    unsigned char reply[SRV_PACKET_MAX];
    ProtocolPlayersList list;
    uint32_t id;
    SrvPlayer *player;
    int i;

    if (!protocol_parse_players_request(packet, length, &id)) {
        srv_log(SRV_LOG_DEBUG, "[4] от %s: неверная длина %u, пакет пропущен",
                endpoint_text, (unsigned)length);
        return;
    }

    player = col_find_endpoint(&server->players, from);
    if (player == NULL) {
        srv_log(SRV_LOG_DEBUG, "[4] от %s без сессии, пакет пропущен",
                endpoint_text);
        return;
    }
    if (id != player->id) {
        srv_log(SRV_LOG_DEBUG, "[4] от %s с чужим id, пакет пропущен",
                endpoint_text);
        return;
    }

    player->last_seen = now;

    protocol_players_list_begin(&list, reply, sizeof reply);
    for (i = 0; i < server->players.capacity; i++) {
        const SrvPlayer *other = &server->players.slots[i];
        float state[6];

        if (!other->active || other == player || !other->has_state) continue;
        if (!ray_player_visible(&server->map,
                                player->x, player->y, player->z,
                                other->x, other->y, other->z)) {
            continue;
        }

        state[0] = other->x;
        state[1] = other->y;
        state[2] = other->z;
        state[3] = other->look_x;
        state[4] = other->look_y;
        state[5] = other->look_z;
        if (!protocol_players_list_add(&list, state)) break;
    }

    (void)srv_socket_send(server->socket, reply, list.length, from);
    srv_log(SRV_LOG_DEBUG, "[4] id=%" PRIu32 " %s: %d видимых",
            player->id, player->endpoint_text, list.count);
}

/* [5] — выход: завершает сессию по endpoint, без аргументов и ответа. */
static void server_handle_leave(SrvServer *server, const unsigned char *packet,
                                size_t length, const SrvEndpoint *from,
                                const char *endpoint_text) {
    SrvPlayer *player;

    if (!protocol_is(packet, length, SRV_MSG_LEAVE)) {
        srv_log(SRV_LOG_DEBUG, "[5] от %s: лишние байты (%u), пакет пропущен",
                endpoint_text, (unsigned)length);
        return;
    }

    player = col_find_endpoint(&server->players, from);
    if (player == NULL) {
        srv_log(SRV_LOG_DEBUG, "[5] от %s без сессии", endpoint_text);
        return;
    }

    /* Поля игрока не стираются: id и адрес ещё нужны для записи в лог. */
    col_remove(&server->players, player);
    srv_log(SRV_LOG_INFO, "игрок id=%" PRIu32 " отключился: %s (%d/%d)",
            player->id, player->endpoint_text, server->players.count,
            server->config.max_players);
}

static void server_handle_datagram(SrvServer *server,
                                   const unsigned char *packet, size_t length,
                                   const SrvEndpoint *from, double now) {
    char endpoint_text[SRV_ENDPOINT_TEXT_MAX + 1];

    if (length == 0) return;

    srv_endpoint_format(from, endpoint_text, sizeof endpoint_text);

    switch (packet[0]) {
        case SRV_MSG_INFO:
            server_handle_info(server, packet, length, from, endpoint_text);
            break;
        case SRV_MSG_JOIN:
            server_handle_join(server, packet, length, from, endpoint_text,
                               now);
            break;
        case SRV_MSG_TRANSFORM:
            server_handle_transform(server, packet, length, from,
                                    endpoint_text, now);
            break;
        case SRV_MSG_PLAYERS:
            server_handle_players(server, packet, length, from, endpoint_text,
                                  now);
            break;
        case SRV_MSG_LEAVE:
            server_handle_leave(server, packet, length, from, endpoint_text);
            break;
        default:
            /* Случайный UDP-шум или пакеты будущих версий протокола. */
            srv_log(SRV_LOG_DEBUG, "датаграмма [%u] от %s: неизвестный номер, "
                    "игнорируется", (unsigned)packet[0], endpoint_text);
            break;
    }
}

/* Принимает всё, что накопилось в сокете, до первого «пакетов нет». */
static void server_receive_datagrams(SrvServer *server) {
    unsigned char packet[SRV_PACKET_MAX];

    for (;;) {
        SrvEndpoint from;
        const double now = server_monotonic_seconds();
        const int received = srv_socket_receive(server->socket, packet,
                                                sizeof packet, &from);
        if (received <= 0) break;

        server_handle_datagram(server, packet, (size_t)received, &from, now);
    }
}

/* ------------------------------------------------------------------ */
/* Жизненный цикл                                                     */
/* ------------------------------------------------------------------ */

int server_init(SrvServer *server, const SrvConfig *config) {
    memset(server, 0, sizeof *server);
    server->socket = SRV_SOCKET_INVALID;
    server->config = *config;

    if (!srv_socket_open(&server->socket, server->config.bind_address,
                         server->config.port, server->bound_endpoint,
                         sizeof server->bound_endpoint)) {
        return 0;
    }
    server->socket_open = 1;

    if (server->config.map_file[0] != '\0') {
        if (!srv_map_load(&server->map, server->config.map_file)) {
            server_free(server);
            return 0;
        }
        server->map_loaded = 1;
    }

    if (!col_init(&server->players, server->config.max_players)) {
        srv_log(SRV_LOG_ERROR, "не хватило памяти под таблицу игроков");
        server_free(server);
        return 0;
    }

    server->started_at = server_monotonic_seconds();
    return 1;
}

void server_free(SrvServer *server) {
    if (server->socket_open) {
        srv_socket_close(server->socket);
        server->socket = SRV_SOCKET_INVALID;
        server->socket_open = 0;
    }
    col_free(&server->players);
    srv_map_free(&server->map);
    server->map_loaded = 0;
}

void server_run(SrvServer *server) {
    srv_log(SRV_LOG_INFO, "сервер запущен: %s, имя «%s», карта %s, до %d "
            "игроков, тайм-аут %.0f с",
            server->bound_endpoint, server->config.name,
            server->config.map[0] != '\0'
                ? server->config.map
                : "(клиент оставляет свою)",
            server->config.max_players, server->config.timeout_seconds);
    if (server->map_loaded) {
        srv_log(SRV_LOG_INFO, "геометрия карты: %s (%zu кубов), видимость "
                "игроков по лучу", server->config.map_file,
                server->map.count);
    }
    srv_log(SRV_LOG_INFO, "команды: help, status, list, kick <id>, quit");

    while (!g_stop_requested) {
        struct timeval timeout;
        fd_set read_set;
        int ready;

        FD_ZERO(&read_set);
        FD_SET(server->socket, &read_set);
        timeout.tv_sec = 0;
        timeout.tv_usec = (long)(SRV_SELECT_SECONDS * 1.0e6);

#ifdef _WIN32
        ready = select(0, &read_set, NULL, NULL, &timeout);
#else
        ready = select(server->socket + 1, &read_set, NULL, NULL, &timeout);
#endif
        if (ready < 0) {
            if (errno == EINTR) continue;
            srv_log(SRV_LOG_ERROR, "select: %s", strerror(errno));
            break;
        }

        if (ready > 0) {
            server_receive_datagrams(server);
        }

        /* Между ожиданиями датаграмм: команды со stdin и тайм-ауты. */
        commands_poll(server);
        col_sweep(&server->players, server_monotonic_seconds(),
                  server->config.timeout_seconds);
    }

    {
        char uptime[64];
        server_uptime_text(server, uptime, sizeof uptime);
        srv_log(SRV_LOG_INFO, "сервер остановлен (аптайм %s)", uptime);
    }
}
