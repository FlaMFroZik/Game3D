/* Автономный CLI-сервер многопользовательского режима Game3D.
 *
 * Протокол зафиксирован в README.md и реализован ровно так, как там
 * написано: каждая датаграмма начинается с однобайтового номера [num],
 * числа u32 и f32 записываются big-endian (IEEE-754 binary32), строки
 * завершаются нулём. Сервер не считает физику и не знает карты: он
 * принимает состояния игроков из [3] и раздаёт их остальным через [4],
 * следя за сессиями по UDP endpoint.
 *
 * Управление полностью из командной строки: настройка — флагами при
 * запуске, события — логом в stdout, команды (help, status, list, kick,
 * quit) читаются со stdin. Графических зависимостей нет, поэтому сервер
 * собирается и работает на выделенной машине без OpenGL.
 */

#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#  define _POSIX_C_SOURCE 200112L
#endif

#include <errno.h>
#include <inttypes.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#  include <process.h>
#  include <winsock2.h>
#  include <ws2tcpip.h>
#else
#  include <fcntl.h>
#  include <netdb.h>
#  include <pthread.h>
#  include <sys/select.h>
#  include <sys/socket.h>
#  include <sys/types.h>
#  include <unistd.h>
#endif

/* Протокол требует IEEE-754 binary32 и 32-битные u32 — проверим на этапе
 * компиляции, а не на первом же пакете. */
_Static_assert(sizeof(float) == 4, "протокол требует 32-битный float");
_Static_assert(sizeof(uint32_t) == 4, "протокол требует 32-битный uint32_t");

/* ------------------------------------------------------------------ */
/* Константы                                                          */
/* ------------------------------------------------------------------ */

#define SRV_VERSION                 "1.0"

#define SRV_DEFAULT_PORT            27015
#define SRV_DEFAULT_NAME            "Game3D Server"
#define SRV_DEFAULT_MAX_PLAYERS     16
#define SRV_DEFAULT_TIMEOUT_SECONDS 30.0

/* Ограничения клиента Game3D (протокол — в README.md). Сервер может
 * отвечать что угодно, но если он укладывается в них сам, его ответы [1]
 * всегда проходят строгую проверку клиента и попадают в список серверов. */
#define SRV_NAME_MAX                63    /* предел имени у клиента, байт */
#define SRV_MAP_MAX                 255   /* предел имени карты у клиента, байт */
#define SRV_MAX_PLAYERS_LIMIT       64    /* сколько записей клиента хватает в [4] */

#define SRV_PACKET_MAX              2048  /* как буфер приёма у клиента */
#define SRV_BIND_MAX                255   /* адрес/hostname для --bind */
#define SRV_ENDPOINT_MAX            95    /* «[IPv6%zone]:порт» с запасом */

/* Ответ [4]: 1 байт заголовка, затем записи по 24 байта, разделённые [0].
 * 58 записей (1 + 24*58 + 57 = 1450 байт) не выходят за типичный MTU 1500,
 * чтобы список игроков не резался на пути к клиенту. */
#define SRV_PLAYERS_PER_PACKET      58

/* tick в ответе [1] — время работы сервера в тиках по 1/60 c, как шаг
 * физики клиента. Клиент число только показывает, сервер на него не
 * завязан. */
#define SRV_TICKS_PER_SECOND        60.0

/* Гранулярность главного цикла: между select() проверяем stdin-команды
 * и тайм-ауты сессий. */
#define SRV_SELECT_SECONDS          0.2

/* Номера датаграмм протокола; таблица — в README.md. */
#define SRV_MSG_INFO       1u
#define SRV_MSG_JOIN       2u
#define SRV_MSG_TRANSFORM  3u
#define SRV_MSG_PLAYERS    4u
#define SRV_MSG_LEAVE      5u

/* [3][id:u32][x][y][z][lx][ly][lz] — 1 + 4 + 6*4 байт. */
#define SRV_TRANSFORM_LENGTH        29

/* ------------------------------------------------------------------ */
/* Типы                                                               */
/* ------------------------------------------------------------------ */

#ifdef _WIN32
typedef SOCKET SrvSocket;
#  define SRV_SOCKET_INVALID INVALID_SOCKET
#else
typedef int SrvSocket;
#  define SRV_SOCKET_INVALID (-1)
#endif

typedef struct {
    int active;                          /* слот занят */
    struct sockaddr_storage addr;        /* UDP endpoint сессии */
    socklen_t addr_len;
    uint32_t id;                         /* ID из ответа [2] */
    int has_state;                       /* пришёл хотя бы один [3] */
    float x, y, z;                       /* последняя позиция */
    float look_x, look_y, look_z;        /* последний вектор взгляда */
    double last_seen;                    /* монотонное время [2]/[3]/[4] */
    double joined_at;
    char endpoint[SRV_ENDPOINT_MAX + 1]; /* «хост:порт» для лога */
} SrvSession;

typedef struct {
    char bind_address[SRV_BIND_MAX + 1]; /* как задано через --bind */
    char bound_endpoint[SRV_ENDPOINT_MAX + 1]; /* числовой адрес после bind */
    int port;
    char name[SRV_NAME_MAX + 1];
    char map[SRV_MAP_MAX + 1];
    int max_players;
    double timeout_seconds;

    SrvSocket socket;
    int socket_open;
    SrvSession *sessions;                /* max_players слотов */
    int session_count;
    double started_at;                   /* монотонное время запуска */
} SrvServer;

typedef struct {
    const char *bind_address;            /* NULL — по умолчанию 0.0.0.0 */
    const char *name;
    const char *map;
    int port;
    int max_players;
    double timeout_seconds;
    int verbose;
    int quiet;
} SrvConfig;

/* ------------------------------------------------------------------ */
/* Глобальное состояние                                               */
/* ------------------------------------------------------------------ */

/* Уровень логов: ошибки печатаются всегда, обычные события скрываются
 * флагом --quiet, каждая датаграмма — только с --verbose. */
enum {
    SRV_LOG_ERROR = 0,
    SRV_LOG_INFO  = 1,
    SRV_LOG_DEBUG = 2
};

static int g_log_level = SRV_LOG_INFO;

/* Сбрасывается обработчиком сигнала; тип sig_atomic_t — единственный
 * способ записи из обработчика без гонок. */
static volatile sig_atomic_t g_running = 1;

#ifdef _WIN32
static int g_winsock_started = 0;
#endif

/* ---------- Очередь команд из stdin ---------- */

#define SRV_COMMAND_MAX    255
#define SRV_COMMAND_QUEUE  16

/* Поток чтения stdin единственный пишет в очередь, главный цикл —
 * единственный читает; мьютекс защищает и то, и другое. */
static char g_commands[SRV_COMMAND_QUEUE][SRV_COMMAND_MAX + 1];
static int g_command_count = 0;
static int g_stdin_closed = 0;

#ifdef _WIN32
static CRITICAL_SECTION g_command_lock;
#else
static pthread_mutex_t g_command_lock = PTHREAD_MUTEX_INITIALIZER;
#endif

/* ------------------------------------------------------------------ */
/* Время, лог, байты                                                   */
/* ------------------------------------------------------------------ */

