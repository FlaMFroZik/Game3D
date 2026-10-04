/* Точка входа game3d-server: разобрать флаги, подготовить модули и
 * отдать управление главному циклу server.c. Вся содержательная
 * работа разложена по модулям:
 *   config.c   — флаги командной строки
 *   socket.c   — UDP-сокет и endpoint'ы
 *   protocol.c — формат датаграмм [1]..[5]
 *   map/       — геометрия карты .tfm
 *   player/    — коллекция игроков и лучевая видимость
 *   commands.c — команды stdin
 *   server.c   — обработчики датаграмм и главный цикл */

/* getpid и sigaction видны при строгом -std=c11 только после feature
 * test macro — до любых системных заголовков. */
#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#  define _POSIX_C_SOURCE 200112L
#endif

#include "commands.h"
#include "config.h"
#include "log.h"
#include "server.h"

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifndef _WIN32
#  include <unistd.h>
#endif

static void on_signal(int signal_number) {
    (void)signal_number;
    /* sig_atomic_t — единственный способ записи из обработчика. */
    server_request_stop();
}

static void install_signal_handlers(void) {
#ifdef _WIN32
    signal(SIGINT, on_signal);
    signal(SIGBREAK, on_signal);
#else
    struct sigaction action;

    memset(&action, 0, sizeof action);
    action.sa_handler = on_signal;
    sigemptyset(&action.sa_mask);
    if (sigaction(SIGINT, &action, NULL) != 0) {
        signal(SIGINT, on_signal);
    }
    if (sigaction(SIGTERM, &action, NULL) != 0) {
        signal(SIGTERM, on_signal);
    }
    /* sendto в «мёртвый» endpoint не должен убивать сервер SIGPIPE. */
    signal(SIGPIPE, SIG_IGN);
#endif
}

/* Seed для col_generate_id: PID и время делают ID разных запусков
 * разными с первого rand(). */
static unsigned random_seed(void) {
#ifdef _WIN32
    const unsigned pid = (unsigned)_getpid();
#else
    const unsigned pid = (unsigned)getpid();
#endif
    return (unsigned)time(NULL) ^ (pid << 16) ^ (unsigned)clock();
}

int main(int argc, char **argv) {
    SrvConfig config;
    SrvServer server;
    int result;

    config_defaults(&config);
    result = config_parse_args(argc, argv, &config);
    if (result == CONFIG_EXIT_OK) return 0;
    if (result == CONFIG_ERROR) {
        fprintf(stderr, "запустите game3d-server --help для списка параметров\n");
        return 1;
    }

    srv_log_init(config.quiet
                     ? SRV_LOG_ERROR
                     : (config.verbose ? SRV_LOG_DEBUG : SRV_LOG_INFO));
    srand(random_seed());

    if (!srv_socket_lib_init()) {
        srv_log(SRV_LOG_ERROR, "не удалось запустить Winsock");
        return 1;
    }

    if (!server_init(&server, &config)) {
        srv_socket_lib_cleanup();
        return 1;
    }

    if (!commands_init()) {
        /* Не фатально: сервер продолжает работать без интерактивных
         * команд, флаги командной строки никуда не деваются. */
        srv_log(SRV_LOG_ERROR, "не удалось запустить поток чтения stdin, "
                "команды недоступны");
    }

    install_signal_handlers();
    server_run(&server);

    server_free(&server);
    srv_socket_lib_cleanup();
    return 0;
}
