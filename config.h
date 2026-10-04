#ifndef SRV_CONFIG_H
#define SRV_CONFIG_H

/* ------------------------------------------------------------------
 * Конфигурация сервера: разбор флагов командной строки.
 * ------------------------------------------------------------------ */

#include <stdio.h>

#define SRV_VERSION                 "1.1"

#define SRV_DEFAULT_PORT            27015
#define SRV_DEFAULT_NAME            "Game3D Server"
#define SRV_DEFAULT_MAX_PLAYERS     16
#define SRV_DEFAULT_TIMEOUT_SECONDS 30.0

/* Ограничения клиента Game3D (протокол — в README.md). Сервер может
 * отвечать что угодно, но если он укладывается в них сам, его ответы
 * [1] всегда проходят строгую проверку клиента и попадают в список
 * серверов. */
#define SRV_NAME_MAX                63    /* предел имени у клиента, байт */
#define SRV_MAP_MAX                 255   /* предел имени карты у клиента, байт */
#define SRV_MAX_PLAYERS_LIMIT       64    /* сколько записей клиента хватает в [4] */

#define SRV_BIND_MAX                255   /* адрес/hostname для --bind */
#define SRV_PATH_MAX                511   /* путь к файлу карты --map-file */

typedef struct {
    char bind_address[SRV_BIND_MAX + 1];
    char name[SRV_NAME_MAX + 1];
    char map[SRV_MAP_MAX + 1];       /* имя карты для клиента (реклама в [1]) */
    char map_file[SRV_PATH_MAX + 1]; /* .tfm с геометрией для видимости */
    int port;
    int max_players;
    double timeout_seconds;
    int verbose;
    int quiet;
} SrvConfig;

typedef enum {
    CONFIG_OK = 0,      /* сервер можно запускать */
    CONFIG_EXIT_OK,     /* --help или --version: выйти с кодом 0 */
    CONFIG_ERROR        /* ошибка в аргументах: выйти с кодом 1 */
} ConfigParseResult;

void config_defaults(SrvConfig *config);

/* Разбирает argc/argv и выполняет пост-проверки (взаимоисключающие
 * флаги, имя карты по умолчанию из --map-file). */
int config_parse_args(int argc, char **argv, SrvConfig *config);

void config_print_usage(FILE *stream);
void config_print_version(void);

#endif /* SRV_CONFIG_H */