static double srv_monotonic_seconds(void) {
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

static uint32_t srv_current_tick(const SrvServer *server) {
    return (uint32_t)((srv_monotonic_seconds() - server->started_at) *
                      SRV_TICKS_PER_SECOND);
}

static void srv_log(int level, const char *format, ...) {
    time_t wall;
    const struct tm *local;
    char stamp[16];
    va_list args;

    if (g_log_level < level) return;

    wall = time(NULL);
    local = localtime(&wall);
    if (local != NULL && strftime(stamp, sizeof stamp, "%H:%M:%S", local) > 0) {
        printf("[%s] ", stamp);
    } else {
        printf("[--:--:--] ");
    }

    va_start(args, format);
    vprintf(format, args);
    va_end(args);
    putchar('\n');
    fflush(stdout);
}

static void srv_write_u32_be(unsigned char *out, uint32_t value) {
    out[0] = (unsigned char)(value >> 24);
    out[1] = (unsigned char)(value >> 16);
    out[2] = (unsigned char)(value >> 8);
    out[3] = (unsigned char)value;
}

static uint32_t srv_read_u32_be(const unsigned char *in) {
    return ((uint32_t)in[0] << 24) | ((uint32_t)in[1] << 16) |
           ((uint32_t)in[2] << 8) | (uint32_t)in[3];
}

static void srv_write_f32_be(unsigned char *out, float value) {
    uint32_t bits;
    memcpy(&bits, &value, sizeof bits);
    srv_write_u32_be(out, bits);
}

static float srv_read_f32_be(const unsigned char *in) {
    const uint32_t bits = srv_read_u32_be(in);
    float value;
    memcpy(&value, &bits, sizeof value);
    return value;
}

static void srv_format_endpoint(const struct sockaddr *addr, socklen_t addr_len,
                                char *out, size_t out_size) {
    /* NI_NUMERICHOST|NI_NUMERICSERV не ходят в DNS, поэтому цифровым
     * адресам хватит и таких буферов: 45 байт — максимум текстового IPv6. */
    char host[64];
    char service[8];

    if (getnameinfo(addr, addr_len, host, sizeof host, service, sizeof service,
                    NI_NUMERICHOST | NI_NUMERICSERV) == 0) {
        if (addr->sa_family == AF_INET6) {
            /* IPv6-адрес сам содержит ':', поэтому берём его в скобки. */
            snprintf(out, out_size, "[%s]:%s", host, service);
        } else {
            snprintf(out, out_size, "%s:%s", host, service);
        }
    } else {
        snprintf(out, out_size, "неизвестный адрес");
    }
}

/* ------------------------------------------------------------------ */
/* Сессии                                                             */
/* ------------------------------------------------------------------ */

static int srv_same_endpoint(const struct sockaddr_storage *a, socklen_t a_len,
                             const struct sockaddr_storage *b, socklen_t b_len) {
    if (a_len != b_len || a->ss_family != b->ss_family) return 0;

    if (a->ss_family == AF_INET) {
        const struct sockaddr_in *x = (const struct sockaddr_in *)a;
        const struct sockaddr_in *y = (const struct sockaddr_in *)b;
        return x->sin_port == y->sin_port &&
               x->sin_addr.s_addr == y->sin_addr.s_addr;
    }
    if (a->ss_family == AF_INET6) {
        const struct sockaddr_in6 *x = (const struct sockaddr_in6 *)a;
        const struct sockaddr_in6 *y = (const struct sockaddr_in6 *)b;
        return x->sin6_port == y->sin6_port &&
               x->sin6_scope_id == y->sin6_scope_id &&
               memcmp(&x->sin6_addr, &y->sin6_addr, sizeof x->sin6_addr) == 0;
    }
    return 0;
}

static SrvSession *srv_find_session(SrvServer *server,
                                    const struct sockaddr_storage *addr,
                                    socklen_t addr_len) {
    int i;
    for (i = 0; i < server->max_players; i++) {
        SrvSession *session = &server->sessions[i];
        if (session->active &&
            srv_same_endpoint(&session->addr, session->addr_len, addr, addr_len)) {
            return session;
        }
    }
    return NULL;
}

static SrvSession *srv_find_session_by_id(SrvServer *server, uint32_t id) {
    int i;
    for (i = 0; i < server->max_players; i++) {
        SrvSession *session = &server->sessions[i];
        if (session->active && session->id == id) return session;
    }
    return NULL;
}

static SrvSession *srv_create_session(SrvServer *server,
                                      const struct sockaddr_storage *addr,
                                      socklen_t addr_len) {
    int i;
    for (i = 0; i < server->max_players; i++) {
        SrvSession *session = &server->sessions[i];
        if (session->active) continue;

        memset(session, 0, sizeof *session);
        session->addr = *addr;
        session->addr_len = addr_len;
        srv_format_endpoint((const struct sockaddr *)&session->addr,
                            session->addr_len,
                            session->endpoint, sizeof session->endpoint);
        session->active = 1;
        server->session_count++;
        return session;
    }
    return NULL;
}

/* Слот помечается свободным, но поля не затираются: вызывающий код ещё
 * пишет id и endpoint в лог после отключения. */
static void srv_drop_session(SrvServer *server, SrvSession *session) {
    if (session->active) {
        session->active = 0;
        server->session_count--;
    }
}

static int srv_id_in_use(const SrvServer *server, uint32_t id) {
    int i;
    for (i = 0; i < server->max_players; i++) {
        const SrvSession *session = &server->sessions[i];
        if (session->active && session->id == id) return 1;
    }
    return 0;
}

/* rand() гарантирует не меньше 15 бит (RAND_MAX >= 32767), поэтому двух
 * вызовов хватает на всё пространство u32 даже на Windows. */
static uint32_t srv_generate_id(const SrvServer *server) {
    for (;;) {
        const uint32_t id = (((uint32_t)rand() << 16)) ^ ((uint32_t)rand());
        /* Ноль не выдаём: он удобен как «ID ещё нет» и в чужих реализациях. */
        if (id != 0 && !srv_id_in_use(server, id)) return id;
    }
}

static unsigned srv_random_seed(void) {
#ifdef _WIN32
    const unsigned pid = (unsigned)_getpid();
#else
    const unsigned pid = (unsigned)getpid();
#endif
    return (unsigned)time(NULL) ^ (pid << 16) ^ (unsigned)clock();
}

/* ------------------------------------------------------------------ */
/* Сокет                                                              */
/* ------------------------------------------------------------------ */

static void srv_close_socket_raw(SrvSocket socket_handle) {
#ifdef _WIN32
    closesocket(socket_handle);
#else
    close(socket_handle);
#endif
}

static int srv_set_nonblocking(SrvSocket socket_handle) {
#ifdef _WIN32
    u_long enabled = 1;
    return ioctlsocket(socket_handle, FIONBIO, &enabled) == 0;
#else
    const int flags = fcntl(socket_handle, F_GETFL, 0);
    if (flags < 0) return 0;
    return fcntl(socket_handle, F_SETFL, flags | O_NONBLOCK) == 0;
#endif
}

static void srv_send(const SrvServer *server, const unsigned char *packet,
                     size_t length, const struct sockaddr_storage *to,
                     socklen_t to_len) {
#ifdef _WIN32
    if (sendto(server->socket, (const char *)packet, (int)length, 0,
               (const struct sockaddr *)to, (int)to_len) == SOCKET_ERROR) {
        srv_log(SRV_LOG_DEBUG, "sendto: ошибка Winsock %d", (int)WSAGetLastError());
    }
#else
    if (sendto(server->socket, packet, length, 0,
               (const struct sockaddr *)to, to_len) < 0) {
        /* Отказ отдельному адресату не должен останавливать сервер. */
        srv_log(SRV_LOG_DEBUG, "sendto: %s", strerror(errno));
    }
#endif
}

static int srv_open_socket(SrvServer *server) {
    struct addrinfo hints;
    struct addrinfo *addresses = NULL;
    struct addrinfo *address;
    char port_text[16];
    const char *node;
    int bound = 0;

    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;    /* IPv4 или IPv6 — как скажет --bind */
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_protocol = IPPROTO_UDP;
    hints.ai_flags = AI_PASSIVE;    /* пустой адрес — все интерфейсы */

    snprintf(port_text, sizeof port_text, "%d", server->port);
    node = server->bind_address[0] != '\0' ? server->bind_address : NULL;

    if (getaddrinfo(node, port_text, &hints, &addresses) != 0) {
        srv_log(SRV_LOG_ERROR, "не удалось разобрать адрес «%s»",
                server->bind_address[0] != '\0' ? server->bind_address : "(любой)");
        return 0;
    }

    for (address = addresses; address != NULL; address = address->ai_next) {
        SrvSocket socket_handle;

        socket_handle = socket(address->ai_family, address->ai_socktype,
                               address->ai_protocol);
        if (socket_handle == SRV_SOCKET_INVALID) continue;

#ifdef SO_REUSEADDR
        {
            int enabled = 1;
            /* Быстрый перезапуск после остановки: порт не ждёт тайм-аутов
             * ядра. На Windows поведение другое, там флаг не ставим. */
            (void)setsockopt(socket_handle, SOL_SOCKET, SO_REUSEADDR,
                             (const void *)&enabled, sizeof enabled);
        }
#endif

        if (bind(socket_handle, address->ai_addr, address->ai_addrlen) == 0 &&
            srv_set_nonblocking(socket_handle)) {
#ifndef _WIN32
            /* select() не принимает дескрипторы >= FD_SETSIZE; у демона,
             * открывшего много файлов, такое возможно. */
            if (socket_handle >= FD_SETSIZE) {
                srv_log(SRV_LOG_ERROR,
                        "дескриптор сокета %d не помещается в select()", socket_handle);
                srv_close_socket_raw(socket_handle);
                break;
            }
#endif
            server->socket = socket_handle;
            server->socket_open = 1;
            srv_format_endpoint(address->ai_addr, address->ai_addrlen,
                                server->bound_endpoint,
                                sizeof server->bound_endpoint);
            bound = 1;
            break;
        }

        srv_close_socket_raw(socket_handle);
    }

    freeaddrinfo(addresses);

    if (!bound) {
        srv_log(SRV_LOG_ERROR, "не удалось привязать UDP %s:%d",
                server->bind_address[0] != '\0' ? server->bind_address : "0.0.0.0",
                server->port);
        return 0;
    }
    return 1;
}

/* ------------------------------------------------------------------ */
/* Обработка датаграмм                                                */
/* ------------------------------------------------------------------ */

/* [1][max_players:u8][players:u8][tick:u32][name\0][map\0] — точный размер,
 * иначе браузер клиента выбросит ответ как некорректный. */
static void srv_handle_info(SrvServer *server, size_t length,
                            const struct sockaddr_storage *from,
                            socklen_t from_len, const char *endpoint) {
    unsigned char reply[SRV_PACKET_MAX];
    const size_t name_length = strlen(server->name);
    const size_t map_length = strlen(server->map);
    size_t offset = 0;

    if (length != 1) {
        srv_log(SRV_LOG_DEBUG, "[1] от %s: лишние байты (%u), пакет пропущен",
                endpoint, (unsigned)length);
        return;
    }

    reply[offset++] = SRV_MSG_INFO;
    reply[offset++] = (unsigned char)server->max_players;
    reply[offset++] = (unsigned char)server->session_count;
    srv_write_u32_be(reply + offset, srv_current_tick(server));
    offset += 4;
    memcpy(reply + offset, server->name, name_length + 1); /* вместе с нулём */
    offset += name_length + 1;
    memcpy(reply + offset, server->map, map_length + 1);
    offset += map_length + 1;

    srv_send(server, reply, offset, from, from_len);
    srv_log(SRV_LOG_DEBUG, "[1] запрос сведений от %s", endpoint);
}

/* При players >= max_players клиенту отдаётся ровно [2] — он покажет
 * «сервер полон». Иначе [2][id:u32]. Повторный [2] с того же endpoint
 * возвращает уже выданный ID: UDP мог потерять первый ответ. */
static void srv_handle_join(SrvServer *server, size_t length,
                            const struct sockaddr_storage *from,
                            socklen_t from_len, const char *endpoint,
                            double now) {
    unsigned char reply[5];
    SrvSession *session;

    if (length != 1) {
        srv_log(SRV_LOG_DEBUG, "[2] от %s: лишние байты (%u), пакет пропущен",
                endpoint, (unsigned)length);
        return;
    }

    session = srv_find_session(server, from, from_len);
    if (session != NULL) {
        session->last_seen = now;
        reply[0] = SRV_MSG_JOIN;
        srv_write_u32_be(reply + 1, session->id);
        srv_send(server, reply, sizeof reply, from, from_len);
        srv_log(SRV_LOG_DEBUG, "[2] повторный запрос от %s: тот же id %" PRIu32,
                endpoint, session->id);
        return;
    }

    if (server->session_count >= server->max_players) {
        reply[0] = SRV_MSG_JOIN;
        srv_send(server, reply, 1, from, from_len);
        srv_log(SRV_LOG_INFO, "[2] отказано %s: сервер полон (%d/%d)",
                endpoint, server->session_count, server->max_players);
        return;
    }

    session = srv_create_session(server, from, from_len);
    if (session == NULL) {
        /* Не должно случиться: свободное место проверено выше. Но лучше
         * честный отказ, чем второй игрок в занятом слоте. */
        reply[0] = SRV_MSG_JOIN;
        srv_send(server, reply, 1, from, from_len);
        return;
    }

    session->id = srv_generate_id(server);
    session->joined_at = now;
    session->last_seen = now;

    reply[0] = SRV_MSG_JOIN;
    srv_write_u32_be(reply + 1, session->id);
    srv_send(server, reply, sizeof reply, from, from_len);
    srv_log(SRV_LOG_INFO, "игрок id=%" PRIu32 " подключился: %s (%d/%d)",
            session->id, endpoint, server->session_count, server->max_players);
}

/* [3][id][x][y][z][lx][ly][lz] — 29 байт. ID сверяется с сессией этого
 * endpoint; чужой или потерянный ID молча игнорируется. */
static void srv_handle_transform(SrvServer *server, const unsigned char *packet,
                                 size_t length,
                                 const struct sockaddr_storage *from,
                                 socklen_t from_len, const char *endpoint,
                                 double now) {
    static const unsigned char reply[] = {SRV_MSG_TRANSFORM};
    SrvSession *session;

    if (length != SRV_TRANSFORM_LENGTH) {
        srv_log(SRV_LOG_DEBUG, "[3] от %s: неверная длина %u, пакет пропущен",
                endpoint, (unsigned)length);
        return;
    }

    session = srv_find_session(server, from, from_len);
    if (session == NULL) {
        srv_log(SRV_LOG_DEBUG, "[3] от %s без сессии, пакет пропущен", endpoint);
        return;
    }
    if (srv_read_u32_be(packet + 1) != session->id) {
        srv_log(SRV_LOG_DEBUG, "[3] от %s с чужим id, пакет пропущен", endpoint);
        return;
    }

    session->x      = srv_read_f32_be(packet + 5);
    session->y      = srv_read_f32_be(packet + 9);
    session->z      = srv_read_f32_be(packet + 13);
    session->look_x = srv_read_f32_be(packet + 17);
    session->look_y = srv_read_f32_be(packet + 21);
    session->look_z = srv_read_f32_be(packet + 25);
    session->has_state = 1;
    session->last_seen = now;

    srv_send(server, reply, sizeof reply, from, from_len);
    srv_log(SRV_LOG_DEBUG, "[3] id=%" PRIu32 " %s: (%.2f, %.2f, %.2f)",
            session->id, endpoint,
            (double)session->x, (double)session->y, (double)session->z);
}

/* [4][id] — 5 байт. Ответ: [4] и записи остальных игроков с известным
 * состоянием, [x][y][z][lx][ly][lz] по 24 байта, между записями [0].
 * Про самого спрашивающего не рассказываем: клиент рисует силуэты
 * только чужих игроков. Пустой список — просто [4]. */
static void srv_handle_players(SrvServer *server, const unsigned char *packet,
                               size_t length,
                               const struct sockaddr_storage *from,
                               socklen_t from_len, const char *endpoint,
                               double now) {
    unsigned char reply[SRV_PACKET_MAX];
    SrvSession *session;
    size_t offset = 0;
    int entries = 0;
    int i;

    if (length != 5) {
        srv_log(SRV_LOG_DEBUG, "[4] от %s: неверная длина %u, пакет пропущен",
                endpoint, (unsigned)length);
        return;
    }

    session = srv_find_session(server, from, from_len);
    if (session == NULL) {
        srv_log(SRV_LOG_DEBUG, "[4] от %s без сессии, пакет пропущен", endpoint);
        return;
    }
    if (srv_read_u32_be(packet + 1) != session->id) {
        srv_log(SRV_LOG_DEBUG, "[4] от %s с чужим id, пакет пропущен", endpoint);
        return;
    }

    session->last_seen = now;

    reply[offset++] = SRV_MSG_PLAYERS;
    for (i = 0; i < server->max_players && entries < SRV_PLAYERS_PER_PACKET; i++) {
        const SrvSession *other = &server->sessions[i];
        if (!other->active || other == session || !other->has_state) continue;

        if (entries > 0) reply[offset++] = 0; /* разделитель между записями */
        srv_write_f32_be(reply + offset, other->x);      offset += 4;
        srv_write_f32_be(reply + offset, other->y);      offset += 4;
        srv_write_f32_be(reply + offset, other->z);      offset += 4;
        srv_write_f32_be(reply + offset, other->look_x); offset += 4;
        srv_write_f32_be(reply + offset, other->look_y); offset += 4;
        srv_write_f32_be(reply + offset, other->look_z); offset += 4;
        entries++;
    }

    srv_send(server, reply, offset, from, from_len);
    srv_log(SRV_LOG_DEBUG, "[4] id=%" PRIu32 " %s: %d игрок(ов)",
            session->id, endpoint, entries);
}

/* [5] завершает сессию по endpoint, без аргументов и без ответа. */
static void srv_handle_leave(SrvServer *server, size_t length,
                             const struct sockaddr_storage *from,
                             socklen_t from_len, const char *endpoint) {
    SrvSession *session;

    if (length != 1) {
        srv_log(SRV_LOG_DEBUG, "[5] от %s: лишние байты (%u), пакет пропущен",
                endpoint, (unsigned)length);
        return;
    }

    session = srv_find_session(server, from, from_len);
    if (session == NULL) {
        srv_log(SRV_LOG_DEBUG, "[5] от %s без сессии", endpoint);
        return;
    }

    srv_drop_session(server, session);
    srv_log(SRV_LOG_INFO, "игрок id=%" PRIu32 " отключился: %s (%d/%d)",
            session->id, session->endpoint,
            server->session_count, server->max_players);
}

static void srv_handle_datagram(SrvServer *server, const unsigned char *packet,
                                size_t length,
                                const struct sockaddr_storage *from,
                                socklen_t from_len, double now) {
    char endpoint[SRV_ENDPOINT_MAX + 1];

    if (length == 0) return;

    srv_format_endpoint((const struct sockaddr *)from, from_len,
                        endpoint, sizeof endpoint);

    switch (packet[0]) {
        case SRV_MSG_INFO:
            srv_handle_info(server, length, from, from_len, endpoint);
            break;
        case SRV_MSG_JOIN:
            srv_handle_join(server, length, from, from_len, endpoint, now);
            break;
        case SRV_MSG_TRANSFORM:
            srv_handle_transform(server, packet, length, from, from_len,
                                 endpoint, now);
            break;
        case SRV_MSG_PLAYERS:
            srv_handle_players(server, packet, length, from, from_len,
                               endpoint, now);
            break;
        case SRV_MSG_LEAVE:
            srv_handle_leave(server, length, from, from_len, endpoint);
            break;
        default:
            /* Случайный UDP-шум или пакеты будущих версий протокола. */
            srv_log(SRV_LOG_DEBUG, "датаграмма [%u] от %s: неизвестный номер, "
                    "игнорируется", (unsigned)packet[0], endpoint);
            break;
    }
}

/* UDP может потерять [5], поэтому давно молчащие сессии закрываются
 * принудительно. Живой клиент шлёт [3] 60 раз в секунду и [4] до 10 раз,
 * так что тайм-аут считается по последнему [2]/[3]/[4]. */
static void srv_sweep_timeouts(SrvServer *server, double now) {
    int i;
    for (i = 0; i < server->max_players; i++) {
        SrvSession *session = &server->sessions[i];
        if (!session->active) continue;
        if (now - session->last_seen <= server->timeout_seconds) continue;

        srv_drop_session(server, session);
        srv_log(SRV_LOG_INFO, "тайм-аут игрока id=%" PRIu32 ": %s молчал %.0f с (%d/%d)",
                session->id, session->endpoint, server->timeout_seconds,
                server->session_count, server->max_players);
    }
}

static void srv_receive_datagrams(SrvServer *server) {
    unsigned char packet[SRV_PACKET_MAX];

    for (;;) {
        struct sockaddr_storage from;
        socklen_t from_len = sizeof from;
        const double now = srv_monotonic_seconds();
        int received;

#ifdef _WIN32
        received = recvfrom(server->socket, (char *)packet, (int)sizeof packet, 0,
                            (struct sockaddr *)&from, &from_len);
        if (received == SOCKET_ERROR) {
            const int error = WSAGetLastError();
            /* WSAECONNRESET прилетает на UDP-сокет, если какой-то из прежних
             * адресатов закрыл порт; это не ошибка сервера. */
            if (error == WSAEWOULDBLOCK || error == WSAECONNRESET) break;
            srv_log(SRV_LOG_ERROR, "recvfrom: ошибка Winsock %d", error);
            break;
        }
#else
        received = recvfrom(server->socket, packet, sizeof packet, 0,
                            (struct sockaddr *)&from, &from_len);
        if (received < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) break;
            if (errno == EINTR) break;
            srv_log(SRV_LOG_ERROR, "recvfrom: %s", strerror(errno));
            break;
        }
#endif
        if (received == 0) continue; /* пустая датаграмма — тоже валидный UDP */

        srv_handle_datagram(server, packet, (size_t)received, &from, from_len, now);
    }
}

