#include "config.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>

void config_defaults(SrvConfig *config) {
    memset(config, 0, sizeof *config);
    snprintf(config->name, sizeof config->name, "%s", SRV_DEFAULT_NAME);
    config->port = SRV_DEFAULT_PORT;
    config->max_players = SRV_DEFAULT_MAX_PLAYERS;
    config->timeout_seconds = SRV_DEFAULT_TIMEOUT_SECONDS;
}

void config_print_usage(FILE *stream) {
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
        "  -f, --map-file <файл>    загрузить .tfm с геометрией: [4] возвращает\n"
        "                           только игроков, до которых дошёл луч;\n"
        "                           без файла видны все. Имя карты в [1] по\n"
        "                           умолчанию — имя этого файла\n"
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
        "  game3d-server --map-file arena.tfm -v\n"
        "  game3d-server -b 192.168.1.40 -p 28000\n",
        SRV_VERSION, SRV_DEFAULT_PORT, SRV_NAME_MAX, SRV_DEFAULT_NAME,
        SRV_MAP_MAX, SRV_MAX_PLAYERS_LIMIT, SRV_DEFAULT_MAX_PLAYERS,
        SRV_DEFAULT_TIMEOUT_SECONDS);
}

void config_print_version(void) {
    printf("game3d-server %s\n", SRV_VERSION);
    printf("протокол многопользовательской игры Game3D — см. README.md\n");
}

/* ---------- Валидация значений ---------- */

/* Имя — UTF-8, поэтому ограничение в байтах, а не в символах: клиент
 * проверяет именно длину в байтах. */
static int config_valid_name(const char *text) {
    return strlen(text) <= SRV_NAME_MAX;
}

/* Карта должна быть печатным ASCII: это имя локального файла клиента. */
static int config_valid_map(const char *text) {
    size_t i;

    if (strlen(text) > SRV_MAP_MAX) return 0;
    for (i = 0; text[i] != '\0'; i++) {
        if ((unsigned char)text[i] < 0x20 || (unsigned char)text[i] > 0x7e) {
            return 0;
        }
    }
    return 1;
}

static int config_parse_int(const char *text, long minimum, long maximum,
                            long *out) {
    char *end = NULL;
    long value;

    errno = 0;
    value = strtol(text, &end, 10);
    if (errno != 0 || end == text || *end != '\0') return 0;
    if (value < minimum || value > maximum) return 0;
    *out = value;
    return 1;
}

static int config_parse_seconds(const char *text, double minimum, double *out) {
    char *end = NULL;
    double value;

    errno = 0;
    value = strtod(text, &end);
    if (errno != 0 || end == text || *end != '\0') return 0;
    if (value < minimum) return 0;
    *out = value;
    return 1;
}

/* Имя карты по умолчанию — имя файла карты: клиенту нужен именно файл
 * с таким именем рядом с собой. Возвращает 1 при успехе. */
static int config_default_map_from_file(SrvConfig *config) {
    const char *slash = strrchr(config->map_file, '/');
    const char *backslash = strrchr(config->map_file, '\\');
    const char *base;

    if (config->map[0] != '\0' || config->map_file[0] == '\0') return 1;

    base = (slash != NULL && backslash != NULL)
        ? (slash > backslash ? slash + 1 : backslash + 1)
        : (slash != NULL ? slash + 1 : (backslash != NULL ? backslash + 1
                                                          : config->map_file));
    if (*base == '\0' || !config_valid_map(base)) {
        fprintf(stderr, "game3d-server: не удалось взять имя карты из «%s»: "
                "нужно до %d байт печатного ASCII; задайте его явно "
                "опцией --map\n", config->map_file, SRV_MAP_MAX);
        return 0;
    }
    /* config_valid_map уже ограничила длину; memcpy не даёт GCC
     * подозревать усечение из-за разницы размеров буферов. */
    memcpy(config->map, base, strlen(base) + 1);
    return 1;
}

/* ---------- Разбор флагов ---------- */

/* Значение опции: следующий argv или NULL с сообщением об ошибке. */
static const char *config_next_value(int argc, char **argv, int *index,
                                     const char *option) {
    if (*index + 1 >= argc) {
        fprintf(stderr, "game3d-server: опции %s нужно значение\n", option);
        return NULL;
    }
    (*index)++;
    return argv[*index];
}

