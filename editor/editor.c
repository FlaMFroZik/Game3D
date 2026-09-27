#include <math.h>
#include <stdio.h>
#include <string.h>

#include <GL/gl.h>

#include "editor/editor.h"
#include "map/list.h"
#include "map/map.h"
#include "render/render.h"
#include "render/window.h"

/* ---------- Параметры ---------- */

#define ED_MOVE_SPEED   8.0f     /* полёт камеры, метров в секунду */
#define ED_FAST_FACTOR  3.0f     /* ускорение с Shift */
#define ED_LOOK_SENS    0.0025f  /* радиан на пиксель движения мыши */
#define ED_MIN_Y       -20.0f    /* ниже камеру не пускаем: там ничего нет */
#define ED_MAX_Y        120.0f

/* ---------- Служебная геометрия ---------- */

typedef struct {
    float min[3];
    float max[3];
} Box;

static void ordered(float a, float b, float *lo, float *hi) {
    *lo = (a < b) ? a : b;
    *hi = (a < b) ? b : a;
}

static Box cube_box(const MapCube *c) {
    Box b;
    ordered(c->x, c->x + c->sx, &b.min[0], &b.max[0]);
    ordered(c->y, c->y + c->sy, &b.min[1], &b.max[1]);
    ordered(c->z, c->z + c->sz, &b.min[2], &b.max[2]);
    return b;
}

