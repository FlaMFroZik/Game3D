#ifndef MULTIPLAYER_H
#define MULTIPLAYER_H

/* ------------------------------------------------------------------
 * UDP-клиент многопользовательского режима.
 *
 * Здесь нет и не должно быть реализации сервера. Точная раскладка
 * датаграмм описана в README.md, чтобы сервер можно было написать отдельно.
 * ------------------------------------------------------------------ */

#include <stdint.h>

#define MULTIPLAYER_DEFAULT_PORT        "27015"
#define MULTIPLAYER_ENDPOINT_MAX       255
#define MULTIPLAYER_NAME_MAX            63
#define MULTIPLAYER_MAP_MAX            255
#define MULTIPLAYER_MAX_REMOTE_PLAYERS  64
#define MULTIPLAYER_MAX_SERVERS          64
#define MULTIPLAYER_SERVER_FILE_MAX     511

typedef enum {
    MULTIPLAYER_IDLE = 0,
    MULTIPLAYER_WAIT_INFO,
    MULTIPLAYER_WAIT_JOIN,
    MULTIPLAYER_READY,
    MULTIPLAYER_FULL,
    MULTIPLAYER_TIMEOUT,
    MULTIPLAYER_ERROR
} MultiplayerState;

typedef struct {
    uint8_t max_players;
    uint8_t players;
    uint32_t tick;
    char name[MULTIPLAYER_NAME_MAX + 1];
    char map[MULTIPLAYER_MAP_MAX + 1];
} MultiplayerServerInfo;

typedef struct {
    float x, y, z;
    float look_x, look_y, look_z;
} MultiplayerRemotePlayer;

typedef struct {
    /* uintptr_t позволяет не тянуть платформенные socket-заголовки в игру. */
    uintptr_t socket_handle;
    int socket_open;
    int winsock_started;

    MultiplayerState state;
    int joined;
    uint32_t player_id;
    char endpoint[MULTIPLAYER_ENDPOINT_MAX + 1];
    char status[128];

    MultiplayerServerInfo info;
    MultiplayerRemotePlayer remote_players[MULTIPLAYER_MAX_REMOTE_PLAYERS];
    int remote_player_count;

    double last_request_at;
    int request_attempts;
    double next_players_request_at;
} MultiplayerClient;

/* ---------- Поиск серверов локальной сети ----------
 * У протокола нет master-сервера, поэтому «все доступные» означает все
 * серверы в LAN, ответившие на IPv4 UDP broadcast [1] за окно поиска. */
typedef enum {
    MULTIPLAYER_DISCOVERY_IDLE = 0,
    MULTIPLAYER_DISCOVERY_SCANNING,
    MULTIPLAYER_DISCOVERY_FINISHED,
    MULTIPLAYER_DISCOVERY_ERROR
} MultiplayerDiscoveryState;

typedef struct {
    char endpoint[MULTIPLAYER_ENDPOINT_MAX + 1];
    MultiplayerServerInfo info;
} MultiplayerServerEntry;

typedef struct {
    uintptr_t socket_handle;
    int socket_open;
    int winsock_started;
    MultiplayerDiscoveryState state;
    char status[128];
    char server_file[MULTIPLAYER_SERVER_FILE_MAX + 1];
    double finish_at;
    int phase;
    int file_query_count;
    MultiplayerServerEntry servers[MULTIPLAYER_MAX_SERVERS];
    int server_count;
} MultiplayerServerBrowser;

/* Начальное пустое состояние. */
void multiplayer_init(MultiplayerClient *client);

/* Открывает UDP-сокет к endpoint (host:port, порт по умолчанию 27015) и
 * начинает обмен: сначала посылается [1], затем после ответа — [2]. */
int multiplayer_connect(MultiplayerClient *client, const char *endpoint,
                        double now);

/* Принимает все доступные UDP-датаграммы и повторяет потерянные [1]/[2].
 * Вызывать каждый кадр в меню подключения и во время сетевой игры. */
void multiplayer_update(MultiplayerClient *client, double now);

/* Посылает предусмотренный протоколом пакет [3][x][y][z][lx][ly][lz]. */
void multiplayer_send_transform(MultiplayerClient *client,
                                float x, float y, float z,
                                float look_x, float look_y, float look_z);

/* Посылает [4] не чаще десяти раз в секунду, чтобы получить видимых игроков. */
void multiplayer_request_visible_players(MultiplayerClient *client, double now);

/* Посылает [5] для удаления ID на сервере и закрывает клиентский UDP-сокет.
 * Эту функцию безопасно вызывать и до успешного подключения. */
void multiplayer_disconnect(MultiplayerClient *client);

int multiplayer_is_joined(const MultiplayerClient *client);
const char *multiplayer_status(const MultiplayerClient *client);
const MultiplayerServerInfo *multiplayer_server_info(const MultiplayerClient *client);
const MultiplayerRemotePlayer *multiplayer_remote_players(const MultiplayerClient *client,
                                                           int *count);

/* Сначала посылает broadcast [1] на MULTIPLAYER_DEFAULT_PORT, а затем читает
 * server_file (один host:port в строке) и посылает [1] каждому адресу из него.
 * На каждый этап ответа ждутся две секунды. В список входят только строго
 * корректные [1]-пакеты: точный размер, ASCII-имя карты и
 * players <= max_players. Пустой server_file пропускает второй этап. */
void multiplayer_server_browser_init(MultiplayerServerBrowser *browser);
int multiplayer_server_browser_start(MultiplayerServerBrowser *browser,
                                     const char *server_file, double now);
void multiplayer_server_browser_update(MultiplayerServerBrowser *browser, double now);
void multiplayer_server_browser_stop(MultiplayerServerBrowser *browser);
const char *multiplayer_server_browser_status(const MultiplayerServerBrowser *browser);
int multiplayer_server_browser_is_scanning(const MultiplayerServerBrowser *browser);
const MultiplayerServerEntry *multiplayer_server_browser_entries(
    const MultiplayerServerBrowser *browser, int *count);

#endif /* MULTIPLAYER_H */
