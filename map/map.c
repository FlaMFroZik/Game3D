#include "map/map.h"

#include "log.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Те же пределы, что у клиента: строка — 512 байт, токен — не длиннее
 * 256 байт. Слишком длинная строка/токен у клиента обрезается и
 * продолжает парситься, у сервера — строка пропускается с записью
 * в лог, чтобы не собрать геометрию из обрезанного файла. */
#define SRV_MAP_LINE_MAX  512
#define SRV_MAP_MAX_TOKENS 32
#define SRV_MAP_TOKEN_MAX 256
/* Верхний предел кубов: защита от злонамеренно огромного файла.
 * Ручные карты на порядки меньше. */
#define SRV_MAP_MAX_CUBES ((size_t)1 << 20)

/* Разбор числа: токен должен быть числом целиком и быть конечным
 * (nan/inf клиент не принимает — так же и здесь). */
static int map_token_is_number(const char *token, float *out) {
    char *end = NULL;
    float value;

    if (token == NULL || *token == '\0') return 0;
    value = strtof(token, &end);
    if (end == token || *end != '\0') return 0;
    if (!isfinite(value)) return 0;
    *out = value;
    return 1;
}

static int map_append_box(SrvMap *map, float x, float y, float z,
                          float sx, float sy, float sz) {
    SrvBox *box;

    if (map->count >= SRV_MAP_MAX_CUBES) return 0;

    if (map->count == map->capacity) {
        const size_t new_capacity = map->capacity > 0 ? map->capacity * 2 : 64;
        SrvBox *grown = realloc(map->boxes, new_capacity * sizeof *grown);
        if (grown == NULL) return 0;
        map->boxes = grown;
        map->capacity = new_capacity;
    }

    box = &map->boxes[map->count++];
    /* Отрицательные размеры переворачивают куб — приводим к min/max. */
    box->min_x = x < x + sx ? x : x + sx;
    box->max_x = x < x + sx ? x + sx : x;
    box->min_y = y < y + sy ? y : y + sy;
    box->max_y = y < y + sy ? y + sy : y;
    box->min_z = z < z + sz ? z : z + sz;
    box->max_z = z < z + sz ? z + sz : z;
    return 1;
}

/* ---------- Токенизатор (как у клиента) ----------
 *
 * Разделители — пробелы и табы; кавычки ("..." или '...') склеивают
 * пробелы внутрь токена (кавычки в токен не попадают); '#' в начале
 * токена начинает комментарий до конца строки. # внутри токена —
 * обычный символ. Возвращает число токенов; *overflow — строка была
 * длиннее буфера или токенов больше, чем вместится. */
static int map_tokenize(const char *line, char *storage, size_t storage_size,
                        char *tokens[], int max_tokens, int *overflow) {
    int count = 0;
    size_t used = 0;
    const char *p = line;

    if (overflow) *overflow = 0;

    while (*p != '\0') {
        while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
        if (*p == '\0' || *p == '#') break;

        if (count == max_tokens || used + 1 >= storage_size) {
            if (overflow) *overflow = 1;
            break;
        }

        tokens[count++] = storage + used;

        char quote = '\0';
        while (*p != '\0') {
            const char c = *p;
            if (quote != '\0') {
                if (c == quote) { quote = '\0'; p++; continue; }
            } else if (c == '"' || c == '\'') {
                quote = c; p++; continue;
            } else if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
                break;
            }

            if (used + 1 >= storage_size) break;   /* строка длиннее буфера */
            storage[used++] = c;
            p++;
        }
        storage[used++] = '\0';
    }
    return count;
}

static int map_is_directive(const char *key) {
    return strcmp(key, "texture") == 0 || strcmp(key, "tex") == 0
        || strcmp(key, "tile") == 0
        || strcmp(key, "repeat") == 0 || strcmp(key, "stretch") == 0;
}

/* ---------- Разбор строки ----------
 * Формат клиента Game3D:
 *   <префаб> x y z sx sy sz [опции]   — новая карта
 *   x y z sx sy sz [опции]             — старая карта (объект куба)
 * Директивы (texture/tex/tile/repeat/stretch) на геометрию не влияют
 * и пропускаются; комментарии и пустые строки пропускаются. */
static int map_parse_line(char *line, int line_number, SrvMap *map) {
    char storage[SRV_MAP_MAX_TOKENS * SRV_MAP_TOKEN_MAX];
    char *tokens[SRV_MAP_MAX_TOKENS];
    int overflow = 0;
    int count;
    int i;
    float v[6];

    count = map_tokenize(line, storage, sizeof(storage), tokens,
                         SRV_MAP_MAX_TOKENS, &overflow);
    if (overflow) {
        srv_log(SRV_LOG_ERROR, "карта: строка %d: слишком много токенов или "
                "слишком длинная строка — строка пропущена", line_number);
        return -1;
    }
    if (count == 0) return 0;

    /* Директивы внешнего вида действуют на отрисовку у клиента, геометрии
     * не касаются. */
    if (map_is_directive(tokens[0])) return 0;

    float legacy;
    int numbers_start;   /* индекс первого числа */

    if (map_token_is_number(tokens[0], &legacy)) {
        /* Старая карта: строка начинается сразу с координат. Читаем её
         * как встроенный куб, как клиент. */
        numbers_start = 0;
    } else {
        /* Новая карта: первый токен — имя префаба (куб или модель).
         * Серверу не нужно знать префаб — ему нужен AABB объекта. */
        numbers_start = 1;
    }

    if (count - numbers_start < 6) {
        srv_log(SRV_LOG_ERROR, "карта: строка %d: нужно «x y z sx sy sz», "
                "чисел всего %d — строка пропущена",
                line_number, count - numbers_start);
        return -1;
    }
    for (i = 0; i < 6; i++) {
        if (!map_token_is_number(tokens[numbers_start + i], &v[i])) {
            srv_log(SRV_LOG_ERROR, "карта: строка %d: «%s» — не число, "
                    "строка пропущена", line_number,
                    tokens[numbers_start + i]);
            return -1;
        }
    }

    if (!map_append_box(map, v[0], v[1], v[2], v[3], v[4], v[5])) {
        return -2;
    }
    return 1;
}

int srv_map_load(SrvMap *map, const char *path) {
    FILE *file;
    char line[SRV_MAP_LINE_MAX];
    int line_number = 0;
    int cubes = 0;
    int errors = 0;
    int overflow = 0;

    map->boxes = NULL;
    map->count = 0;
    map->capacity = 0;

    file = fopen(path, "r");
    if (file == NULL) {
        srv_log(SRV_LOG_ERROR, "не удалось открыть карту «%s»", path);
        return 0;
    }

    while (fgets(line, sizeof line, file) != NULL) {
        int result;

        line[strcspn(line, "\r\n")] = '\0';
        line_number++;

        result = map_parse_line(line, line_number, map);
        if (result == 1) {
            cubes++;
        } else if (result == -1) {
            errors++;
        } else if (result == -2) {
            overflow = 1;
            break;
        }
    }
    fclose(file);

    if (overflow) {
        srv_log(SRV_LOG_ERROR, "карта «%s»: слишком много кубов или не "
                "хватило памяти (загружено %zu)", path, map->count);
        srv_map_free(map);
        return 0;
    }

    srv_log(SRV_LOG_INFO, "карта «%s»: %d куб(ов)%s", path, cubes,
            errors > 0 ? ", некорректные строки пропущены" : "");
    return 1;
}

void srv_map_free(SrvMap *map) {
    free(map->boxes);
    map->boxes = NULL;
    map->count = 0;
    map->capacity = 0;
}