static float clampf(float v, float lo, float hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

/* Пересечение луча с AABB (метод «слабов»). Возвращает 1, если луч входит
 * в куб снаружи на расстоянии (0, t_limit]; t_out — дистанция до входа,
 * axis_out и sign_out — ось и знак нормали грани, через которую луч вошёл. */
static int ray_box(const Box *b, const float o[3], const float d[3],
                   float t_limit, float *t_out, int *axis_out, int *sign_out) {
    float t_enter = 0.0f;
    float t_exit = t_limit;
    int axis = -1;
    int sign = 0;

    for (int i = 0; i < 3; i++) {
        if (fabsf(d[i]) < 1e-8f) {
            /* Луч параллелен слабу: мимо, если начало вне его. */
            if (o[i] < b->min[i] || o[i] > b->max[i]) return 0;
            continue;
        }

        const float inv = 1.0f / d[i];
        float t0 = (b->min[i] - o[i]) * inv;
        float t1 = (b->max[i] - o[i]) * inv;
        int face_sign = (inv < 0.0f) ? 1 : -1;   /* нормаль грани входа */
        if (t0 > t1) {
            const float tmp = t0;
            t0 = t1;
            t1 = tmp;
        }

        if (t0 > t_enter) {
            t_enter = t0;
            axis = i;
            sign = face_sign;
        }
        if (t1 < t_exit) t_exit = t1;
        if (t_enter > t_exit) return 0;
    }

    /* axis < 0 — начало луча внутри куба: грани входа нет. */
    if (axis < 0 || t_enter <= 0.0f) return 0;

    *t_out = t_enter;
    *axis_out = axis;
    *sign_out = sign;
    return 1;
}

/* ---------- Прицеливание ---------- */

/* Луч из камеры в центр экрана: ближайший куб или пол y = 0.
 * Заполняет has_hit / hit_cube / place_*. */
static void editor_pick(Editor *ed) {
    float o[3] = { ed->cam.x, ed->cam.y, ed->cam.z };
    float d[3];
    render_view_dir(&ed->cam, &d[0], &d[1], &d[2]);

    float best_t = EDITOR_REACH;
    int best_cube = -1;
    int best_axis = 1;
    int best_sign = 1;

    for (size_t i = 0; i < g_map.count; i++) {
        const Box b = cube_box(&g_map.cubes[i]);
        float t;
        int axis, sign;
        if (ray_box(&b, o, d, best_t, &t, &axis, &sign) && t < best_t) {
            best_t = t;
            best_cube = (int)i;
            best_axis = axis;
            best_sign = sign;
        }
    }

    /* Пол y = 0: на него ставится первый ряд кубов. */
    int ground = 0;
    if (best_cube < 0 && fabsf(d[1]) > 1e-6f) {
        const float t = (0.0f - o[1]) / d[1];
        if (t > 0.0f && t < best_t) {
            best_t = t;
            ground = 1;
        }
    }

    if (best_cube < 0 && !ground) {
        ed->has_hit = 0;
        ed->hit_cube = -1;
        return;
    }

    /* Точка попадания и позиция нового куба: по касательным осям — привязка
     * к метровой сетке, по нормали — вплотную к грани (или на пол). */
    const float hx = o[0] + d[0] * best_t;
    const float hy = o[1] + d[1] * best_t;
    const float hz = o[2] + d[2] * best_t;

    float place[3] = { floorf(hx), floorf(hy), floorf(hz) };
    const float brush[3] = { ed->brush_w, ed->brush_h, ed->brush_w };

    if (ground) {
        place[1] = 0.0f;
    } else {
        const Box b = cube_box(&g_map.cubes[best_cube]);
        if (best_sign > 0) {
            place[best_axis] = b.max[best_axis];
        } else {
            place[best_axis] = b.min[best_axis] - brush[best_axis];
        }
    }

    ed->has_hit = 1;
    ed->hit_cube = ground ? -1 : best_cube;
    ed->place_x = place[0];
    ed->place_y = place[1];
    ed->place_z = place[2];
}

/* ---------- Сессия ---------- */

static void editor_reset(Editor *ed) {
    memset(ed, 0, sizeof(*ed));

    ed->cam.x = 0.0f;
    ed->cam.y = 6.0f;
    ed->cam.z = 10.0f;
    ed->cam.yaw = 0.0f;      /* взгляд на -Z: к началу координат */
    ed->cam.pitch = -0.35f;

    ed->brush_w = 1.0f;
    ed->brush_h = 1.0f;

    ed->material = NULL;
    ed->mat_tile = 0.0f;
    ed->mat_uv = MAP_UV_TILE;

    ed->grid_on = 1;
    ed->hit_cube = -1;
}

void editor_init(Editor *ed) {
    editor_reset(ed);
    map_edit_begin();
}

void editor_init_loaded(Editor *ed, const char *file) {
    editor_reset(ed);
    snprintf(ed->file, sizeof(ed->file), "%s", file ? file : "");
}

/* ---------- Кадр ---------- */

void editor_update(Editor *ed, double dt) {
    const float step = (float)dt;

    /* Обзор мышью (курсор захвачен — см. main.c). */
    double mdx = 0.0, mdy = 0.0;
    win_mouse_delta(&mdx, &mdy);
    ed->cam.yaw   += (float)mdx * ED_LOOK_SENS;
    ed->cam.pitch -= (float)mdy * ED_LOOK_SENS;
    ed->cam.pitch = clampf(ed->cam.pitch, -RENDER_PITCH_LIMIT, RENDER_PITCH_LIMIT);

    /* Полёт: WASD в плоскости взгляда, Space/Ctrl — вверх/вниз. */
    const float forward = (float)(win_key_down(WIN_KEY_W) - win_key_down(WIN_KEY_S));
    const float strafe  = (float)(win_key_down(WIN_KEY_D) - win_key_down(WIN_KEY_A));
    const float lift    = (float)(win_key_down(WIN_KEY_SPACE) - win_key_down(WIN_KEY_CTRL));

    float speed = ED_MOVE_SPEED * step;
    if (win_key_down(WIN_KEY_SHIFT)) speed *= ED_FAST_FACTOR;

    const float dir_x =  sinf(ed->cam.yaw);
    const float dir_z = -cosf(ed->cam.yaw);
    const float right_x = -dir_z;
    const float right_z =  dir_x;

    ed->cam.x += (forward * dir_x + strafe * right_x) * speed;
    ed->cam.z += (forward * dir_z + strafe * right_z) * speed;
    ed->cam.y += lift * speed;
    ed->cam.y = clampf(ed->cam.y, ED_MIN_Y, ED_MAX_Y);

    /* Колесо: высота кисти; с Shift — сечение. */
    const int wheel = (int)win_scroll_delta();
    if (wheel != 0) {
        if (win_key_down(WIN_KEY_SHIFT)) {
            ed->brush_w = clampf(ed->brush_w + (float)wheel,
                                 EDITOR_BRUSH_MIN, EDITOR_BRUSH_MAX);
        } else {
            ed->brush_h = clampf(ed->brush_h + (float)wheel,
                                 EDITOR_BRUSH_MIN, EDITOR_BRUSH_MAX);
        }
    }

    if (win_key_pressed(WIN_KEY_G)) {
        ed->grid_on = !ed->grid_on;
    }

    editor_pick(ed);
}

/* ---------- Правка карты ---------- */

int editor_place(Editor *ed) {
    if (!ed->has_hit) return 0;

    MapCube cube;
    cube.x = ed->place_x;
    cube.y = ed->place_y;
    cube.z = ed->place_z;
    cube.sx = ed->brush_w;
    cube.sy = ed->brush_h;
    cube.sz = ed->brush_w;
    cube.texture = ed->material;
    cube.tile = ed->mat_tile;
    cube.uv = ed->mat_uv;

    if (!map_edit_add(&cube)) return 0;
    ed->dirty = 1;
    return 1;
}

int editor_remove(Editor *ed) {
    if (!ed->has_hit || ed->hit_cube < 0) return 0;

    map_edit_remove((size_t)ed->hit_cube);
    ed->dirty = 1;
    ed->hit_cube = -1;
    ed->has_hit = 0;   /* прицел пересчитается в следующем кадре */
    return 1;
}

int editor_pick_material(Editor *ed) {
    if (!ed->has_hit || ed->hit_cube < 0) return 0;

    const MapCube *c = &g_map.cubes[ed->hit_cube];
    ed->material = c->texture;
    ed->mat_tile = c->tile;
    ed->mat_uv = c->uv;
    return 1;
}

const char *editor_material_name(const Editor *ed) {
    const char *path = map_texture_path(ed->material);
    if (!path) return "ПО УМОЛЧАНИЮ";

    /* В HUD достаточно имени файла. */
    const char *slash = strrchr(path, '/');
    const char *backslash = strrchr(path, '\\');
    if (!slash || (backslash && backslash > slash)) slash = backslash;
    return slash ? slash + 1 : path;
}

/* ---------- Сохранение ---------- */

static int file_exists(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    fclose(f);
    return 1;
}

int editor_save(Editor *ed) {
    if (ed->file[0] == '\0') {
        /* Первое свободное имя mapNN.tfm рядом с редактором. */
        for (int i = 1; i < 100; i++) {
            char candidate[MAP_LIST_PATH_MAX];
            snprintf(candidate, sizeof(candidate), "%s/map%02d.tfm",
                     map_list_game_dir(), i);
            if (!file_exists(candidate)) {
                snprintf(ed->file, sizeof(ed->file), "%s", candidate);
                break;
            }
        }
        if (ed->file[0] == '\0') return 0;   /* 99 карт — хватит всем */
    }

    if (!map_save(ed->file)) return 0;
    ed->dirty = 0;
    return 1;
}

/* ---------- Гизмо: сетка, подсветка, «призрак» ---------- */

static void draw_wire_box(float min_x, float min_y, float min_z,
                          float max_x, float max_y, float max_z) {
    glBegin(GL_LINES);
    /* нижний прямоугольник */
    glVertex3f(min_x, min_y, min_z); glVertex3f(max_x, min_y, min_z);
    glVertex3f(max_x, min_y, min_z); glVertex3f(max_x, min_y, max_z);
    glVertex3f(max_x, min_y, max_z); glVertex3f(min_x, min_y, max_z);
    glVertex3f(min_x, min_y, max_z); glVertex3f(min_x, min_y, min_z);
    /* верхний прямоугольник */
    glVertex3f(min_x, max_y, min_z); glVertex3f(max_x, max_y, min_z);
    glVertex3f(max_x, max_y, min_z); glVertex3f(max_x, max_y, max_z);
    glVertex3f(max_x, max_y, max_z); glVertex3f(min_x, max_y, max_z);
    glVertex3f(min_x, max_y, max_z); glVertex3f(min_x, max_y, min_z);
    /* вертикальные рёбра */
    glVertex3f(min_x, min_y, min_z); glVertex3f(min_x, max_y, min_z);
    glVertex3f(max_x, min_y, min_z); glVertex3f(max_x, max_y, min_z);
    glVertex3f(max_x, min_y, max_z); glVertex3f(max_x, max_y, max_z);
    glVertex3f(min_x, min_y, max_z); glVertex3f(min_x, max_y, max_z);
    glEnd();
}

static void draw_box_faces(float min_x, float min_y, float min_z,
                           float max_x, float max_y, float max_z) {
    glBegin(GL_QUADS);
    /* Y- и Y+ */
    glVertex3f(min_x, min_y, min_z); glVertex3f(max_x, min_y, min_z);
    glVertex3f(max_x, min_y, max_z); glVertex3f(min_x, min_y, max_z);
    glVertex3f(min_x, max_y, min_z); glVertex3f(min_x, max_y, max_z);
    glVertex3f(max_x, max_y, max_z); glVertex3f(max_x, max_y, min_z);
    /* Z- и Z+ */
    glVertex3f(min_x, min_y, min_z); glVertex3f(min_x, max_y, min_z);
    glVertex3f(max_x, max_y, min_z); glVertex3f(max_x, min_y, min_z);
    glVertex3f(min_x, min_y, max_z); glVertex3f(max_x, min_y, max_z);
    glVertex3f(max_x, max_y, max_z); glVertex3f(min_x, max_y, max_z);
    /* X- и X+ */
    glVertex3f(min_x, min_y, min_z); glVertex3f(min_x, min_y, max_z);
    glVertex3f(min_x, max_y, max_z); glVertex3f(min_x, max_y, min_z);
    glVertex3f(max_x, min_y, min_z); glVertex3f(max_x, max_y, min_z);
    glVertex3f(max_x, max_y, max_z); glVertex3f(max_x, min_y, max_z);
    glEnd();
}

/* Сетка на полу: метровые клетки вокруг камеры, оси X и Z выделены. */
static void draw_grid(const Editor *ed) {
    const float y = 0.01f;   /* чуть выше пола, чтобы не мерцать с ним */
    const int cx = (int)floorf(ed->cam.x);
    const int cz = (int)floorf(ed->cam.z);
    const int x0 = cx - EDITOR_GRID_HALF, x1 = cx + EDITOR_GRID_HALF;
    const int z0 = cz - EDITOR_GRID_HALF, z1 = cz + EDITOR_GRID_HALF;

    glLineWidth(1.0f);
    glBegin(GL_LINES);
    for (int x = x0; x <= x1; x++) {
        if (x == 0) continue;   /* ось рисуется отдельно */
        const float shade = (x % 8 == 0) ? 0.30f : 0.16f;
        glColor4f(shade, shade, shade, 1.0f);
        glVertex3f((float)x, y, (float)z0);
        glVertex3f((float)x, y, (float)z1);
    }
    for (int z = z0; z <= z1; z++) {
        if (z == 0) continue;
        const float shade = (z % 8 == 0) ? 0.30f : 0.16f;
        glColor4f(shade, shade, shade, 1.0f);
        glVertex3f((float)x0, y, (float)z);
        glVertex3f((float)x1, y, (float)z);
    }
    glEnd();

    /* Оси мира: X — красная, Z — синяя. По ним видно, где начало карты. */
    glLineWidth(2.0f);
    glBegin(GL_LINES);
    if (z0 <= 0 && 0 <= z1) {
        glColor4f(0.75f, 0.25f, 0.25f, 1.0f);
        glVertex3f((float)x0, y, 0.0f);
        glVertex3f((float)x1, y, 0.0f);
    }
    if (x0 <= 0 && 0 <= x1) {
        glColor4f(0.25f, 0.35f, 0.80f, 1.0f);
        glVertex3f(0.0f, y, (float)z0);
        glVertex3f(0.0f, y, (float)z1);
    }
    glEnd();
    glLineWidth(1.0f);
}

void editor_draw_gizmos(const Editor *ed) {
    /* Мир уже нарисован: текстуры выключены, глубина и туман включены. */

    if (ed->grid_on) {
        draw_grid(ed);
    }

    /* Куб под прицелом: жёлтый контур — его удалит правая кнопка. */
    if (ed->has_hit && ed->hit_cube >= 0) {
        const Box b = cube_box(&g_map.cubes[ed->hit_cube]);
        const float e = 0.004f;   /* зазор, чтобы линии не тонули в гранях */

        glLineWidth(2.0f);
        glColor4f(1.0f, 0.85f, 0.2f, 1.0f);
        draw_wire_box(b.min[0] - e, b.min[1] - e, b.min[2] - e,
                      b.max[0] + e, b.max[1] + e, b.max[2] + e);
    }

    /* «Призрак» кисти: сюда левая кнопка поставит куб. */
    if (ed->has_hit) {
        const float x0 = ed->place_x, y0 = ed->place_y, z0 = ed->place_z;
        const float x1 = x0 + ed->brush_w;
        const float y1 = y0 + ed->brush_h;
        const float z1 = z0 + ed->brush_w;

        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glDepthMask(GL_FALSE);   /* полупрозрачный объём не пишет глубину */
        glColor4f(0.3f, 0.9f, 0.45f, 0.18f);
        draw_box_faces(x0, y0, z0, x1, y1, z1);
        glDepthMask(GL_TRUE);
        glDisable(GL_BLEND);

        glLineWidth(2.0f);
        glColor4f(0.3f, 0.95f, 0.45f, 1.0f);
        draw_wire_box(x0, y0, z0, x1, y1, z1);
    }

    /* Возвращаем состояние: текстуры модулируются текущим цветом,
     * поэтому цвет обязан вернуться к белому. */
    glLineWidth(1.0f);
    glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
}