static int config_parse_long_option(int argc, char **argv, int *index,
                                    SrvConfig *config) {
    char name_buffer[64];
    const char *name = argv[*index] + 2;
    const char *value = NULL;
    const char *equals = strchr(name, '=');

    if (equals != NULL) {
        const size_t length = (size_t)(equals - name);
        if (length == 0 || length >= sizeof name_buffer) {
            fprintf(stderr, "game3d-server: неверная запись «%s»\n", argv[*index]);
            return CONFIG_ERROR;
        }
        memcpy(name_buffer, name, length);
        name_buffer[length] = '\0';
        name = name_buffer;
        value = equals + 1;
    }

    if (strcmp(name, "help") == 0) {
        if (value != NULL) {
            fprintf(stderr, "game3d-server: --help не принимает значение\n");
            return CONFIG_ERROR;
        }
        config_print_usage(stdout);
        return CONFIG_EXIT_OK;
    }
    if (strcmp(name, "version") == 0) {
        if (value != NULL) {
            fprintf(stderr, "game3d-server: --version не принимает значение\n");
            return CONFIG_ERROR;
        }
        config_print_version();
        return CONFIG_EXIT_OK;
    }
    if (strcmp(name, "verbose") == 0) {
        if (value != NULL) {
            fprintf(stderr, "game3d-server: --verbose не принимает значение\n");
            return CONFIG_ERROR;
        }
        config->verbose = 1;
        return CONFIG_OK;
    }
    if (strcmp(name, "quiet") == 0) {
        if (value != NULL) {
            fprintf(stderr, "game3d-server: --quiet не принимает значение\n");
            return CONFIG_ERROR;
        }
        config->quiet = 1;
        return CONFIG_OK;
    }

    if (strcmp(name, "port") == 0 || strcmp(name, "bind") == 0 ||
        strcmp(name, "name") == 0 || strcmp(name, "map") == 0 ||
        strcmp(name, "map-file") == 0 ||
        strcmp(name, "max-players") == 0 || strcmp(name, "timeout") == 0) {
        if (value == NULL) {
            value = config_next_value(argc, argv, index, argv[*index]);
            if (value == NULL) return CONFIG_ERROR;
        }
        if (strcmp(name, "port") == 0) {
            long number;
            if (!config_parse_int(value, 1, 65535, &number)) {
                fprintf(stderr, "game3d-server: «--port %s»: порт — целое "
                        "число 1..65535\n", value);
                return CONFIG_ERROR;
            }
            config->port = (int)number;
            return CONFIG_OK;
        }
        if (strcmp(name, "bind") == 0) {
            if (strlen(value) > SRV_BIND_MAX) {
                fprintf(stderr, "game3d-server: «--bind %s»: адрес длиннее "
                        "%d байт\n", value, SRV_BIND_MAX);
                return CONFIG_ERROR;
            }
            snprintf(config->bind_address, sizeof config->bind_address, "%s",
                     value);
            return CONFIG_OK;
        }
        if (strcmp(name, "name") == 0) {
            if (!config_valid_name(value)) {
                fprintf(stderr, "game3d-server: «--name %s»: имя длиннее %d "
                        "байт\n", value, SRV_NAME_MAX);
                return CONFIG_ERROR;
            }
            snprintf(config->name, sizeof config->name, "%s", value);
            return CONFIG_OK;
        }
        if (strcmp(name, "map") == 0) {
            if (!config_valid_map(value)) {
                fprintf(stderr, "game3d-server: «--map %s»: карта — до %d "
                        "байт печатного ASCII\n", value, SRV_MAP_MAX);
                return CONFIG_ERROR;
            }
            snprintf(config->map, sizeof config->map, "%s", value);
            return CONFIG_OK;
        }
        if (strcmp(name, "map-file") == 0) {
            if (strlen(value) > SRV_PATH_MAX) {
                fprintf(stderr, "game3d-server: «--map-file %s»: путь длиннее "
                        "%d байт\n", value, SRV_PATH_MAX);
                return CONFIG_ERROR;
            }
            snprintf(config->map_file, sizeof config->map_file, "%s", value);
            return CONFIG_OK;
        }
        if (strcmp(name, "max-players") == 0) {
            long number;
            if (!config_parse_int(value, 1, SRV_MAX_PLAYERS_LIMIT, &number)) {
                fprintf(stderr, "game3d-server: «--max-players %s»: нужно "
                        "целое число 1..%d\n", value, SRV_MAX_PLAYERS_LIMIT);
                return CONFIG_ERROR;
            }
            config->max_players = (int)number;
            return CONFIG_OK;
        }
        /* --timeout */
        {
            double number;
            if (!config_parse_seconds(value, 1.0, &number)) {
                fprintf(stderr, "game3d-server: «--timeout %s»: нужно число "
                        "секунд не меньше 1\n", value);
                return CONFIG_ERROR;
            }
            config->timeout_seconds = number;
            return CONFIG_OK;
        }
    }

    fprintf(stderr, "game3d-server: неизвестная опция «--%s»\n", name);
    return CONFIG_ERROR;
}