/* ------------------------------------------------------------------ */
/* Команды stdin                                                      */
/* ------------------------------------------------------------------ */

/* Команда длиннее SRV_COMMAND_MAX байт обрезается; её хвост fgets
 * прочитает следующим куском — это то же поведение, что и разбивка
 * длинной строки на две команды. */
static void srv_copy_command(char out[SRV_COMMAND_MAX + 1], const char *src) {
    size_t i;
    for (i = 0; i < SRV_COMMAND_MAX && src[i] != '\0'; i++) {
        out[i] = src[i];
    }
    out[i] = '\0';
}

static void srv_command_push(const char *line) {
#ifdef _WIN32
    EnterCriticalSection(&g_command_lock);
#else
    pthread_mutex_lock(&g_command_lock);
#endif
    if (g_command_count < SRV_COMMAND_QUEUE) {
        srv_copy_command(g_commands[g_command_count], line);
        g_command_count++;
    }
    /* Переполнение теряется молча: очередь велика, а команды вводит
     * человек, который тут же повторит. */
#ifdef _WIN32
    LeaveCriticalSection(&g_command_lock);
#else
    pthread_mutex_unlock(&g_command_lock);
#endif
}

/* Буфер out обязан вмещать SRV_COMMAND_MAX + 1 байт: строка копируется
 * целиком вместе с завершающим нулём, как она лежит в очереди. */
