#ifndef SRV_SOCKET_H
#define SRV_SOCKET_H

/* ------------------------------------------------------------------
 * UDP-сокет сервера: платформенно-независимая обёртка над сокетами
 * POSIX и Winsock. Модуль знает про привязку по адресу/порту (IPv4 и
 * IPv6), неблокирующий режим, приём и отправку датаграмм, а также
 * текстовое представление endpoint'а для логов.
 * ------------------------------------------------------------------ */

#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#  define _POSIX_C_SOURCE 200112L
#endif

#include <stddef.h>

#ifdef _WIN32
#  include <winsock2.h>
#  include <ws2tcpip.h>
typedef SOCKET SrvSocket;
#  define SRV_SOCKET_INVALID INVALID_SOCKET
#else
#  include <netdb.h>
#  include <sys/socket.h>
typedef int SrvSocket;
#  define SRV_SOCKET_INVALID (-1)
#endif

/* Максимальная длина текстового вида endpoint'а: «[IPv6%zone]:порт»
 * с запасом. */
#define SRV_ENDPOINT_TEXT_MAX 96

/* UDP-адрес собеседника: sockaddr любого семейства + его длина. */
typedef struct {
    struct sockaddr_storage addr;
    socklen_t len;
} SrvEndpoint;

/* Инициализация сетевой библиотеки (Winsock на Windows). */
int  srv_socket_lib_init(void);
void srv_socket_lib_cleanup(void);

/* Открывает неблокирующий UDP-сокет, привязанный к bind_address:port.
 * Пустой адрес — все интерфейсы (IPv4), «::» — все интерфейсы IPv6.
 * bound_endpoint получает числовой адрес привязки для лога и status.
 * Возвращает 1 при успехе, 0 при ошибке (сообщение уже в логе). */
int  srv_socket_open(SrvSocket *out, const char *bind_address, int port,
                     char *bound_endpoint, size_t bound_size);
void srv_socket_close(SrvSocket socket);

/* Результат приёма: >0 — размер датаграммы; 0 — пакетов нет (нечего
 * читать или аналог WSAECONNRESET); -1 — ошибка сокета (уже в логе). */
int srv_socket_receive(SrvSocket socket, unsigned char *buffer,
                       size_t capacity, SrvEndpoint *from);

/* Отправляет одну датаграмму. Ошибка отправки одному адресату не
 * фатальна: она уходит в отладочный лог, сервер продолжает работу.
 * Возвращает 1 при успехе, 0 при ошибке. */
int srv_socket_send(SrvSocket socket, const unsigned char *packet,
                    size_t length, const SrvEndpoint *to);

/* Один и тот же ли UDP endpoint (семейство, адрес, порт). */
int  srv_endpoint_equal(const SrvEndpoint *a, const SrvEndpoint *b);

/* Числовое представление: «1.2.3.4:порт» или «[::1]:порт». */
void srv_endpoint_format(const SrvEndpoint *endpoint, char *out,
                         size_t out_size);

#endif /* SRV_SOCKET_H */
