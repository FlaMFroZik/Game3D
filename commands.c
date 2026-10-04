/* pthread виден при строгом -std=c11 только после feature test macro —
 * до любых системных заголовков. */
#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#  define _POSIX_C_SOURCE 200112L
#endif

#include "commands.h"

#include "log.h"
#include "player/col.h"

#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#  include <process.h>
#else
#  include <pthread.h>
#endif

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

static void commands_lock(void) {
#ifdef _WIN32
    EnterCriticalSection(&g_command_lock);
#else
    pthread_mutex_lock(&g_command_lock);
#endif
}

static void commands_unlock(void) {
#ifdef _WIN32
    LeaveCriticalSection(&g_command_lock);
#else
    pthread_mutex_unlock(&g_command_lock);
#endif
}

/* Команда длиннее SRV_COMMAND_MAX байт обрезается; её хвост fgets
 * прочитает следующим куском — это то же поведение, что и разбивка
 * длинной строки на две команды. */
static void commands_copy(char out[SRV_COMMAND_MAX + 1], const char *src) {
    size_t i;

    for (i = 0; i < SRV_COMMAND_MAX && src[i] != '\0'; i++) {
        out[i] = src[i];
    }
    out[i] = '\0';
}

static void commands_push(const char *line) {
    commands_lock();
    if (g_command_count < SRV_COMMAND_QUEUE) {
        commands_copy(g_commands[g_command_count], line);
        g_command_count++;
    }
    /* Переполнение теряется молча: очередь велика, а команды вводит
     * человек, который тут же повторит. */
    commands_unlock();
}

static int commands_pop(char out[SRV_COMMAND_MAX + 1]) {
    int taken = 0;

    commands_lock();
    if (g_command_count > 0) {
        /* out обязан вмещать SRV_COMMAND_MAX + 1 байт: строка лежит в
         * очереди целиком вместе с завершающим нулём. */
        memcpy(out, g_commands[0], SRV_COMMAND_MAX + 1);
        taken = 1;
        g_command_count--;
        if (g_command_count > 0) {
            memmove(g_commands[0], g_commands[1],
                    (size_t)g_command_count * sizeof g_commands[0]);
        }
    }
    commands_unlock();
    return taken;
}

static
#ifdef _WIN32
unsigned __stdcall
#else
void *
#endif
commands_stdin_thread(void *argument) {
    char line[SRV_COMMAND_MAX + 1];

    (void)argument;
    while (fgets(line, sizeof line, stdin) != NULL) {
        line[strcspn(line, "\r\n")] = '\0';
        commands_push(line);
    }
    /* stdin закрыт (сервис без терминала) — это не ошибка, сервер
     * продолжает работать, просто без интерактивных команд. */
    g_stdin_closed = 1;
    return 0;
}

int commands_init(void) {
#ifdef _WIN32
    InitializeCriticalSection(&g_command_lock);
    {
        const uintptr_t thread = _beginthreadex(NULL, 0, commands_stdin_thread,
                                                NULL, 0, NULL);
        if (thread == 0) return 0;
        CloseHandle((HANDLE)thread);
        return 1;
    }
#else
    pthread_t thread;
    if (pthread_create(&thread, NULL, commands_stdin_thread, NULL) != 0) {
        return 0;
    }
    pthread_detach(thread);
    return 1;
#endif
}

/* ------------------------------------------------------------------ */
/* Выполнение команд                                                  */
/* ------------------------------------------------------------------ */

static void commands_print_help(void) {
    printf("команды: help, status, list, kick <id>, quit\n");
    fflush(stdout);
}

static void commands_print_status(SrvServer *server) {
    char uptime[64];

    server_uptime_text(server, uptime, sizeof uptime);
    printf("аптайм      %s\n", uptime);
    printf("адрес       %s\n", server->bound_endpoint);
    printf("имя         %s\n", server->config.name);
    printf("карта       %s\n",
           server->config.map[0] != '\0'
               ? server->config.map
               : "(не задана: клиент оставляет свою)");
    printf("геометрия   %s\n",
           server->map_loaded
               ? server->config.map_file
               : "(нет: [4] возвращает всех игроков)");
    printf("игроки      %d/%d\n", server->players.count,
           server->config.max_players);
    printf("тайм-аут    %.0f с\n", server->config.timeout_seconds);
    printf("tick        %" PRIu32 "\n", server_tick(server));
    if (g_stdin_closed) {
        printf("stdin       закрыт, команды недоступны\n");
    }
    fflush(stdout);
}

static void commands_print_list(SrvServer *server) {
    const double now = server_monotonic_seconds();
    int i;
    int printed = 0;

    for (i = 0; i < server->players.capacity; i++) {
        const SrvPlayer *player = &server->players.slots[i];
        if (!player->active) continue;

        if (player->has_state) {
            printf("%-10" PRIu32 "  %-22s  (%8.2f %8.2f %8.2f)  простой %.1f с\n",
                   player->id, player->endpoint_text,
                   (double)player->x, (double)player->y, (double)player->z,
                   now - player->last_seen);
        } else {
            printf("%-10" PRIu32 "  %-22s  (состояния ещё нет)       простой "
                   "%.1f с\n", player->id, player->endpoint_text,
                   now - player->last_seen);
        }
        printed++;
    }

    if (printed == 0) printf("нет подключённых игроков\n");
    fflush(stdout);
}

static void commands_kick(SrvServer *server, const char *argument) {
    char *end = NULL;
    unsigned long value;
    SrvPlayer *player;

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

    player = col_find_id(&server->players, (uint32_t)value);
    if (player == NULL) {
        printf("kick: игрок с id %lu не найден\n", value);
        fflush(stdout);
        return;
    }

    /* Поля игрока не стираются: id и адрес нужны для записи в лог. */
    col_remove(&server->players, player);
    srv_log(SRV_LOG_INFO, "kick: игрок id=%" PRIu32 " отключён (%s) (%d/%d)",
            player->id, player->endpoint_text, server->players.count,
            server->config.max_players);
}

static void commands_execute(SrvServer *server, char *line) {
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
        commands_print_help();
        return;
    }
    if (strcmp(line, "status") == 0) {
        commands_print_status(server);
        return;
    }
    if (strcmp(line, "list") == 0) {
        commands_print_list(server);
        return;
    }
    if (strcmp(line, "kick") == 0) {
        commands_kick(server, argument);
        return;
    }
    if (strcmp(line, "quit") == 0 || strcmp(line, "exit") == 0 ||
        strcmp(line, "shutdown") == 0) {
        server_request_stop();
        return;
    }

    printf("неизвестная команда «%s» — help покажет список\n", line);
    fflush(stdout);
}

void commands_poll(SrvServer *server) {
    char command[SRV_COMMAND_MAX + 1];

    while (commands_pop(command)) {
        commands_execute(server, command);
    }
}