static int srv_command_pop(char out[SRV_COMMAND_MAX + 1]) {
    int taken = 0;

#ifdef _WIN32
    EnterCriticalSection(&g_command_lock);
#else
    pthread_mutex_lock(&g_command_lock);
#endif
    if (g_command_count > 0) {
        memcpy(out, g_commands[0], SRV_COMMAND_MAX + 1);
        taken = 1;
        g_command_count--;
        if (g_command_count > 0) {
            memmove(g_commands[0], g_commands[1],
                    (size_t)g_command_count * sizeof g_commands[0]);
        }
    }
#ifdef _WIN32
    LeaveCriticalSection(&g_command_lock);
#else
    pthread_mutex_unlock(&g_command_lock);
#endif
    return taken;
}

/* Отдельный поток блокирующе читает stdin: на Windows select() умеет
 * ждать только сокеты, а выделять консоль в неблокирующий режим —
 * сложнее и хрупче, чем одна нить на fgets. */
static
#ifdef _WIN32
unsigned __stdcall
#else
void *
#endif
srv_stdin_thread(void *arg) {
    char line[SRV_COMMAND_MAX + 1];

    (void)arg;
    while (fgets(line, sizeof line, stdin) != NULL) {
        line[strcspn(line, "\r\n")] = '\0';
        srv_command_push(line);
    }
    /* stdin закрыт (сервис без терминала) — это не ошибка, сервер
     * продолжает работать, просто без интерактивных команд. */
    g_stdin_closed = 1;
    return 0;
}

