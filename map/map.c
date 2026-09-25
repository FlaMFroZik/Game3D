#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <GL/gl.h>

#include "map/map.h"
#include "physics/coll.h"
#include "render/prim.h"
#include "render/texpool.h"

/* ---------- Ограничения формата ---------- */

#define MAP_LINE_MAX   512                       /* длина строки файла карты */
#define MAP_MAX_TOKENS 16                        /* 6 чисел + опции куба */
#define MAP_TOKEN_MAX  256                       /* средний запас на токен: все
                                                  * токены делят общий буфер */

Map g_map = {0};

/* Каталог файла карты: относительные пути текстур ищутся сначала рядом
 * с картой, а уже потом относительно текущего каталога игры. */
static char map_base_dir[TEXPOOL_PATH_MAX];

void map_init(void) {
    free(g_map.cubes);
    g_map.cubes = NULL;
    g_map.count = 0;
    g_map.capacity = 0;
    g_map.is_loaded = 0;

    /* Текстуры прошлой карты больше не нужны. Если карта уже была загружена,
     * контекст OpenGL ещё жив — иначе пул пуст и ничего не удаляется. */
    texpool_free(&g_map.textures);
    map_base_dir[0] = '\0';
}

void map_free(void) {
    map_init();
}

int map_is_custom(void) {
    return g_map.is_loaded;
}

static int map_add_cube(const MapCube *cube) {
    if (g_map.count >= g_map.capacity) {
        size_t new_cap = (g_map.capacity == 0) ? 16 : g_map.capacity * 2;
        MapCube *new_cubes = realloc(g_map.cubes, new_cap * sizeof(MapCube));
        if (!new_cubes) {
            return 0;
        }
        g_map.cubes = new_cubes;
        g_map.capacity = new_cap;
    }

    g_map.cubes[g_map.count++] = *cube;
    return 1;
}

/* ---------- Пути к текстурам ----------
 * Карта и текстуры часто лежат в одной папке, а игра запускается из другой:
 * поэтому путь из карты ищется сначала рядом с файлом карты, затем
 * относительно текущего каталога. Абсолютный путь берётся как есть,
 * кавычки позволяют записать файл с пробелами. */

static int path_is_absolute(const char *path) {
    if (path[0] == '/' || path[0] == '\\') return 1;
    if (isalpha((unsigned char)path[0]) && path[1] == ':') return 1;   /* C:\... */
    return 0;
}

static int file_readable(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    fclose(f);
    return 1;
}

static int join_path(char *out, size_t out_size, const char *dir, const char *name) {
    int written = snprintf(out, out_size, "%s%s", dir, name);
    return written > 0 && (size_t)written < out_size;
}

/* "maps/world.txt" -> "maps/", "world.txt" -> "" */
static void remember_map_dir(const char *filename) {
    map_base_dir[0] = '\0';

    const char *slash = strrchr(filename, '/');
    const char *backslash = strrchr(filename, '\\');
    if (!slash || (backslash && backslash > slash)) slash = backslash;
    if (!slash) return;

    size_t len = (size_t)(slash - filename) + 1;   /* вместе с разделителем */
    if (len >= sizeof(map_base_dir)) len = sizeof(map_base_dir) - 1;
    memcpy(map_base_dir, filename, len);
    map_base_dir[len] = '\0';
}

static int resolve_texture_path(const char *name, char *out, size_t out_size) {
    if (name[0] == '\0') return 0;
    if (path_is_absolute(name)) return join_path(out, out_size, "", name);

    if (map_base_dir[0] != '\0' && join_path(out, out_size, map_base_dir, name)
        && file_readable(out)) {
        return 1;
    }
    if (file_readable(name)) return join_path(out, out_size, "", name);

    /* Файла нет ни там, ни там: показываем путь рядом с картой — по нему
     * обычно и понятно, какого файла не хватает. */
    if (map_base_dir[0] != '\0' && join_path(out, out_size, map_base_dir, name)) return 1;
    return join_path(out, out_size, "", name);
}

/* Текстура из карты. NULL — не загрузилась: куб нарисуется текстурой
 * по умолчанию, а карта продолжит загружаться. */