static int config_parse_short_options(int argc, char **argv, int *index,
                                      SrvConfig *config) {
    const char *cluster = argv[*index] + 1;

    while (*cluster != '\0') {
        const char option = *cluster;
        const char *value = NULL;

        /* Опции со значением: -p27015 или -p 27015. */
        if (option == 'p' || option == 'b' || option == 'n' ||
            option == 'm' || option == 'f' || option == 'M' ||
            option == 't') {
            if (cluster[1] != '\0') {
                value = cluster + 1;
            } else {
                char option_text[3];
                option_text[0] = '-';
                option_text[1] = option;
                option_text[2] = '\0';
                value = config_next_value(argc, argv, index, option_text);
                if (value == NULL) return CONFIG_ERROR;
            }
        }

        switch (option) {
            case 'h':
                config_print_usage(stdout);
                return CONFIG_EXIT_OK;
            case 'V':
                config_print_version();
                return CONFIG_EXIT_OK;
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
                if (!config_parse_int(value, 1, 65535, &number)) {
                    fprintf(stderr, "game3d-server: «-%c %s»: порт — целое "
                            "число 1..65535\n", option, value);
                    return CONFIG_ERROR;
                }
                config->port = (int)number;
                cluster += strlen(cluster);
                break;
            }
            case 'b':
                if (strlen(value) > SRV_BIND_MAX) {
                    fprintf(stderr, "game3d-server: «-%c %s»: адрес длиннее "
                            "%d байт\n", option, value, SRV_BIND_MAX);
                    return CONFIG_ERROR;
                }
                snprintf(config->bind_address, sizeof config->bind_address,
                         "%s", value);
                cluster += strlen(cluster);
                break;
            case 'n':
                if (!config_valid_name(value)) {
                    fprintf(stderr, "game3d-server: «-%c %s»: имя длиннее %d "
                            "байт\n", option, value, SRV_NAME_MAX);
                    return CONFIG_ERROR;
                }
                snprintf(config->name, sizeof config->name, "%s", value);
                cluster += strlen(cluster);
                break;
            case 'm':
                if (!config_valid_map(value)) {
                    fprintf(stderr, "game3d-server: «-%c %s»: карта — до %d "
                            "байт печатного ASCII\n", option, value,
                            SRV_MAP_MAX);
                    return CONFIG_ERROR;
                }
                snprintf(config->map, sizeof config->map, "%s", value);
                cluster += strlen(cluster);
                break;
            case 'f':
                if (strlen(value) > SRV_PATH_MAX) {
                    fprintf(stderr, "game3d-server: «-%c %s»: путь длиннее "
                            "%d байт\n", option, value, SRV_PATH_MAX);
                    return CONFIG_ERROR;
                }
                snprintf(config->map_file, sizeof config->map_file, "%s",
                         value);
                cluster += strlen(cluster);
                break;
            case 'M': {
                long number;
                if (!config_parse_int(value, 1, SRV_MAX_PLAYERS_LIMIT,
                                      &number)) {
                    fprintf(stderr, "game3d-server: «-%c %s»: нужно целое "
                            "число 1..%d\n", option, value,
                            SRV_MAX_PLAYERS_LIMIT);
                    return CONFIG_ERROR;
                }
                config->max_players = (int)number;
                cluster += strlen(cluster);
                break;
            }
            case 't': {
                double number;
                if (!config_parse_seconds(value, 1.0, &number)) {
                    fprintf(stderr, "game3d-server: «-%c %s»: нужно число "
                            "секунд не меньше 1\n", option, value);
                    return CONFIG_ERROR;
                }
                config->timeout_seconds = number;
                cluster += strlen(cluster);
                break;
            }
            default:
                fprintf(stderr, "game3d-server: неизвестная опция «-%c»\n",
                        option);
                return CONFIG_ERROR;
        }
    }
    return CONFIG_OK;
}

int config_parse_args(int argc, char **argv, SrvConfig *config) {
    int i = 1;

    while (i < argc) {
        const char *arg = argv[i];
        int result;

        if (strcmp(arg, "--") == 0) {
            if (i + 1 < argc) {
                fprintf(stderr, "game3d-server: неожиданный аргумент «%s»\n",
                        argv[i + 1]);
                return CONFIG_ERROR;
            }
            break;
        }
        if (arg[0] != '-' || arg[1] == '\0') {
            fprintf(stderr, "game3d-server: неожиданный аргумент «%s»\n", arg);
            return CONFIG_ERROR;
        }

        if (arg[1] == '-') {
            result = config_parse_long_option(argc, argv, &i, config);
        } else {
            result = config_parse_short_options(argc, argv, &i, config);
        }
        if (result != CONFIG_OK) return result;

        i++;
    }

    if (config->verbose && config->quiet) {
        fprintf(stderr, "game3d-server: --verbose и --quiet противоречат "
                "друг другу\n");
        return CONFIG_ERROR;
    }
    if (!config_default_map_from_file(config)) {
        return CONFIG_ERROR;
    }
    return CONFIG_OK;
}
