#include "map/map.h"

#include "log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SRV_MAP_LINE_MAX  1024
/* Верхний предел кубов: защита от злонамеренно огромного файла.
 * Ручные карты на порядки меньше. */
#define SRV_MAP_MAX_CUBES ((size_t)1 << 20)

/* Разбор числа: токен должен быть числом целиком («10#комментарий» —
 * ошибка, как и у клиента). */
static int map_token_is_number(const char *token, float *out) {
    char *end = NULL;
    float value;

    value = strtof(token, &end);
    if (end == token || *end != '\0') return 0;
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

/* Результат разбора строки:
 *   0 — строка не про геометрию (пустая, комментарий, директива);
 *   1 — куб добавлен;
 *  -1 — ошибка в строке, предупреждение уже записано;
 *  -2 — кубов слишком много или не хватило памяти. */
static int map_parse_line(char *line, int line_number, SrvMap *map) {
    /* Текстуры могут содержать пробелы в кавычках, но кубу хватает
     * первых шести чисел — остальное можно не разбирать тонко. */
    char *tokens[64];
    int count = 0;
    char *cursor = line;
    float v[6];
    int i;

    while (*cursor != '\0' && count < 64) {
        while (*cursor == ' ' || *cursor == '\t') cursor++;
        /* '#' в начале токена начинает комментарий до конца строки. */
        if (*cursor == '\0' || *cursor == '#') break;
        tokens[count++] = cursor;
        while (*cursor != '\0' && *cursor != ' ' && *cursor != '\t') cursor++;
        if (*cursor != '\0') {
            *cursor = '\0';
            cursor++;
        }
    }
    if (count == 0) return 0;

    /* Директивы внешнего вида действуют только на отрисовку у клиента. */
    if (strcmp(tokens[0], "texture") == 0 ||
        strcmp(tokens[0], "tile") == 0 ||
        strcmp(tokens[0], "repeat") == 0 ||
        strcmp(tokens[0], "stretch") == 0) {
        return 0;
    }

    if (count < 6) {
        srv_log(SRV_LOG_ERROR, "карта: строка %d: нужно «x y z sx sy sz», "
                "чисел всего %d — строка пропущена", line_number, count);
        return -1;
    }
    for (i = 0; i < 6; i++) {
        if (!map_token_is_number(tokens[i], &v[i])) {
            srv_log(SRV_LOG_ERROR, "карта: строка %d: «%s» — не число, "
                    "строка пропущена", line_number, tokens[i]);
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