static int srv_start_stdin_thread(void) {
#ifdef _WIN32
    const uintptr_t thread = _beginthreadex(NULL, 0, srv_stdin_thread, NULL,
                                            0, NULL);
    if (thread == 0) return 0;
    CloseHandle((HANDLE)thread);
    return 1;
#else
    pthread_t thread;
    if (pthread_create(&thread, NULL, srv_stdin_thread, NULL) != 0) return 0;
    pthread_detach(thread);
    return 1;
#endif
}

static void srv_format_duration(double seconds, char *out, size_t out_size) {
    const long total = seconds < 0.0 ? 0 : (long)(seconds + 0.5);
    const long days = total / 86400;
    const long hours = (total % 86400) / 3600;
    const long minutes = (total % 3600) / 60;
    const long secs = total % 60;

    if (days > 0) {
        snprintf(out, out_size, "%ldд %ldч %ldм", days, hours, minutes);
    } else if (hours > 0) {
        snprintf(out, out_size, "%ldч %ldм %ldс", hours, minutes, secs);
    } else if (minutes > 0) {
        snprintf(out, out_size, "%ldм %ldс", minutes, secs);
    } else {
        snprintf(out, out_size, "%ldс", secs);
    }
}

static void srv_command_status(SrvServer *server, double now) {
    char duration[64];

    srv_format_duration(now - server->started_at, duration, sizeof duration);
    printf("аптайм      %s\n", duration);
    printf("адрес       %s\n", server->bound_endpoint);
    printf("имя         %s\n", server->name);
    printf("карта       %s\n",
           server->map[0] != '\0' ? server->map : "(не задана: клиент оставляет свою)");
    printf("игроки      %d/%d\n", server->session_count, server->max_players);
    printf("тайм-аут    %.0f с\n", server->timeout_seconds);
    printf("tick        %" PRIu32 "\n", srv_current_tick(server));
    if (g_stdin_closed) {
        printf("stdin       закрыт, команды недоступны\n");
    }
    fflush(stdout);
}

static void srv_command_list(SrvServer *server, double now) {
    int i;
    int printed = 0;

    for (i = 0; i < server->max_players; i++) {
        const SrvSession *session = &server->sessions[i];
        if (!session->active) continue;

        if (session->has_state) {
            printf("%-10" PRIu32 "  %-22s  (%8.2f %8.2f %8.2f)  простой %.1f с\n",
                   session->id, session->endpoint,
                   (double)session->x, (double)session->y, (double)session->z,
                   now - session->last_seen);
        } else {
            printf("%-10" PRIu32 "  %-22s  (состояния ещё нет)       простой %.1f с\n",
                   session->id, session->endpoint, now - session->last_seen);
        }
        printed++;
    }

    if (printed == 0) printf("нет подключённых игроков\n");
    fflush(stdout);
}

