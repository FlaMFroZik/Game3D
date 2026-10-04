/* getaddrinfo и прочие POSIX-функции сокетов видны при строгом -std=c11
 * только после feature test macro — до любых системных заголовков. */
#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#  define _POSIX_C_SOURCE 200112L
#endif

#include "socket.h"

#include "log.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>

#ifndef _WIN32
#  include <fcntl.h>
#  include <sys/select.h>
#  include <unistd.h>
#endif

#ifdef _WIN32
static int g_winsock_started = 0;
#endif

/* Одна запись в лог об ошибке сокета: на Windows это код Winsock,
 * на POSIX — текст errno. */
static void srv_socket_log_error(const char *what, int error, int level) {
#ifdef _WIN32
    srv_log(level, "%s: ошибка Winsock %d", what, error);
#else
    (void)error;
    srv_log(level, "%s: %s", what, strerror(errno));
#endif
}

int srv_socket_lib_init(void) {
#ifdef _WIN32
    WSADATA data;
    if (WSAStartup(MAKEWORD(2, 2), &data) != 0) {
        return 0;
    }
    g_winsock_started = 1;
#endif
    return 1;
}

void srv_socket_lib_cleanup(void) {
#ifdef _WIN32
    if (g_winsock_started) {
        WSACleanup();
        g_winsock_started = 0;
    }
#endif
}

static void srv_socket_close_raw(SrvSocket socket_handle) {
#ifdef _WIN32
    closesocket(socket_handle);
#else
    close(socket_handle);
#endif
}

void srv_socket_close(SrvSocket socket_handle) {
    if (socket_handle != SRV_SOCKET_INVALID) {
        srv_socket_close_raw(socket_handle);
    }
}

static int srv_socket_set_nonblocking(SrvSocket socket_handle) {
#ifdef _WIN32
    u_long enabled = 1;
    return ioctlsocket(socket_handle, FIONBIO, &enabled) == 0;
#else
    const int flags = fcntl(socket_handle, F_GETFL, 0);
    if (flags < 0) return 0;
    return fcntl(socket_handle, F_SETFL, flags | O_NONBLOCK) == 0;
#endif
}

void srv_endpoint_format(const SrvEndpoint *endpoint, char *out,
                         size_t out_size) {
    /* NI_NUMERICHOST|NI_NUMERICSERV не ходят в DNS, поэтому цифровым
     * адресам хватит и таких буферов: 45 байт — максимум текстового
     * IPv6, 5 байт — максимум порта. */
    char host[48];
    char service[8];

    if (getnameinfo((const struct sockaddr *)&endpoint->addr, endpoint->len,
                    host, sizeof host, service, sizeof service,
                    NI_NUMERICHOST | NI_NUMERICSERV) == 0) {
        if (endpoint->addr.ss_family == AF_INET6) {
            /* IPv6-адрес сам содержит ':', поэтому берём его в скобки. */
            snprintf(out, out_size, "[%s]:%s", host, service);
        } else {
            snprintf(out, out_size, "%s:%s", host, service);
        }
    } else {
        snprintf(out, out_size, "неизвестный адрес");
    }
}

int srv_endpoint_equal(const SrvEndpoint *a, const SrvEndpoint *b) {
    if (a->len != b->len || a->addr.ss_family != b->addr.ss_family) {
        return 0;
    }

    if (a->addr.ss_family == AF_INET) {
        const struct sockaddr_in *x = (const struct sockaddr_in *)&a->addr;
        const struct sockaddr_in *y = (const struct sockaddr_in *)&b->addr;
        return x->sin_port == y->sin_port &&
               x->sin_addr.s_addr == y->sin_addr.s_addr;
    }
    if (a->addr.ss_family == AF_INET6) {
        const struct sockaddr_in6 *x = (const struct sockaddr_in6 *)&a->addr;
        const struct sockaddr_in6 *y = (const struct sockaddr_in6 *)&b->addr;
        return x->sin6_port == y->sin6_port &&
               x->sin6_scope_id == y->sin6_scope_id &&
               memcmp(&x->sin6_addr, &y->sin6_addr, sizeof x->sin6_addr) == 0;
    }
    return 0;
}

