/* getaddrinfo в glibc при строгом -std=c11 виден только после POSIX feature
 * test macro. Он должен быть задан до любого системного заголовка. */
#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#  define _POSIX_C_SOURCE 200112L
#endif

#include "network/multiplayer.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>

#ifdef _WIN32
#  include <winsock2.h>
#  include <ws2tcpip.h>
#  define MP_SOCKET_INVALID ((uintptr_t)INVALID_SOCKET)
#  define mp_socket_error() WSAGetLastError()
#  define MP_WOULD_BLOCK(error) ((error) == WSAEWOULDBLOCK)
#else
#  include <arpa/inet.h>
#  include <fcntl.h>
#  include <netdb.h>
#  include <sys/socket.h>
#  include <unistd.h>
#  define MP_SOCKET_INVALID ((uintptr_t)-1)
#  define mp_socket_error() errno
#  define MP_WOULD_BLOCK(error) ((error) == EAGAIN || (error) == EWOULDBLOCK)
#endif

/* У UDP нет гарантии доставки, поэтому запрос сведений и вход повторяются
 * несколько раз. На сервере повторный [2] от того же endpoint должен
 * возвращать уже назначенный ID, а не создавать второго игрока. */
#define MP_RETRY_SECONDS 0.75
#define MP_MAX_ATTEMPTS  5
#define MP_PLAYERS_POLL_SECONDS 0.10
#define MP_DISCOVERY_SECONDS    2.00

#define MP_PACKET_INFO       1u
#define MP_PACKET_JOIN       2u
#define MP_PACKET_TRANSFORM  3u
#define MP_PACKET_PLAYERS    4u
#define MP_PACKET_LEAVE      5u

static void set_status(MultiplayerClient *client, const char *text) {
    snprintf(client->status, sizeof client->status, "%s", text);
}

static void set_browser_status(MultiplayerServerBrowser *browser, const char *text) {
    snprintf(browser->status, sizeof browser->status, "%s", text);
}

static void close_socket(MultiplayerClient *client) {
    if (client->socket_open) {
#ifdef _WIN32
        closesocket((SOCKET)client->socket_handle);
#else
        close((int)client->socket_handle);
#endif
    }
    client->socket_handle = MP_SOCKET_INVALID;
    client->socket_open = 0;

#ifdef _WIN32
    if (client->winsock_started) {
        WSACleanup();
        client->winsock_started = 0;
    }
#endif
}

static void close_browser_socket(MultiplayerServerBrowser *browser) {
    if (browser->socket_open) {
#ifdef _WIN32
        closesocket((SOCKET)browser->socket_handle);
#else
        close((int)browser->socket_handle);
#endif
    }
    browser->socket_handle = MP_SOCKET_INVALID;
    browser->socket_open = 0;

#ifdef _WIN32
    if (browser->winsock_started) {
        WSACleanup();
        browser->winsock_started = 0;
    }
#endif
}

static int set_nonblocking(uintptr_t socket_handle) {
#ifdef _WIN32
    u_long enabled = 1;
    return ioctlsocket((SOCKET)socket_handle, FIONBIO, &enabled) == 0;
#else
    const int fd = (int)socket_handle;
    const int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) return 0;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0;
#endif
}

