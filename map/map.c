#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <GL/gl.h>

#include "map/map.h"
#include "physics/coll.h"
#include "render/prim.h"

Map g_map = {0};

void map_init(void) {
    if (g_map.cubes) {
        free(g_map.cubes);
    }
    g_map.cubes = NULL;
    g_map.count = 0;
    g_map.capacity = 0;
    g_map.is_loaded = 0;
}

void map_free(void) {
    if (g_map.cubes) {
        free(g_map.cubes);
        g_map.cubes = NULL;
    }
    g_map.count = 0;
    g_map.capacity = 0;
    g_map.is_loaded = 0;
}

int map_is_custom(void) {
    return g_map.is_loaded;
}

static int map_add_cube(float x, float y, float z, float sx, float sy, float sz) {
    if (g_map.count >= g_map.capacity) {
        size_t new_cap = (g_map.capacity == 0) ? 16 : g_map.capacity * 2;
        MapCube *new_cubes = realloc(g_map.cubes, new_cap * sizeof(MapCube));
        if (!new_cubes) {
            return 0;
        }
        g_map.cubes = new_cubes;
        g_map.capacity = new_cap;
    }

    g_map.cubes[g_map.count].x = x;
    g_map.cubes[g_map.count].y = y;
    g_map.cubes[g_map.count].z = z;
    g_map.cubes[g_map.count].sx = sx;
    g_map.cubes[g_map.count].sy = sy;
    g_map.cubes[g_map.count].sz = sz;
    g_map.count++;
    return 1;
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

    char line[256];
    int line_num = 0;
    int loaded_count = 0;

    while (fgets(line, sizeof(line), f)) {
        line_num++;
        /* Пропуск пробелов в начале */
        char *ptr = line;
        while (*ptr == ' ' || *ptr == '\t') ptr++;

        if (*ptr == '\0' || *ptr == '\r' || *ptr == '\n' || *ptr == '#') {
            continue;
        }

        float x, y, z, sx, sy, sz;
        if (sscanf(ptr, "%f %f %f %f %f %f", &x, &y, &z, &sx, &sy, &sz) == 6) {
            if (!map_add_cube(x, y, z, sx, sy, sz)) {
                fprintf(stderr, "Error: out of memory reading map file at line %d\n", line_num);
                fclose(f);
                map_free();
                return 0;
            }
            loaded_count++;
        } else {
            fprintf(stderr, "Warning: invalid map format on line %d in '%s'\n", line_num, filename);
        }
    }

    fclose(f);

    printf("Loaded map '%s': %d cubes\n", filename, loaded_count);
    g_map.is_loaded = 1;
    return 1;
}

/* Отрисовка одного куба.
 *
 * Текстура не растягивается на грань: её координаты считаются от мировых
 * осей (U — вдоль грани, V — вверх для боковых граней и вглубь для
 * горизонтальных), поэтому рисунок масштабирован одинаково на всех гранях,
 * повторяется по поверхности и продолжается на соседних кубах.
 * Размер одной копии текстуры задаёт PRIM_TEX_TILE_METERS (render/prim.h). */