static void srv_command_kick(SrvServer *server, const char *argument) {
    char *end = NULL;
    unsigned long value;
    SrvSession *session;

    if (argument == NULL || argument[0] == '\0') {
        printf("kick: укажите числовой ID игрока (см. list)\n");
        fflush(stdout);
        return;
    }

    errno = 0;
    value = strtoul(argument, &end, 10);
    if (errno != 0 || end == argument || *end != '\0' ||
        value > 0xFFFFFFFFUL) {
        printf("kick: «%s» — не ID (нужно число 0..4294967295)\n", argument);
        fflush(stdout);
        return;
    }

    session = srv_find_session_by_id(server, (uint32_t)value);
    if (session == NULL) {
        printf("kick: игрок с id %lu не найден\n", value);
        fflush(stdout);
        return;
    }

    srv_drop_session(server, session);
    srv_log(SRV_LOG_INFO, "kick: игрок id=%" PRIu32 " отключён (%s) (%d/%d)",
            session->id, session->endpoint,
            server->session_count, server->max_players);
}

static void srv_run_command(SrvServer *server, char *line, double now) {
    char *argument = NULL;
    char *end;

    while (*line == ' ' || *line == '\t') line++;

    end = line + strlen(line);
    while (end > line && (end[-1] == ' ' || end[-1] == '\t')) {
        end--;
        *end = '\0';
    }
    if (*line == '\0') return;

    end = strpbrk(line, " \t");
    if (end != NULL) {
        *end = '\0';
        argument = end + 1;
        while (*argument == ' ' || *argument == '\t') argument++;
        if (*argument == '\0') argument = NULL;
    }

    if (strcmp(line, "help") == 0 || strcmp(line, "?") == 0) {
        printf("команды: help, status, list, kick <id>, quit\n");
        fflush(stdout);
        return;
    }
    if (strcmp(line, "status") == 0) {
        srv_command_status(server, now);
        return;
    }
    if (strcmp(line, "list") == 0) {
        srv_command_list(server, now);
        return;
    }
    if (strcmp(line, "kick") == 0) {
        srv_command_kick(server, argument);
        return;
    }
    if (strcmp(line, "quit") == 0 || strcmp(line, "exit") == 0 ||
        strcmp(line, "shutdown") == 0) {
        g_running = 0;
        return;
    }

    printf("неизвестная команда «%s» — help покажет список\n", line);
    fflush(stdout);
}

/* ------------------------------------------------------------------ */
/* Сигналы                                                            */
/* ------------------------------------------------------------------ */

static void srv_on_signal(int signal_number) {
    (void)signal_number;
    g_running = 0;
}

static void srv_install_signal_handlers(void) {
#ifdef _WIN32
    signal(SIGINT, srv_on_signal);
    signal(SIGBREAK, srv_on_signal);
#else
    struct sigaction action;

    memset(&action, 0, sizeof action);
    action.sa_handler = srv_on_signal;
    sigemptyset(&action.sa_mask);
    if (sigaction(SIGINT, &action, NULL) != 0) {
        signal(SIGINT, srv_on_signal);
    }
    if (sigaction(SIGTERM, &action, NULL) != 0) {
        signal(SIGTERM, srv_on_signal);
    }
    /* sendto в «мёртвый» endpoint не должен убивать сервер SIGPIPE. */
    signal(SIGPIPE, SIG_IGN);
#endif
}

/* ------------------------------------------------------------------ */
/* Разбор аргументов командной строки                                 */
/* ------------------------------------------------------------------ */

enum {
    SRV_PARSE_OK = 0,
    SRV_PARSE_EXIT_OK,
    SRV_PARSE_ERROR
};

static void srv_print_usage(FILE *stream) {
    fprintf(stream,
        "game3d-server %s — автономный CLI-сервер Game3D (протокол в README.md)\n"
        "\n"
        "Использование: game3d-server [параметры]\n"
        "\n"
        "Параметры:\n"
        "  -p, --port <порт>        UDP-порт, 1..65535 (по умолчанию %d)\n"
        "  -b, --bind <адрес>       адрес привязки сокета (по умолчанию 0.0.0.0;\n"
        "                           IPv6: --bind ::)\n"
        "  -n, --name <имя>         имя в списке серверов, до %d байт UTF-8\n"
        "                           (по умолчанию \"%s\")\n"
        "  -m, --map <карта>        карта для подключившихся клиентов, до %d\n"
        "                           байт печатного ASCII; пустая строка — каждый\n"
        "                           клиент играет на выбранной в меню карте\n"
        "  -M, --max-players <N>    максимум игроков, 1..%d (по умолчанию %d)\n"
        "  -t, --timeout <сек>      тайм-аут сессии без пакетов, минимум 1\n"
        "                           (по умолчанию %.0f)\n"
        "  -v, --verbose            логировать каждую датаграмму\n"
        "  -q, --quiet              печатать только ошибки\n"
        "  -h, --help               этот текст\n"
        "  -V, --version            версия сервера\n"
        "\n"
        "Команды со stdin во время работы:\n"
        "  help        список команд\n"
        "  status      аптайм, адрес, игроки, tick\n"
        "  list        подключённые игроки и их ID\n"
        "  kick <id>   отключить игрока по ID\n"
        "  quit        остановить сервер (то же — Ctrl+C)\n"
        "\n"
        "Примеры:\n"
        "  game3d-server --name \"Ночная смена\" --map arena.tfm --max-players 8\n"
        "  game3d-server -b 192.168.1.40 -p 28000 -v\n",
        SRV_VERSION, SRV_DEFAULT_PORT, SRV_NAME_MAX, SRV_DEFAULT_NAME,
        SRV_MAP_MAX, SRV_MAX_PLAYERS_LIMIT, SRV_DEFAULT_MAX_PLAYERS,
        SRV_DEFAULT_TIMEOUT_SECONDS);
}

static void srv_print_version(void) {
    printf("game3d-server %s\n", SRV_VERSION);
    printf("протокол многопользовательской игры Game3D — см. README.md\n");
}

static int srv_parse_int(const char *text, long minimum, long maximum, long *out) {
    char *end = NULL;
    long value;

    errno = 0;
    value = strtol(text, &end, 10);
    if (errno != 0 || end == text || *end != '\0') return 0;
    if (value < minimum || value > maximum) return 0;
    *out = value;
    return 1;
}

static int srv_parse_seconds(const char *text, double minimum, double *out) {
    char *end = NULL;
    double value;

    errno = 0;
    value = strtod(text, &end);
    if (errno != 0 || end == text || *end != '\0') return 0;
    if (value < minimum) return 0;
    *out = value;
    return 1;
}

/* Имя — UTF-8, поэтому ограничение в байтах, а не в символах: клиент
 * проверяет именно длину в байтах. */
static int srv_valid_name(const char *text) {
    return strlen(text) <= SRV_NAME_MAX;
}

/* Карта должна быть печатным ASCII: это имя локального файла клиента. */
static int srv_valid_map(const char *text) {
    size_t i;
    if (strlen(text) > SRV_MAP_MAX) return 0;
    for (i = 0; text[i] != '\0'; i++) {
        if ((unsigned char)text[i] < 0x20 || (unsigned char)text[i] > 0x7e) {
            return 0;
        }
    }
    return 1;
}