static int send_packet(MultiplayerClient *client, const unsigned char *packet,
                       size_t length) {
    if (!client->socket_open) return 0;

#ifdef _WIN32
    const int sent = send((SOCKET)client->socket_handle, (const char *)packet,
                          (int)length, 0);
    if (sent == SOCKET_ERROR || (size_t)sent != length) {
#else
    const ssize_t sent = send((int)client->socket_handle, packet, length, 0);
    if (sent < 0 || (size_t)sent != length) {
#endif
        set_status(client, "ОШИБКА ОТПРАВКИ UDP");
        return 0;
    }
    return 1;
}

static void write_u32_be(unsigned char *out, uint32_t value) {
    out[0] = (unsigned char)(value >> 24);
    out[1] = (unsigned char)(value >> 16);
    out[2] = (unsigned char)(value >> 8);
    out[3] = (unsigned char)value;
}

static uint32_t read_u32_be(const unsigned char *in) {
    return ((uint32_t)in[0] << 24) |
           ((uint32_t)in[1] << 16) |
           ((uint32_t)in[2] << 8) |
           (uint32_t)in[3];
}

static void write_f32_be(unsigned char *out, float value) {
    uint32_t bits = 0;
    memcpy(&bits, &value, sizeof bits);
    write_u32_be(out, bits);
}

static float read_f32_be(const unsigned char *in) {
    const uint32_t bits = read_u32_be(in);
    float value = 0.0f;
    memcpy(&value, &bits, sizeof value);
    return value;
}

/* Разбирает host:port. IPv6 с портом записывается как [::1]:27015; IPv6 без
 * скобок тоже принимается и получает порт по умолчанию. */
static int split_endpoint(const char *endpoint, char *host, size_t host_size,
                          char *port, size_t port_size) {
    const char *default_port = MULTIPLAYER_DEFAULT_PORT;
    const char *port_start = default_port;
    size_t host_length = 0;

    if (!endpoint || endpoint[0] == '\0') return 0;

    if (endpoint[0] == '[') {
        const char *closing = strchr(endpoint, ']');
        if (!closing || closing == endpoint + 1) return 0;
        host_length = (size_t)(closing - endpoint - 1);
        if (closing[1] != '\0') {
            if (closing[1] != ':' || closing[2] == '\0') return 0;
            port_start = closing + 2;
        }
        memcpy(host, endpoint + 1, host_length);
    } else {
        const char *last_colon = strrchr(endpoint, ':');
        const char *first_colon = strchr(endpoint, ':');
        if (last_colon && last_colon == first_colon && last_colon[1] != '\0') {
            host_length = (size_t)(last_colon - endpoint);
            port_start = last_colon + 1;
        } else {
            host_length = strlen(endpoint);
        }
        if (host_length == 0) return 0;
        memcpy(host, endpoint, host_length);
    }

    if (host_length >= host_size || strlen(port_start) >= port_size) return 0;
    host[host_length] = '\0';
    snprintf(port, port_size, "%s", port_start);
    return 1;
}

static int open_udp_socket(MultiplayerClient *client, const char *endpoint) {
    char host[MULTIPLAYER_ENDPOINT_MAX + 1];
    char port[16];
    struct addrinfo hints;
    struct addrinfo *addresses = NULL;
    struct addrinfo *address;

    if (!split_endpoint(endpoint, host, sizeof host, port, sizeof port)) {
        set_status(client, "НЕВЕРНЫЙ АДРЕС СЕРВЕРА");
        return 0;
    }

#ifdef _WIN32
    WSADATA data;
    if (WSAStartup(MAKEWORD(2, 2), &data) != 0) {
        set_status(client, "НЕ УДАЛОСЬ ЗАПУСТИТЬ WINSOCK");
        return 0;
    }
    client->winsock_started = 1;
#endif

    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_protocol = IPPROTO_UDP;

    if (getaddrinfo(host, port, &hints, &addresses) != 0) {
        set_status(client, "АДРЕС СЕРВЕРА НЕ НАЙДЕН");
        close_socket(client);
        return 0;
    }

    for (address = addresses; address; address = address->ai_next) {
#ifdef _WIN32
        SOCKET socket_handle = socket(address->ai_family, address->ai_socktype,
                                      address->ai_protocol);
        if (socket_handle == INVALID_SOCKET) continue;
        if (connect(socket_handle, address->ai_addr, (int)address->ai_addrlen) != 0) {
            closesocket(socket_handle);
            continue;
        }
        client->socket_handle = (uintptr_t)socket_handle;
#else
        const int socket_handle = socket(address->ai_family, address->ai_socktype,
                                         address->ai_protocol);
        if (socket_handle < 0) continue;
        if (connect(socket_handle, address->ai_addr, address->ai_addrlen) != 0) {
            close(socket_handle);
            continue;
        }
        client->socket_handle = (uintptr_t)socket_handle;
#endif
        client->socket_open = 1;
        if (!set_nonblocking(client->socket_handle)) {
            /* Не вызываем close_socket: на Windows он сделал бы WSACleanup,
             * хотя следующий адрес из getaddrinfo ещё можно попробовать. */
#ifdef _WIN32
            closesocket((SOCKET)client->socket_handle);
#else
            close((int)client->socket_handle);
#endif
            client->socket_handle = MP_SOCKET_INVALID;
            client->socket_open = 0;
            continue;
        }
        break;
    }

    freeaddrinfo(addresses);

    if (!client->socket_open) {
        set_status(client, "НЕ УДАЛОСЬ ОТКРЫТЬ UDP-СОКЕТ");
        return 0;
    }
    return 1;
}

static void send_info_request(MultiplayerClient *client, double now) {
    const unsigned char packet[] = {MP_PACKET_INFO};
    (void)send_packet(client, packet, sizeof packet);
    /* Даже локальная ошибка send не должна превратить ожидание в бесконечный
     * вызов send на каждом кадре. */
    client->last_request_at = now;
    client->request_attempts++;
}

static void send_join_request(MultiplayerClient *client, double now) {
    const unsigned char packet[] = {MP_PACKET_JOIN};
    (void)send_packet(client, packet, sizeof packet);
    client->last_request_at = now;
    client->request_attempts++;
}

/* name может быть UTF-8, а map для каталога серверов сознательно ограничен
 * печатным ASCII: это имя локального файла, которое должно быть одинаково на
 * разных клиентах. Строка длиннее поля не считается корректной, а не молча
 * обрезается. */
static int copy_zero_terminated(char *out, size_t out_size,
                                const unsigned char *packet, size_t length,
                                size_t *offset, int printable_ascii) {
    const size_t start = *offset;
    size_t end = start;
    size_t copy_length;

    while (end < length && packet[end] != 0) {
        if (printable_ascii && (packet[end] < 0x20 || packet[end] > 0x7e)) {
            return 0;
        }
        end++;
    }
    if (end == length) return 0;

    copy_length = end - start;
    if (copy_length >= out_size) return 0;
    memcpy(out, packet + start, copy_length);
    out[copy_length] = '\0';
    *offset = end + 1;
    return 1;
}

/* Строгая проверка [1]. Она одновременно проверяет точный размер дейтаграммы:
 * после завершающего нуля map не допускается ни одного байта. */
static int parse_info_packet(const unsigned char *packet, size_t length,
                             MultiplayerServerInfo *info) {
    size_t offset = 7;

    if (!packet || !info || length < offset || packet[0] != MP_PACKET_INFO ||
        !copy_zero_terminated(info->name, sizeof info->name,
                              packet, length, &offset, 0) ||
        !copy_zero_terminated(info->map, sizeof info->map,
                              packet, length, &offset, 1) ||
        offset != length || packet[2] > packet[1]) {
        return 0;
    }

    info->max_players = packet[1];
    info->players = packet[2];
    info->tick = read_u32_be(packet + 3);
    return 1;
}

static void receive_info(MultiplayerClient *client, const unsigned char *packet,
                         size_t length, double now) {
    MultiplayerServerInfo info;

    /* [1][max_players:u8][players:u8][tick:u32][name:\0][map:\0] */
    if (!parse_info_packet(packet, length, &info)) {
        set_status(client, "НЕВЕРНЫЙ ОТВЕТ [1] СЕРВЕРА");
        client->state = MULTIPLAYER_ERROR;
        return;
    }

    client->info = info;
    client->state = MULTIPLAYER_WAIT_JOIN;
    client->request_attempts = 0;
    set_status(client, "ЗАПРОС МЕСТА НА СЕРВЕРЕ");
    send_join_request(client, now);
}

static void receive_join(MultiplayerClient *client, const unsigned char *packet,
                         size_t length) {
    if (length == 1) {
        client->state = MULTIPLAYER_FULL;
        set_status(client, "СЕРВЕР ПОЛОН");
        return;
    }
    if (length != 5) {
        client->state = MULTIPLAYER_ERROR;
        set_status(client, "НЕВЕРНЫЙ ОТВЕТ [2] СЕРВЕРА");
        return;
    }

    client->player_id = read_u32_be(packet + 1);
    client->joined = 1;
    client->state = MULTIPLAYER_READY;
    client->next_players_request_at = 0.0;
    set_status(client, "ПОДКЛЮЧЕНО ПО UDP");
}

static void receive_visible_players(MultiplayerClient *client,
                                    const unsigned char *packet, size_t length) {
    size_t offset = 1;
    int count = 0;

    /* Каждая запись имеет фиксированные 24 байта f32. [0] нужен между
     * соседними записями; завершающий [0] после последней также принимается.
     * Нельзя искать разделитель до записи: первый байт корректного float
     * (например x = 0) тоже может быть нулём. */
    while (offset < length) {
        MultiplayerRemotePlayer *player;
        if (length - offset < 24) {
            set_status(client, "НЕВЕРНЫЙ ОТВЕТ [4] СЕРВЕРА");
            return;
        }
        if (count >= MULTIPLAYER_MAX_REMOTE_PLAYERS) {
            /* Остальные записи пакета не нужны клиенту, но это не ошибка. */
            break;
        }

        player = &client->remote_players[count];
        player->x = read_f32_be(packet + offset); offset += 4;
        player->y = read_f32_be(packet + offset); offset += 4;
        player->z = read_f32_be(packet + offset); offset += 4;
        player->look_x = read_f32_be(packet + offset); offset += 4;
        player->look_y = read_f32_be(packet + offset); offset += 4;
        player->look_z = read_f32_be(packet + offset); offset += 4;
        count++;

        if (offset == length) break;
        if (packet[offset] != 0) {
            set_status(client, "НЕВЕРНЫЙ РАЗДЕЛИТЕЛЬ [4]");
            return;
        }
        offset++;
    }
    client->remote_player_count = count;
}

static void receive_packet(MultiplayerClient *client, const unsigned char *packet,
                           size_t length, double now) {
    if (length == 0) return;

    switch (packet[0]) {
        case MP_PACKET_INFO:
            if (client->state == MULTIPLAYER_WAIT_INFO) {
                receive_info(client, packet, length, now);
            }
            break;
        case MP_PACKET_JOIN:
            if (client->state == MULTIPLAYER_WAIT_JOIN) {
                receive_join(client, packet, length);
            }
            break;
        case MP_PACKET_PLAYERS:
            if (client->joined) receive_visible_players(client, packet, length);
            break;
        case MP_PACKET_TRANSFORM:
            /* [3] — подтверждение последней позиции; дополнительного тела нет. */
            break;
        default:
            /* Неизвестные пакеты от подключённого UDP endpoint игнорируются. */
            break;
    }
}

void multiplayer_init(MultiplayerClient *client) {
    memset(client, 0, sizeof *client);
    client->socket_handle = MP_SOCKET_INVALID;
    client->state = MULTIPLAYER_IDLE;
    set_status(client, "НЕ ПОДКЛЮЧЕНО");
}

int multiplayer_connect(MultiplayerClient *client, const char *endpoint,
                        double now) {
    multiplayer_disconnect(client);
    client->remote_player_count = 0;
    memset(&client->info, 0, sizeof client->info);
    client->player_id = 0;

    if (!endpoint || strlen(endpoint) > MULTIPLAYER_ENDPOINT_MAX) {
        client->state = MULTIPLAYER_ERROR;
        set_status(client, "НЕВЕРНЫЙ АДРЕС СЕРВЕРА");
        return 0;
    }
    snprintf(client->endpoint, sizeof client->endpoint, "%s", endpoint);

    if (!open_udp_socket(client, endpoint)) {
        client->state = MULTIPLAYER_ERROR;
        return 0;
    }

    client->state = MULTIPLAYER_WAIT_INFO;
    client->request_attempts = 0;
    set_status(client, "ОЖИДАНИЕ ОТВЕТА СЕРВЕРА");
    send_info_request(client, now);
    return 1;
}

void multiplayer_update(MultiplayerClient *client, double now) {
    unsigned char packet[2048];

    if (!client->socket_open) return;

    for (;;) {
#ifdef _WIN32
        const int received = recv((SOCKET)client->socket_handle, (char *)packet,
                                  (int)sizeof packet, 0);
        if (received == SOCKET_ERROR) {
            const int error = mp_socket_error();
            if (MP_WOULD_BLOCK(error)) break;
            set_status(client, "ОШИБКА ПРИЁМА UDP");
            client->state = MULTIPLAYER_ERROR;
            break;
        }
#else
        const ssize_t received = recv((int)client->socket_handle, packet,
                                      sizeof packet, 0);
        if (received < 0) {
            const int error = mp_socket_error();
            if (MP_WOULD_BLOCK(error)) break;
            set_status(client, "ОШИБКА ПРИЁМА UDP");
            client->state = MULTIPLAYER_ERROR;
            break;
        }
#endif
        receive_packet(client, packet, (size_t)received, now);
    }

    if (client->state != MULTIPLAYER_WAIT_INFO &&
        client->state != MULTIPLAYER_WAIT_JOIN) {
        return;
    }
    if (now - client->last_request_at < MP_RETRY_SECONDS) return;

    if (client->request_attempts >= MP_MAX_ATTEMPTS) {
        client->state = MULTIPLAYER_TIMEOUT;
        set_status(client, "СЕРВЕР НЕ ОТВЕТИЛ");
        return;
    }

    if (client->state == MULTIPLAYER_WAIT_INFO) {
        send_info_request(client, now);
    } else {
        send_join_request(client, now);
    }
}

void multiplayer_send_transform(MultiplayerClient *client,
                                float x, float y, float z,
                                float look_x, float look_y, float look_z) {
    unsigned char packet[25];

    if (!client->joined || !client->socket_open) return;

    packet[0] = MP_PACKET_TRANSFORM;
    write_f32_be(packet + 1,  x);
    write_f32_be(packet + 5,  y);
    write_f32_be(packet + 9,  z);
    write_f32_be(packet + 13, look_x);
    write_f32_be(packet + 17, look_y);
    write_f32_be(packet + 21, look_z);
    (void)send_packet(client, packet, sizeof packet);
}

void multiplayer_request_visible_players(MultiplayerClient *client, double now) {
    const unsigned char packet[] = {MP_PACKET_PLAYERS};

    if (!client->joined || !client->socket_open ||
        now < client->next_players_request_at) {
        return;
    }
    if (send_packet(client, packet, sizeof packet)) {
        client->next_players_request_at = now + MP_PLAYERS_POLL_SECONDS;
    }
}

void multiplayer_disconnect(MultiplayerClient *client) {
    const unsigned char packet[] = {MP_PACKET_LEAVE};

    if (client->joined && client->socket_open) {
        /* Требуемый протоколом [5]. Сервер узнаёт сессию по UDP endpoint. */
        (void)send_packet(client, packet, sizeof packet);
    }
    close_socket(client);
    client->joined = 0;
    client->player_id = 0;
    client->remote_player_count = 0;
    client->state = MULTIPLAYER_IDLE;
    set_status(client, "НЕ ПОДКЛЮЧЕНО");
}

int multiplayer_is_joined(const MultiplayerClient *client) {
    return client->joined;
}

const char *multiplayer_status(const MultiplayerClient *client) {
    return client->status;
}

const MultiplayerServerInfo *multiplayer_server_info(const MultiplayerClient *client) {
    return &client->info;
}

const MultiplayerRemotePlayer *multiplayer_remote_players(const MultiplayerClient *client,
                                                           int *count) {
    if (count) *count = client->remote_player_count;
    return client->remote_players;
}

/* ---------- Поиск серверов в локальной сети ---------- */

#define MP_DISCOVERY_PORT 27015
#define MP_DISCOVERY_PHASE_LAN  0
#define MP_DISCOVERY_PHASE_FILE 1

static void browser_finish(MultiplayerServerBrowser *browser) {
    char text[128];

    close_browser_socket(browser);
    browser->state = MULTIPLAYER_DISCOVERY_FINISHED;
    snprintf(text, sizeof text, "НАЙДЕНО СЕРВЕРОВ: %d", browser->server_count);
    set_browser_status(browser, text);
}

static void browser_add_server(MultiplayerServerBrowser *browser,
                               const char *endpoint,
                               const MultiplayerServerInfo *info) {
    for (int i = 0; i < browser->server_count; i++) {
        MultiplayerServerEntry *entry = &browser->servers[i];
        if (strcmp(entry->endpoint, endpoint) == 0) {
            entry->info = *info;
            return;
        }
    }

    if (browser->server_count >= MULTIPLAYER_MAX_SERVERS) return;

    MultiplayerServerEntry *entry = &browser->servers[browser->server_count++];
    snprintf(entry->endpoint, sizeof entry->endpoint, "%s", endpoint);
    entry->info = *info;
}

/* Отдельная строка файла — конкретный кандидат. Ответы всё равно проходят
 * ту же строгую parse_info_packet-проверку, что и ответы broadcast. */
static int browser_query_endpoint(MultiplayerServerBrowser *browser,
                                  const char *endpoint) {
    const unsigned char packet[] = {MP_PACKET_INFO};
    char host[MULTIPLAYER_ENDPOINT_MAX + 1];
    char port[16];
    struct addrinfo hints;
    struct addrinfo *addresses = NULL;
    int sent_any = 0;

    if (!split_endpoint(endpoint, host, sizeof host, port, sizeof port)) return 0;

    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_INET;  /* browser использует один IPv4 broadcast-сокет */
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_protocol = IPPROTO_UDP;
    if (getaddrinfo(host, port, &hints, &addresses) != 0) return 0;

    for (struct addrinfo *address = addresses; address; address = address->ai_next) {
#ifdef _WIN32
        const int sent = sendto((SOCKET)browser->socket_handle, (const char *)packet,
                                (int)sizeof packet, 0, address->ai_addr,
                                (int)address->ai_addrlen);
        if (sent == (int)sizeof packet) sent_any = 1;
#else
        const ssize_t sent = sendto((int)browser->socket_handle, packet, sizeof packet, 0,
                                    address->ai_addr, address->ai_addrlen);
        if (sent == (ssize_t)sizeof packet) sent_any = 1;
#endif
    }

    freeaddrinfo(addresses);
    return sent_any;
}

static char *trim_server_line(char *text) {
    char *end;

    while (*text == ' ' || *text == '\t' || *text == '\r' || *text == '\n') text++;
    end = text + strlen(text);
    while (end > text) {
        const char c = end[-1];
        if (c != ' ' && c != '\t' && c != '\r' && c != '\n') break;
        *--end = '\0';
    }
    return text;
}

static int browser_query_file(MultiplayerServerBrowser *browser) {
    FILE *file;
    char line[MULTIPLAYER_SERVER_FILE_MAX + 2];
    int count = 0;

    if (browser->server_file[0] == '\0') return 0;
    file = fopen(browser->server_file, "rb");
    if (!file) return 0;

    while (fgets(line, sizeof line, file)) {
        char *comment = strchr(line, '#');
        char *endpoint;
        size_t length;

        /* Не принимаем оборванную строку как частичный адрес. */
        if (!strchr(line, '\n') && !feof(file)) {
            int c;
            while ((c = fgetc(file)) != '\n' && c != EOF) { }
            continue;
        }
        if (comment) *comment = '\0';
        endpoint = trim_server_line(line);
        length = strlen(endpoint);
        if (length == 0 || length > MULTIPLAYER_ENDPOINT_MAX) continue;
        if (browser_query_endpoint(browser, endpoint)) count++;
    }

    fclose(file);
    return count;
}

static void browser_begin_file_queries(MultiplayerServerBrowser *browser, double now) {
    char text[128];

    browser->phase = MP_DISCOVERY_PHASE_FILE;
    browser->file_query_count = browser_query_file(browser);
    if (browser->file_query_count == 0) {
        browser_finish(browser);
        return;
    }

    browser->finish_at = now + MP_DISCOVERY_SECONDS;
    snprintf(text, sizeof text, "ПРОВЕРКА СЕРВЕРОВ ИЗ ФАЙЛА: %d",
             browser->file_query_count);
    set_browser_status(browser, text);
}

void multiplayer_server_browser_init(MultiplayerServerBrowser *browser) {
    memset(browser, 0, sizeof *browser);
    browser->socket_handle = MP_SOCKET_INVALID;
    browser->state = MULTIPLAYER_DISCOVERY_IDLE;
    set_browser_status(browser, "ПОИСК НЕ ЗАПУЩЕН");
}

int multiplayer_server_browser_start(MultiplayerServerBrowser *browser,
                                     const char *server_file, double now) {
    const unsigned char packet[] = {MP_PACKET_INFO};
    struct sockaddr_in destination;
    int broadcast = 1;

    if (server_file && strlen(server_file) > MULTIPLAYER_SERVER_FILE_MAX) {
        browser->state = MULTIPLAYER_DISCOVERY_ERROR;
        set_browser_status(browser, "СЛИШКОМ ДЛИННЫЙ ПУТЬ К СПИСКУ СЕРВЕРОВ");
        return 0;
    }

    close_browser_socket(browser);
    browser->server_count = 0;
    browser->file_query_count = 0;
    browser->phase = MP_DISCOVERY_PHASE_LAN;
    snprintf(browser->server_file, sizeof browser->server_file, "%s",
             server_file ? server_file : "");

#ifdef _WIN32
    WSADATA data;
    if (WSAStartup(MAKEWORD(2, 2), &data) != 0) {
        browser->state = MULTIPLAYER_DISCOVERY_ERROR;
        set_browser_status(browser, "НЕ УДАЛОСЬ ЗАПУСТИТЬ WINSOCK");
        return 0;
    }
    browser->winsock_started = 1;
#endif

#ifdef _WIN32
    const SOCKET socket_handle = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (socket_handle == INVALID_SOCKET) {
        browser_finish(browser);
        browser->state = MULTIPLAYER_DISCOVERY_ERROR;
        set_browser_status(browser, "НЕ УДАЛОСЬ ОТКРЫТЬ UDP-СОКЕТ");
        return 0;
    }
    browser->socket_handle = (uintptr_t)socket_handle;
#else
    const int socket_handle = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (socket_handle < 0) {
        browser->state = MULTIPLAYER_DISCOVERY_ERROR;
        set_browser_status(browser, "НЕ УДАЛОСЬ ОТКРЫТЬ UDP-СОКЕТ");
        return 0;
    }
    browser->socket_handle = (uintptr_t)socket_handle;
#endif
    browser->socket_open = 1;

#ifdef _WIN32
    if (setsockopt((SOCKET)browser->socket_handle, SOL_SOCKET, SO_BROADCAST,
                   (const char *)&broadcast, (int)sizeof broadcast) != 0) {
#else
    if (setsockopt((int)browser->socket_handle, SOL_SOCKET, SO_BROADCAST,
                   &broadcast, sizeof broadcast) != 0) {
#endif
        close_browser_socket(browser);
        browser->state = MULTIPLAYER_DISCOVERY_ERROR;
        set_browser_status(browser, "НЕЛЬЗЯ ВКЛЮЧИТЬ UDP BROADCAST");
        return 0;
    }
    if (!set_nonblocking(browser->socket_handle)) {
        close_browser_socket(browser);
        browser->state = MULTIPLAYER_DISCOVERY_ERROR;
        set_browser_status(browser, "НЕЛЬЗЯ НАСТРОИТЬ UDP-СОКЕТ");
        return 0;
    }

    memset(&destination, 0, sizeof destination);
    destination.sin_family = AF_INET;
    destination.sin_port = htons(MP_DISCOVERY_PORT);
    destination.sin_addr.s_addr = htonl(INADDR_BROADCAST);

#ifdef _WIN32
    if (sendto((SOCKET)browser->socket_handle, (const char *)packet,
               (int)sizeof packet, 0, (const struct sockaddr *)&destination,
               (int)sizeof destination) != (int)sizeof packet) {
#else
    if (sendto((int)browser->socket_handle, packet, sizeof packet, 0,
               (const struct sockaddr *)&destination, sizeof destination) !=
        (ssize_t)sizeof packet) {
#endif
        close_browser_socket(browser);
        browser->state = MULTIPLAYER_DISCOVERY_ERROR;
        set_browser_status(browser, "ОШИБКА ОТПРАВКИ UDP BROADCAST");
        return 0;
    }

    browser->finish_at = now + MP_DISCOVERY_SECONDS;
    browser->state = MULTIPLAYER_DISCOVERY_SCANNING;
    set_browser_status(browser, "ПОИСК СЕРВЕРОВ В ЛОКАЛЬНОЙ СЕТИ...");
    return 1;
}

void multiplayer_server_browser_update(MultiplayerServerBrowser *browser, double now) {
    unsigned char packet[2048];

    if (browser->state != MULTIPLAYER_DISCOVERY_SCANNING ||
        !browser->socket_open) {
        return;
    }

    for (;;) {
        struct sockaddr_in source;
        char address[INET_ADDRSTRLEN];
        char endpoint[MULTIPLAYER_ENDPOINT_MAX + 1];
        MultiplayerServerInfo info;
#ifdef _WIN32
        int source_length = (int)sizeof source;
        const int received = recvfrom((SOCKET)browser->socket_handle, (char *)packet,
                                      (int)sizeof packet, 0,
                                      (struct sockaddr *)&source, &source_length);
        if (received == SOCKET_ERROR) {
#else
        socklen_t source_length = (socklen_t)sizeof source;
        const ssize_t received = recvfrom((int)browser->socket_handle, packet,
                                          sizeof packet, 0,
                                          (struct sockaddr *)&source, &source_length);
        if (received < 0) {
#endif
            const int error = mp_socket_error();
            if (MP_WOULD_BLOCK(error)) break;
            close_browser_socket(browser);
            browser->state = MULTIPLAYER_DISCOVERY_ERROR;
            set_browser_status(browser, "ОШИБКА ПРИЁМА UDP BROADCAST");
            return;
        }

        if (received == 0 || !parse_info_packet(packet, (size_t)received, &info)) {
            /* Пакет не является строго корректным ответом [1]. */
            continue;
        }
        if (!inet_ntop(AF_INET, &source.sin_addr, address, sizeof address)) {
            continue;
        }
        snprintf(endpoint, sizeof endpoint, "%s:%u", address,
                 (unsigned int)ntohs(source.sin_port));
        browser_add_server(browser, endpoint, &info);
    }

    if (now >= browser->finish_at) {
        if (browser->phase == MP_DISCOVERY_PHASE_LAN) {
            browser_begin_file_queries(browser, now);
        } else {
            browser_finish(browser);
        }
    }
}

void multiplayer_server_browser_stop(MultiplayerServerBrowser *browser) {
    close_browser_socket(browser);
    browser->state = MULTIPLAYER_DISCOVERY_IDLE;
    set_browser_status(browser, "ПОИСК НЕ ЗАПУЩЕН");
}

const char *multiplayer_server_browser_status(const MultiplayerServerBrowser *browser) {
    return browser->status;
}

int multiplayer_server_browser_is_scanning(const MultiplayerServerBrowser *browser) {
    return browser->state == MULTIPLAYER_DISCOVERY_SCANNING;
}

const MultiplayerServerEntry *multiplayer_server_browser_entries(
    const MultiplayerServerBrowser *browser, int *count) {
    if (count) *count = browser->server_count;
    return browser->servers;
}