static void draw_cube(const MapCube *c, const Texture *tex) {
    float x0 = c->x;
    float y0 = c->y;
    float z0 = c->z;
    float x1 = c->x + c->sx;
    float y1 = c->y + c->sy;
    float z1 = c->z + c->sz;

    /* координаты текстуры по каждой мировой оси: U и V считаются разными
     * функциями (они совпадают только у квадратной текстуры) */
    const float u_x0 = prim_tex_u(tex, x0), u_x1 = prim_tex_u(tex, x1);
    const float u_z0 = prim_tex_u(tex, z0), u_z1 = prim_tex_u(tex, z1);
    const float v_y0 = prim_tex_v(tex, y0), v_y1 = prim_tex_v(tex, y1);
    const float v_z0 = prim_tex_v(tex, z0), v_z1 = prim_tex_v(tex, z1);

    glBegin(GL_TRIANGLES);

    /* Передняя грань (Z+): U — по X, V — по Y (вверх) */
    glTexCoord2f(u_x0, v_y0); glVertex3f(x0, y0, z1);
    glTexCoord2f(u_x1, v_y0); glVertex3f(x1, y0, z1);
    glTexCoord2f(u_x1, v_y1); glVertex3f(x1, y1, z1);
    glTexCoord2f(u_x0, v_y0); glVertex3f(x0, y0, z1);
    glTexCoord2f(u_x1, v_y1); glVertex3f(x1, y1, z1);
    glTexCoord2f(u_x0, v_y1); glVertex3f(x0, y1, z1);

    /* Задняя грань (Z-) */
    glTexCoord2f(u_x0, v_y0); glVertex3f(x0, y0, z0);
    glTexCoord2f(u_x0, v_y1); glVertex3f(x0, y1, z0);
    glTexCoord2f(u_x1, v_y1); glVertex3f(x1, y1, z0);
    glTexCoord2f(u_x0, v_y0); glVertex3f(x0, y0, z0);
    glTexCoord2f(u_x1, v_y1); glVertex3f(x1, y1, z0);
    glTexCoord2f(u_x1, v_y0); glVertex3f(x1, y0, z0);

    /* Верхняя грань (Y+): U — по X, V — по Z */
    glTexCoord2f(u_x0, v_z0); glVertex3f(x0, y1, z0);
    glTexCoord2f(u_x0, v_z1); glVertex3f(x0, y1, z1);
    glTexCoord2f(u_x1, v_z1); glVertex3f(x1, y1, z1);
    glTexCoord2f(u_x0, v_z0); glVertex3f(x0, y1, z0);
    glTexCoord2f(u_x1, v_z1); glVertex3f(x1, y1, z1);
    glTexCoord2f(u_x1, v_z0); glVertex3f(x1, y1, z0);

    /* Нижняя грань (Y-) */
    glTexCoord2f(u_x0, v_z0); glVertex3f(x0, y0, z0);
    glTexCoord2f(u_x1, v_z0); glVertex3f(x1, y0, z0);
    glTexCoord2f(u_x1, v_z1); glVertex3f(x1, y0, z1);
    glTexCoord2f(u_x0, v_z0); glVertex3f(x0, y0, z0);
    glTexCoord2f(u_x1, v_z1); glVertex3f(x1, y0, z1);
    glTexCoord2f(u_x0, v_z1); glVertex3f(x0, y0, z1);

    /* Правая грань (X+): U — по Z, V — по Y */
    glTexCoord2f(u_z0, v_y0); glVertex3f(x1, y0, z0);
    glTexCoord2f(u_z0, v_y1); glVertex3f(x1, y1, z0);
    glTexCoord2f(u_z1, v_y1); glVertex3f(x1, y1, z1);
    glTexCoord2f(u_z0, v_y0); glVertex3f(x1, y0, z0);
    glTexCoord2f(u_z1, v_y1); glVertex3f(x1, y1, z1);
    glTexCoord2f(u_z1, v_y0); glVertex3f(x1, y0, z1);

    /* Левая грань (X-): U — по Z, V — по Y */
    glTexCoord2f(u_z0, v_y0); glVertex3f(x0, y0, z0);
    glTexCoord2f(u_z1, v_y0); glVertex3f(x0, y0, z1);
    glTexCoord2f(u_z1, v_y1); glVertex3f(x0, y1, z1);
    glTexCoord2f(u_z0, v_y0); glVertex3f(x0, y0, z0);
    glTexCoord2f(u_z1, v_y1); glVertex3f(x0, y1, z1);
    glTexCoord2f(u_z0, v_y1); glVertex3f(x0, y1, z0);

    glEnd();
}

void map_render(const Texture *tex) {
    for (size_t i = 0; i < g_map.count; i++) {
        draw_cube(&g_map.cubes[i], tex);
    }
}

int map_point_blocked(float px, float pz, float bottom_y) {
    if (!g_map.is_loaded) return 0;

    for (size_t i = 0; i < g_map.count; i++) {
        const MapCube *c = &g_map.cubes[i];
        float min_x = c->x;
        float max_x = c->x + c->sx;
        if (min_x > max_x) { float t = min_x; min_x = max_x; max_x = t; }

        float min_z = c->z;
        float max_z = c->z + c->sz;
        if (min_z > max_z) { float t = min_z; min_z = max_z; max_z = t; }

        float min_y = c->y;
        float max_y = c->y + c->sy;
        if (min_y > max_y) { float t = min_y; min_y = max_y; max_y = t; }

        if (px >= min_x && px <= max_x && pz >= min_z && pz <= max_z) {
            /* Стена, если куб выше bottom_y + COLL_STEP_HEIGHT, или если игрок внутри по высоте */
            if (max_y - bottom_y > COLL_STEP_HEIGHT && bottom_y < max_y) {
                return 1;
            }
        }
    }
    return 0;
}

float map_ground_height(float px, float pz, float current_feet_y, float default_y) {
    if (!g_map.is_loaded) return default_y;

    float highest = default_y;

    for (size_t i = 0; i < g_map.count; i++) {
        const MapCube *c = &g_map.cubes[i];
        float min_x = c->x;
        float max_x = c->x + c->sx;
        if (min_x > max_x) { float t = min_x; min_x = max_x; max_x = t; }

        float min_z = c->z;
        float max_z = c->z + c->sz;
        if (min_z > max_z) { float t = min_z; min_z = max_z; max_z = t; }

        float max_y = (c->sy >= 0) ? (c->y + c->sy) : c->y;

        if (px >= min_x && px <= max_x && pz >= min_z && pz <= max_z) {
            /* Куб считается землей под ногами, если его верх не выше ног + COLL_STEP_HEIGHT */
            if (max_y <= current_feet_y + COLL_STEP_HEIGHT + COLL_EPSILON) {
                if (max_y > highest) {
                    highest = max_y;
                }
            }
        }
    }

    return highest;
}