int srv_socket_open(SrvSocket *out, const char *bind_address, int port,
                    char *bound_endpoint, size_t bound_size) {
    struct addrinfo hints;
    struct addrinfo *addresses = NULL;
    struct addrinfo *address;
    char port_text[16];
    const char *node;
    int bound = 0;

    *out = SRV_SOCKET_INVALID;

    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;    /* IPv4 или IPv6 — как скажет --bind */
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_protocol = IPPROTO_UDP;
    hints.ai_flags = AI_PASSIVE;    /* пустой адрес — все интерфейсы */

    snprintf(port_text, sizeof port_text, "%d", port);
    node = (bind_address != NULL && bind_address[0] != '\0') ? bind_address : NULL;

    if (getaddrinfo(node, port_text, &hints, &addresses) != 0) {
        fprintf(stderr, "game3d-server: не удалось разобрать адрес «%s»\n",
                node != NULL ? node : "(любой)");
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
            srv_socket_set_nonblocking(socket_handle)) {
#ifndef _WIN32
            /* select() не принимает дескрипторы >= FD_SETSIZE; у демона,
             * открывшего много файлов, такое возможно. */
            if (socket_handle >= FD_SETSIZE) {
                fprintf(stderr, "game3d-server: дескриптор сокета %d не "
                        "помещается в select()\n", socket_handle);
                srv_socket_close_raw(socket_handle);
                break;
            }
#endif
            *out = socket_handle;
            if (bound_endpoint != NULL) {
                SrvEndpoint endpoint;
                /* Копируем только ai_addrlen байт: addrinfo выделяет ровно
                 * столько, сколько нужно семейству (для IPv4 это 16 байт),
                 * а не весь sockaddr_storage. */
                memset(&endpoint, 0, sizeof endpoint);
                memcpy(&endpoint.addr, address->ai_addr, address->ai_addrlen);
                endpoint.len = address->ai_addrlen;
                srv_endpoint_format(&endpoint, bound_endpoint, bound_size);
            }
            bound = 1;
            break;
        }

        srv_socket_close_raw(socket_handle);
    }

    freeaddrinfo(addresses);

    if (!bound) {
        fprintf(stderr, "game3d-server: не удалось привязать UDP %s:%d\n",
                (bind_address != NULL && bind_address[0] != '\0')
                    ? bind_address : "0.0.0.0",
                port);
    }
    return bound;
}

int srv_socket_receive(SrvSocket socket_handle, unsigned char *buffer,
                       size_t capacity, SrvEndpoint *from) {
    int received;

#ifdef _WIN32
    from->len = sizeof from->addr;
    received = recvfrom(socket_handle, (char *)buffer, (int)capacity, 0,
                        (struct sockaddr *)&from->addr, &from->len);
    if (received == SOCKET_ERROR) {
        const int error = WSAGetLastError();
        /* WSAECONNRESET прилетает на UDP-сокет, если какой-то из прежних
         * адресатов закрыл порт; это не ошибка сервера. */
        if (error == WSAEWOULDBLOCK || error == WSAECONNRESET) return 0;
        srv_socket_log_error("recvfrom", error, SRV_LOG_ERROR);
        return -1;
    }
#else
    from->len = sizeof from->addr;
    received = recvfrom(socket_handle, buffer, capacity, 0,
                        (struct sockaddr *)&from->addr, &from->len);
    if (received < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) return 0;
        if (errno == EINTR) return 0;
        srv_socket_log_error("recvfrom", errno, SRV_LOG_ERROR);
        return -1;
    }
#endif
    return received;
}

int srv_socket_send(SrvSocket socket_handle, const unsigned char *packet,
                    size_t length, const SrvEndpoint *to) {
#ifdef _WIN32
    const int sent = sendto(socket_handle, (const char *)packet, (int)length,
                            0, (const struct sockaddr *)&to->addr,
                            (int)to->len);
    if (sent == SOCKET_ERROR) {
        srv_socket_log_error("sendto", (int)WSAGetLastError(), SRV_LOG_DEBUG);
        return 0;
    }
    return 1;
#else
    const ssize_t sent = sendto(socket_handle, packet, length, 0,
                                (const struct sockaddr *)&to->addr, to->len);
    if (sent < 0) {
        /* Отказ отдельному адресату не должен останавливать сервер. */
        srv_socket_log_error("sendto", errno, SRV_LOG_DEBUG);
        return 0;
    }
    return 1;
#endif
}