/* Значение опции: следующий argv или NULL с сообщением об ошибке. */
static const char *srv_next_value(int argc, char **argv, int *index,
                                  const char *option) {
    if (*index + 1 >= argc) {
        fprintf(stderr, "game3d-server: опции %s нужно значение\n", option);
        return NULL;
    }
    (*index)++;
    return argv[*index];
}

static int srv_parse_long_option(int argc, char **argv, int *index,
                                 SrvConfig *config) {
    char name_buffer[64];
    const char *name = argv[*index] + 2;
    const char *value = NULL;
    const char *equals = strchr(name, '=');

    if (equals != NULL) {
        const size_t length = (size_t)(equals - name);
        if (length == 0 || length >= sizeof name_buffer) {
            fprintf(stderr, "game3d-server: неверная запись «%s»\n", argv[*index]);
            return SRV_PARSE_ERROR;
        }
        memcpy(name_buffer, name, length);
        name_buffer[length] = '\0';
        name = name_buffer;
        value = equals + 1;
    }

    if (strcmp(name, "help") == 0) {
        if (value != NULL) {
            fprintf(stderr, "game3d-server: --help не принимает значение\n");
            return SRV_PARSE_ERROR;
        }
        srv_print_usage(stdout);
        return SRV_PARSE_EXIT_OK;
    }
    if (strcmp(name, "version") == 0) {
        if (value != NULL) {
            fprintf(stderr, "game3d-server: --version не принимает значение\n");
            return SRV_PARSE_ERROR;
        }
        srv_print_version();
        return SRV_PARSE_EXIT_OK;
    }
    if (strcmp(name, "verbose") == 0) {
        if (value != NULL) {
            fprintf(stderr, "game3d-server: --verbose не принимает значение\n");
            return SRV_PARSE_ERROR;
        }
        config->verbose = 1;
        return SRV_PARSE_OK;
    }
    if (strcmp(name, "quiet") == 0) {
        if (value != NULL) {
            fprintf(stderr, "game3d-server: --quiet не принимает значение\n");
            return SRV_PARSE_ERROR;
        }
        config->quiet = 1;
        return SRV_PARSE_OK;
    }

    if (strcmp(name, "port") == 0 || strcmp(name, "bind") == 0 ||
        strcmp(name, "name") == 0 || strcmp(name, "map") == 0 ||
        strcmp(name, "max-players") == 0 || strcmp(name, "timeout") == 0) {
        if (value == NULL) {
            value = srv_next_value(argc, argv, index, argv[*index]);
            if (value == NULL) return SRV_PARSE_ERROR;
        }
        if (strcmp(name, "port") == 0) {
            long number;
            if (!srv_parse_int(value, 1, 65535, &number)) {
                fprintf(stderr, "game3d-server: «--port %s»: порт — целое число "
                        "1..65535\n", value);
                return SRV_PARSE_ERROR;
            }
            config->port = (int)number;
            return SRV_PARSE_OK;
        }
        if (strcmp(name, "bind") == 0) {
            if (strlen(value) > SRV_BIND_MAX) {
                fprintf(stderr, "game3d-server: «--bind %s»: адрес длиннее %d "
                        "байт\n", value, SRV_BIND_MAX);
                return SRV_PARSE_ERROR;
            }
            config->bind_address = value;
            return SRV_PARSE_OK;
        }
        if (strcmp(name, "name") == 0) {
            if (!srv_valid_name(value)) {
                fprintf(stderr, "game3d-server: «--name %s»: имя длиннее %d "
                        "байт\n", value, SRV_NAME_MAX);
                return SRV_PARSE_ERROR;
            }
            config->name = value;
            return SRV_PARSE_OK;
        }
        if (strcmp(name, "map") == 0) {
            if (!srv_valid_map(value)) {
                fprintf(stderr, "game3d-server: «--map %s»: карта — до %d байт "
                        "печатного ASCII\n", value, SRV_MAP_MAX);
                return SRV_PARSE_ERROR;
            }
            config->map = value;
            return SRV_PARSE_OK;
        }
        if (strcmp(name, "max-players") == 0) {
            long number;
            if (!srv_parse_int(value, 1, SRV_MAX_PLAYERS_LIMIT, &number)) {
                fprintf(stderr, "game3d-server: «--max-players %s»: нужно целое "
                        "число 1..%d\n", value, SRV_MAX_PLAYERS_LIMIT);
                return SRV_PARSE_ERROR;
            }
            config->max_players = (int)number;
            return SRV_PARSE_OK;
        }
        /* --timeout */
        {
            double number;
            if (!srv_parse_seconds(value, 1.0, &number)) {
                fprintf(stderr, "game3d-server: «--timeout %s»: нужно число "
                        "секунд не меньше 1\n", value);
                return SRV_PARSE_ERROR;
            }
            config->timeout_seconds = number;
            return SRV_PARSE_OK;
        }
    }

    fprintf(stderr, "game3d-server: неизвестная опция «--%s»\n", name);
    return SRV_PARSE_ERROR;
}

static int srv_parse_short_options(int argc, char **argv, int *index,
                                   SrvConfig *config) {
    const char *cluster = argv[*index] + 1;

    while (*cluster != '\0') {
        const char option = *cluster;
        const char *value = NULL;

        /* Опции со значением: -p27015 или -p 27015. */
        if (option == 'p' || option == 'b' || option == 'n' ||
            option == 'm' || option == 'M' || option == 't') {
            if (cluster[1] != '\0') {
                value = cluster + 1;
            } else {
                char option_text[3];
                option_text[0] = '-';
                option_text[1] = option;
                option_text[2] = '\0';
                value = srv_next_value(argc, argv, index, option_text);
                if (value == NULL) return SRV_PARSE_ERROR;
            }
        }

        switch (option) {
            case 'h':
                srv_print_usage(stdout);
                return SRV_PARSE_EXIT_OK;
            case 'V':
                srv_print_version();
                return SRV_PARSE_EXIT_OK;
            case 'v':
                config->verbose = 1;
                cluster++;
                break;
            case 'q':
                config->quiet = 1;
                cluster++;
                break;
            case 'p': {
                long number;
                if (!srv_parse_int(value, 1, 65535, &number)) {
                    fprintf(stderr, "game3d-server: «-%c %s»: порт — целое число "
                            "1..65535\n", option, value);
                    return SRV_PARSE_ERROR;
                }
                config->port = (int)number;
                cluster += strlen(cluster);
                break;
            }
            case 'b':
                if (strlen(value) > SRV_BIND_MAX) {
                    fprintf(stderr, "game3d-server: «-%c %s»: адрес длиннее %d "
                            "байт\n", option, value, SRV_BIND_MAX);
                    return SRV_PARSE_ERROR;
                }
                config->bind_address = value;
                cluster += strlen(cluster);
                break;
            case 'n':
                if (!srv_valid_name(value)) {
                    fprintf(stderr, "game3d-server: «-%c %s»: имя длиннее %d "
                            "байт\n", option, value, SRV_NAME_MAX);
                    return SRV_PARSE_ERROR;
                }
                config->name = value;
                cluster += strlen(cluster);
                break;
            case 'm':
                if (!srv_valid_map(value)) {
                    fprintf(stderr, "game3d-server: «-%c %s»: карта — до %d байт "
                            "печатного ASCII\n", option, value, SRV_MAP_MAX);
                    return SRV_PARSE_ERROR;
                }
                config->map = value;
                cluster += strlen(cluster);
                break;
            case 'M': {
                long number;
                if (!srv_parse_int(value, 1, SRV_MAX_PLAYERS_LIMIT, &number)) {
                    fprintf(stderr, "game3d-server: «-%c %s»: нужно целое число "
                            "1..%d\n", option, value, SRV_MAX_PLAYERS_LIMIT);
                    return SRV_PARSE_ERROR;
                }
                config->max_players = (int)number;
                cluster += strlen(cluster);
                break;
            }
            case 't': {
                double number;
                if (!srv_parse_seconds(value, 1.0, &number)) {
                    fprintf(stderr, "game3d-server: «-%c %s»: нужно число секунд "
                            "не меньше 1\n", option, value);
                    return SRV_PARSE_ERROR;
                }
                config->timeout_seconds = number;
                cluster += strlen(cluster);
                break;
            }
            default:
                fprintf(stderr, "game3d-server: неизвестная опция «-%c»\n", option);
                return SRV_PARSE_ERROR;
        }
    }
    return SRV_PARSE_OK;
}