static const Texture *load_map_texture(const char *name, int line_num) {
    char path[TEXPOOL_PATH_MAX];

    if (!resolve_texture_path(name, path, sizeof(path))) {
        fprintf(stderr, "Warning: line %d: empty texture name\n", line_num);
        return NULL;
    }

    const Texture *tex = texpool_get(&g_map.textures, path);
    if (!tex) {
        fprintf(stderr, "Warning: line %d: cannot load texture '%s', using the default texture\n",
                line_num, path);
    }
    return tex;
}

/* ---------- Разбор строк ---------- */

/* Разбивает строку на токены:
 *   - разделители — пробелы и табы;
 *   - кавычки ("..." или '...') склеивают пробелы внутрь токена, сами
 *     кавычки в токен не попадают: tex="my textures/brick.raw";
 *   - '#' в начале токена начинает комментарий до конца строки.
 * Токены копируются в storage (каждый заканчивается нулём), в tokens
 * кладутся указатели на них. Возвращает число токенов; overflow — в
 * строке было больше токенов, чем поместилось. */
static int tokenize(const char *line, char *storage, size_t storage_size,
                    char *tokens[], int max_tokens, int *overflow) {
    int count = 0;
    size_t used = 0;
    const char *p = line;

    if (overflow) *overflow = 0;

    while (*p != '\0') {
        /* разделители и конец строки */
        while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
        if (*p == '\0' || *p == '#') break;

        if (count == max_tokens || used + 1 >= storage_size) {
            if (overflow) *overflow = 1;
            break;
        }

        tokens[count++] = storage + used;

        char quote = '\0';
        while (*p != '\0') {
            char c = *p;
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

static int token_is_number(const char *token, float *out) {
    char *end = NULL;
    float value = strtof(token, &end);
    if (end == token) return 0;                    /* не число вовсе */
    if (*end != '\0') return 0;                    /* число с «хвостом» */
    if (!isfinite(value)) return 0;                /* nan / inf */
    *out = value;
    return 1;
}

/* Размер копии текстуры в метрах: неотрицательное число. */
static int parse_tile(const char *text, float *out) {
    float meters = 0.0f;
    if (!token_is_number(text, &meters) || meters < 0.0f) return 0;
    *out = meters;
    return 1;
}

/* Значение опции key=value или NULL, если токен начинается не с key=. */
static const char *option_value(const char *token, const char *key) {
    size_t len = strlen(key);
    if (strncmp(token, key, len) != 0 || token[len] != '=') return NULL;
    return token + len + 1;
}

/* Меняет текстуру, только если новая загрузилась: битый файл оставляет
 * прежнюю (текстуру по умолчанию). */
static void set_texture(const Texture **texture, const char *name, int line_num) {
    const Texture *tex = load_map_texture(name, line_num);
    if (tex) *texture = tex;
}

/* ---------- Директивы карты ----------
 * Действуют на все кубы, описанные ниже: так одной строкой задаётся
 * текстура всей карты, а stretch — только её части. */

typedef struct {
    const Texture *texture;   /* NULL — текстура, переданная игре при запуске */
    float tile;               /* 0 — PRIM_TEX_TILE_METERS */
    MapUvMode uv;
} MapDefaults;

static void apply_directive(char *tokens[], int count, int line_num, MapDefaults *def) {
    const char *key = tokens[0];
    const char *arg = (count > 1) ? tokens[1] : NULL;

    if (strcmp(key, "texture") == 0 || strcmp(key, "tex") == 0) {
        if (!arg) {
            fprintf(stderr, "Warning: line %d: 'texture' needs a file name\n", line_num);
            return;
        }
        set_texture(&def->texture, arg, line_num);
        return;
    }

    if (strcmp(key, "tile") == 0) {
        if (!arg) {                       /* без числа — размер по умолчанию */
            def->tile = 0.0f;
        } else if (!parse_tile(arg, &def->tile)) {
            fprintf(stderr, "Warning: line %d: 'tile' expects a size in meters, got '%s'\n",
                    line_num, arg);
        }
        return;
    }

    if (strcmp(key, "repeat") == 0) { def->uv = MAP_UV_TILE;    return; }
    if (strcmp(key, "stretch") == 0) { def->uv = MAP_UV_STRETCH; return; }

    fprintf(stderr, "Warning: line %d: unknown directive '%s'\n", line_num, key);
}

/* ---------- Строка куба ----------
 * x y z sx sy sz [tex=<файл>] [tile=<метры>] [repeat|stretch]
 * Текстуру можно задать и просто путём без ключа — сразу после размеров.
 * Возвращает 0 только при нехватке памяти. */

static int parse_cube(char *tokens[], int count, int line_num, const MapDefaults *def) {
    if (count < 6) {
        fprintf(stderr, "Warning: line %d: expected 'x y z sx sy sz', only %d values given\n",
                line_num, count);
        return 1;
    }

    float v[6];
    for (int i = 0; i < 6; i++) {
        if (!token_is_number(tokens[i], &v[i])) {
            fprintf(stderr, "Warning: line %d: expected 'x y z sx sy sz', got '%s'\n",
                    line_num, tokens[i]);
            return 1;
        }
    }

    MapCube cube;
    cube.x = v[0];  cube.y = v[1];  cube.z = v[2];
    cube.sx = v[3]; cube.sy = v[4]; cube.sz = v[5];
    cube.texture = def->texture;
    cube.tile = def->tile;
    cube.uv = def->uv;

    for (int i = 6; i < count; i++) {
        const char *token = tokens[i];
        const char *value;

        if ((value = option_value(token, "tex")) || (value = option_value(token, "texture"))) {
            set_texture(&cube.texture, value, line_num);
        } else if ((value = option_value(token, "tile"))) {
            if (!parse_tile(value, &cube.tile)) {
                fprintf(stderr, "Warning: line %d: 'tile=' expects a size in meters, got '%s'\n",
                        line_num, value);
            }
        } else if (strcmp(token, "stretch") == 0) {
            cube.uv = MAP_UV_STRETCH;
        } else if (strcmp(token, "repeat") == 0) {
            cube.uv = MAP_UV_TILE;
        } else if (strchr(token, '=') == NULL) {
            /* путь без ключа: 0 0 0 2 2 2 brick.raw */
            set_texture(&cube.texture, token, line_num);
        } else {
            fprintf(stderr, "Warning: line %d: unknown cube option '%s'\n", line_num, token);
        }
    }

    return map_add_cube(&cube);
}

int map_load(const char *filename) {
    map_init();

    if (!filename) {
        return 0;
    }

    FILE *f = fopen(filename, "r");
    if (!f) {
        fprintf(stderr, "Warning: could not open map file '%s', falling back to procedural generation.\n", filename);
        return 0;
    }

    remember_map_dir(filename);

    MapDefaults def = {NULL, 0.0f, MAP_UV_TILE};

    char line[MAP_LINE_MAX];
    int line_num = 0;
    int loaded_count = 0;

    while (fgets(line, sizeof(line), f)) {
        line_num++;

        char storage[MAP_MAX_TOKENS * MAP_TOKEN_MAX];
        char *tokens[MAP_MAX_TOKENS];
        int overflow = 0;
        int count = tokenize(line, storage, sizeof(storage), tokens, MAP_MAX_TOKENS, &overflow);

        if (overflow) {
            fprintf(stderr, "Warning: line %d: too many tokens or too long a line, "
                            "the rest is ignored\n", line_num);
        }
        if (count == 0) {
            continue;
        }

        /* Строка куба начинается с числа, остальные строки — директивы. */
        float first;
        if (!token_is_number(tokens[0], &first)) {
            apply_directive(tokens, count, line_num, &def);
            continue;
        }

        if (!parse_cube(tokens, count, line_num, &def)) {
            fprintf(stderr, "Error: out of memory reading map file at line %d\n", line_num);
            fclose(f);
            map_free();
            return 0;
        }
        loaded_count++;
    }

    fclose(f);

    printf("Loaded map '%s': %d cubes, %d textures\n",
           filename, loaded_count, (int)texpool_count(&g_map.textures));
    g_map.is_loaded = 1;
    return 1;
}

/* Отрисовка одного куба.
 *
 * По умолчанию текстура не растягивается на грань: её координаты считаются
 * от мировых осей (U — вдоль грани, V — вверх для боковых граней и вглубь
 * для горизонтальных), поэтому рисунок масштабирован одинаково на всех
 * гранях, повторяется по поверхности и продолжается на соседних кубах.
 * Размер одной копии — cube->tile метров (0 — PRIM_TEX_TILE_METERS).
 *
 * В режиме MAP_UV_STRETCH на каждой грани ровно одна копия текстуры:
 * все оси идут от 0 до 1, поэтому рисунок виден целиком. */
static void draw_cube(const MapCube *c, const Texture *tex) {
    float x0 = c->x;
    float y0 = c->y;
    float z0 = c->z;
    float x1 = c->x + c->sx;
    float y1 = c->y + c->sy;
    float z1 = c->z + c->sz;

    float u_x0, u_x1, u_z0, u_z1;
    float v_y0, v_y1, v_z0, v_z1;

    if (c->uv == MAP_UV_STRETCH) {
        u_x0 = u_z0 = v_y0 = v_z0 = 0.0f;
        u_x1 = u_z1 = v_y1 = v_z1 = 1.0f;
    } else {
        /* координаты текстуры по каждой мировой оси: U и V считаются разными
         * функциями (они совпадают только у квадратной текстуры) */
        u_x0 = prim_tex_u_tile(tex, x0, c->tile);
        u_x1 = prim_tex_u_tile(tex, x1, c->tile);
        u_z0 = prim_tex_u_tile(tex, z0, c->tile);
        u_z1 = prim_tex_u_tile(tex, z1, c->tile);
        v_y0 = prim_tex_v_tile(tex, y0, c->tile);
        v_y1 = prim_tex_v_tile(tex, y1, c->tile);
        v_z0 = prim_tex_v_tile(tex, z0, c->tile);
        v_z1 = prim_tex_v_tile(tex, z1, c->tile);
    }

    const PrimVertex faces[6][4] = {
        /* Передняя грань (Z+): U — по X, V — по Y (вверх) */
        {{u_x0, v_y0, x0, y0, z1}, {u_x1, v_y0, x1, y0, z1},
         {u_x1, v_y1, x1, y1, z1}, {u_x0, v_y1, x0, y1, z1}},
        /* Задняя грань (Z-) */
        {{u_x0, v_y0, x0, y0, z0}, {u_x0, v_y1, x0, y1, z0},
         {u_x1, v_y1, x1, y1, z0}, {u_x1, v_y0, x1, y0, z0}},
        /* Верхняя грань (Y+): U — по X, V — по Z */
        {{u_x0, v_z0, x0, y1, z0}, {u_x0, v_z1, x0, y1, z1},
         {u_x1, v_z1, x1, y1, z1}, {u_x1, v_z0, x1, y1, z0}},
        /* Нижняя грань (Y-) */
        {{u_x0, v_z0, x0, y0, z0}, {u_x1, v_z0, x1, y0, z0},
         {u_x1, v_z1, x1, y0, z1}, {u_x0, v_z1, x0, y0, z1}},
        /* Правая грань (X+): U — по Z, V — по Y */
        {{u_z0, v_y0, x1, y0, z0}, {u_z0, v_y1, x1, y1, z0},
         {u_z1, v_y1, x1, y1, z1}, {u_z1, v_y0, x1, y0, z1}},
        /* Левая грань (X-): U — по Z, V — по Y */
        {{u_z0, v_y0, x0, y0, z0}, {u_z1, v_y0, x0, y0, z1},
         {u_z1, v_y1, x0, y1, z1}, {u_z0, v_y1, x0, y1, z0}},
    };

    glBegin(GL_TRIANGLES);
    for (int i = 0; i < 6; i++) {
        prim_emit_quad(faces[i]);
    }
    glEnd();
}

void map_render(const Texture *fallback) {
    GLuint bound = 0;   /* текстура, привязанная прямо сейчас */

    for (size_t i = 0; i < g_map.count; i++) {
        const MapCube *cube = &g_map.cubes[i];
        const Texture *tex = cube->texture ? cube->texture : fallback;
        if (!tex) continue;

        /* Переключаем текстуру только когда она правда другая: у карты
         * с одной текстурой привязка остаётся одна на весь кадр. */
        if (tex->id != bound) {
            glBindTexture(GL_TEXTURE_2D, tex->id);
            bound = tex->id;
        }

        draw_cube(cube, tex);
    }
}

/* ---------- Коллизии ---------- */

/* Границы куба по осям. Размеры в карте могут быть отрицательными
 * (куб «растёт» в другую сторону), поэтому min/max упорядочиваются. */
typedef struct {
    float min_x, max_x;
    float min_y, max_y;
    float min_z, max_z;
} CubeBounds;

static void ordered(float a, float b, float *lo, float *hi) {
    *lo = (a < b) ? a : b;
    *hi = (a < b) ? b : a;
}

static CubeBounds cube_bounds(const MapCube *c) {
    CubeBounds b;
    ordered(c->x, c->x + c->sx, &b.min_x, &b.max_x);
    ordered(c->y, c->y + c->sy, &b.min_y, &b.max_y);
    ordered(c->z, c->z + c->sz, &b.min_z, &b.max_z);
    return b;
}

static float clampf(float v, float min, float max) {
    if (v < min) return min;
    if (v > max) return max;
    return v;
}

/* Квадрат расстояния по горизонтали от точки до «следа» куба (0 — внутри). */
static float footprint_dist_sq(const CubeBounds *b, float x, float z) {
    float dx = x - clampf(x, b->min_x, b->max_x);
    float dz = z - clampf(z, b->min_z, b->max_z);
    return dx * dx + dz * dz;
}

/* Стена, если тело игрока пересекает куб по высоте и куб выше шага
 * или нависает над ногами. */
static int blocks_body(const CubeBounds *b, float feet_y) {
    int overlaps = feet_y + COLL_HEIGHT > b->min_y + COLL_EPSILON
                && feet_y < b->max_y - COLL_EPSILON;
    int too_high = b->max_y - feet_y > COLL_STEP_HEIGHT
                || feet_y < b->min_y - COLL_EPSILON;
    return overlaps && too_high;
}

int map_point_blocked(float px, float pz, float bottom_y) {
    if (!g_map.is_loaded) return 0;

    for (size_t i = 0; i < g_map.count; i++) {
        CubeBounds b = cube_bounds(&g_map.cubes[i]);
        int inside = px >= b.min_x && px <= b.max_x && pz >= b.min_z && pz <= b.max_z;
        if (inside && blocks_body(&b, bottom_y)) return 1;
    }
    return 0;
}

int map_capsule_blocked(float cx, float cz, float feet_y) {
    if (!g_map.is_loaded) return 0;

    for (size_t i = 0; i < g_map.count; i++) {
        CubeBounds b = cube_bounds(&g_map.cubes[i]);
        if (footprint_dist_sq(&b, cx, cz) < COLL_RADIUS * COLL_RADIUS
            && blocks_body(&b, feet_y)) {
            return 1;
        }
    }
    return 0;
}

float map_cylinder_ground_height(float cx, float cz, float radius, float current_feet_y, float default_y) {
    if (!g_map.is_loaded) return default_y;

    /* Куб считается землёй под ногами, если его верх не выше ног + шаг. */
    const float max_ground = current_feet_y + COLL_STEP_HEIGHT + COLL_EPSILON;
    float highest = default_y;

    for (size_t i = 0; i < g_map.count; i++) {
        CubeBounds b = cube_bounds(&g_map.cubes[i]);
        if (footprint_dist_sq(&b, cx, cz) <= radius * radius
            && b.max_y <= max_ground && b.max_y > highest) {
            highest = b.max_y;
        }
    }
    return highest;
}

float map_ground_height(float px, float pz, float current_feet_y, float default_y) {
    return map_cylinder_ground_height(px, pz, 0.0f, current_feet_y, default_y);
}