static int srv_parse_args(int argc, char **argv, SrvConfig *config) {
    int i = 1;

    while (i < argc) {
        const char *arg = argv[i];
        int result;

        if (strcmp(arg, "--") == 0) {
            if (i + 1 < argc) {
                fprintf(stderr, "game3d-server: неожиданный аргумент «%s»\n",
                        argv[i + 1]);
                return SRV_PARSE_ERROR;
            }
            break;
        }
        if (arg[0] != '-' || arg[1] == '\0') {
            fprintf(stderr, "game3d-server: неожиданный аргумент «%s»\n", arg);
            return SRV_PARSE_ERROR;
        }

        if (arg[1] == '-') {
            result = srv_parse_long_option(argc, argv, &i, config);
        } else {
            result = srv_parse_short_options(argc, argv, &i, config);
        }
        if (result != SRV_PARSE_OK) return result;

        i++;
    }

    if (config->verbose && config->quiet) {
        fprintf(stderr, "game3d-server: --verbose и --quiet противоречат "
                "друг другу\n");
        return SRV_PARSE_ERROR;
    }
    return SRV_PARSE_OK;
}

/* ------------------------------------------------------------------ */
/* main                                                               */
/* ------------------------------------------------------------------ */

int main(int argc, char **argv) {
    SrvConfig config;
    SrvServer server;
    int result;

    config.bind_address = NULL;
    config.name = SRV_DEFAULT_NAME;
    config.map = "";
    config.port = SRV_DEFAULT_PORT;
    config.max_players = SRV_DEFAULT_MAX_PLAYERS;
    config.timeout_seconds = SRV_DEFAULT_TIMEOUT_SECONDS;
    config.verbose = 0;
    config.quiet = 0;

    memset(&server, 0, sizeof server);
    server.socket = SRV_SOCKET_INVALID;

    result = srv_parse_args(argc, argv, &config);
    if (result == SRV_PARSE_EXIT_OK) return 0;
    if (result == SRV_PARSE_ERROR) {
        fprintf(stderr, "запустите game3d-server --help для списка параметров\n");
        return 1;
    }

    g_log_level = config.quiet ? SRV_LOG_ERROR
                               : (config.verbose ? SRV_LOG_DEBUG : SRV_LOG_INFO);
    srand(srv_random_seed());

#ifdef _WIN32
    {
        WSADATA data;
        if (WSAStartup(MAKEWORD(2, 2), &data) != 0) {
            srv_log(SRV_LOG_ERROR, "не удалось запустить Winsock");
            return 1;
        }
        g_winsock_started = 1;
    }
#endif

    snprintf(server.bind_address, sizeof server.bind_address, "%s",
             config.bind_address != NULL ? config.bind_address : "0.0.0.0");
    snprintf(server.name, sizeof server.name, "%s", config.name);
    snprintf(server.map, sizeof server.map, "%s", config.map);
    server.port = config.port;
    server.max_players = config.max_players;
    server.timeout_seconds = config.timeout_seconds;

    if (!srv_open_socket(&server)) {
#ifdef _WIN32
        if (g_winsock_started) WSACleanup();
#endif
        return 1;
    }

    server.sessions = calloc((size_t)server.max_players, sizeof *server.sessions);
    if (server.sessions == NULL) {
        srv_log(SRV_LOG_ERROR, "не хватило памяти под таблицу сессий");
        srv_close_socket_raw(server.socket);
#ifdef _WIN32
        WSACleanup();
#endif
        return 1;
    }

#ifdef _WIN32
    InitializeCriticalSection(&g_command_lock);
#endif
    if (!srv_start_stdin_thread()) {
        /* Не фатально: сервер продолжает работать без интерактивных
         * команд, флаги командной строки никуда не деваются. */
        srv_log(SRV_LOG_ERROR, "не удалось запустить поток чтения stdin, "
                "команды недоступны");
    }

    srv_install_signal_handlers();
    server.started_at = srv_monotonic_seconds();

    srv_log(SRV_LOG_INFO, "сервер запущен: %s, имя «%s», карта %s, до %d "
            "игроков, тайм-аут %.0f с",
            server.bound_endpoint, server.name,
            server.map[0] != '\0' ? server.map : "(клиент оставляет свою)",
            server.max_players, server.timeout_seconds);
    srv_log(SRV_LOG_INFO, "команды: help, status, list, kick <id>, quit");

    while (g_running) {
        struct timeval timeout;
        fd_set read_set;
        int ready;

        FD_ZERO(&read_set);
        FD_SET(server.socket, &read_set);
        timeout.tv_sec = 0;
        timeout.tv_usec = (long)(SRV_SELECT_SECONDS * 1.0e6);

#ifdef _WIN32
        ready = select(0, &read_set, NULL, NULL, &timeout);
#else
        ready = select(server.socket + 1, &read_set, NULL, NULL, &timeout);
#endif
        if (ready < 0) {
            if (errno == EINTR) continue;
            srv_log(SRV_LOG_ERROR, "select: %s", strerror(errno));
            break;
        }

        if (ready > 0) {
            srv_receive_datagrams(&server);
        }

        {
            char command[SRV_COMMAND_MAX + 1];
            const double now = srv_monotonic_seconds();
            while (srv_command_pop(command)) {
                srv_run_command(&server, command, now);
            }
            srv_sweep_timeouts(&server, now);
        }
    }

    {
        char duration[64];
        srv_format_duration(srv_monotonic_seconds() - server.started_at,
                            duration, sizeof duration);
        srv_log(SRV_LOG_INFO, "сервер остановлен (аптайм %s)", duration);
    }

    if (server.socket_open) {
        srv_close_socket_raw(server.socket);
        server.socket_open = 0;
    }
    free(server.sessions);

#ifdef _WIN32
    DeleteCriticalSection(&g_command_lock);
    if (g_winsock_started) WSACleanup();
#endif
    return 0;
}
