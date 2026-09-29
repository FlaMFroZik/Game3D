#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include <GL/gl.h>

#include "editor/editor.h"
#include "map/list.h"
#include "map/map.h"
#include "render/prim.h"
#include "render/render.h"
#include "render/ui.h"
#include "render/window.h"

#define ED_MOVE_SPEED    10.0f
#define ED_FAST_FACTOR   3.0f
#define ED_LOOK_SENS     0.0025f
#define ED_HANDLE_SIZE   7.0f

static const float GRID_SIZES[EDITOR_GRID_SIZES_COUNT] = {
    0.125f, 0.25f, 0.5f, 1.0f, 2.0f, 4.0f, 8.0f, 16.0f, 32.0f, 64.0f
};

typedef struct {
    float min[3];
    float max[3];
} AABB;

static void ordered(float a, float b, float *lo, float *hi) {
    *lo = (a < b) ? a : b;
    *hi = (a < b) ? b : a;
}

static AABB cube_aabb(const MapCube *c) {
    AABB b;
    ordered(c->x, c->x + c->sx, &b.min[0], &b.max[0]);
    ordered(c->y, c->y + c->sy, &b.min[1], &b.max[1]);
    ordered(c->z, c->z + c->sz, &b.min[2], &b.max[2]);
    return b;
}

static float snap_val(float val, float grid) {
    if (grid <= 0.0001f) return val;
    return roundf(val / grid) * grid;
}

static float clamp_f(float v, float lo, float hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

/* Пересечение луча с AABB */
static int ray_aabb(const AABB *b, const float o[3], const float d[3],
                    float t_limit, float *t_out, int *axis_out, int *sign_out) {
    float t_enter = 0.0f;
    float t_exit = t_limit;
    int axis = -1;
    int sign = 0;

    for (int i = 0; i < 3; i++) {
        if (fabsf(d[i]) < 1e-8f) {
            if (o[i] < b->min[i] || o[i] > b->max[i]) return 0;
            continue;
        }
        const float inv = 1.0f / d[i];
        float t0 = (b->min[i] - o[i]) * inv;
        float t1 = (b->max[i] - o[i]) * inv;
        int face_sign = (inv < 0.0f) ? 1 : -1;
        if (t0 > t1) {
            float tmp = t0; t0 = t1; t1 = tmp;
        }
        if (t0 > t_enter) {
            t_enter = t0;
            axis = i;
            sign = face_sign;
        }
        if (t1 < t_exit) t_exit = t1;
        if (t_enter > t_exit) return 0;
    }
    if (axis < 0 || t_enter <= 0.0f) return 0;
    *t_out = t_enter;
    *axis_out = axis;
    *sign_out = sign;
    return 1;
}

/* ---------- Undo / Redo ---------- */

void editor_push_undo(Editor *ed) {
    /* Если мы делали undo и теперь совершаем новое действие, отсекаем ветку redo */
    if (ed->undo_index < ed->undo_count - 1) {
        for (int i = ed->undo_index + 1; i < ed->undo_count; i++) {
            free(ed->undo_stack[i].cubes);
            ed->undo_stack[i].cubes = NULL;
            ed->undo_stack[i].count = 0;
        }
        ed->undo_count = ed->undo_index + 1;
    }

    if (ed->undo_count >= EDITOR_MAX_UNDO) {
        free(ed->undo_stack[0].cubes);
        memmove(&ed->undo_stack[0], &ed->undo_stack[1],
                (EDITOR_MAX_UNDO - 1) * sizeof(ed->undo_stack[0]));
        ed->undo_count--;
        ed->undo_index--;
    }

    size_t count = g_map.count;
    MapCube *copy = NULL;
    if (count > 0) {
        copy = malloc(count * sizeof(MapCube));
        if (copy) {
            memcpy(copy, g_map.cubes, count * sizeof(MapCube));
        }
    }

    ed->undo_index = ed->undo_count;
    ed->undo_stack[ed->undo_index].cubes = copy;
    ed->undo_stack[ed->undo_index].count = count;
    ed->undo_count++;
    ed->dirty = 1;
}

void editor_undo(Editor *ed) {
    if (ed->undo_index <= 0) return;
    ed->undo_index--;

    int target = ed->undo_index;
    g_map.count = 0;
    for (size_t i = 0; i < ed->undo_stack[target].count; i++) {
        map_edit_add(&ed->undo_stack[target].cubes[i]);
    }
    ed->selected_cube = -1;
    ed->dirty = 1;
    snprintf(ed->status_msg, sizeof(ed->status_msg), "Undo выполнен");
}

void editor_redo(Editor *ed) {
    if (ed->undo_index >= ed->undo_count - 1) return;
    ed->undo_index++;

    int target = ed->undo_index;
    g_map.count = 0;
    for (size_t i = 0; i < ed->undo_stack[target].count; i++) {
        map_edit_add(&ed->undo_stack[target].cubes[i]);
    }
    ed->selected_cube = -1;
    ed->dirty = 1;
    snprintf(ed->status_msg, sizeof(ed->status_msg), "Redo выполнен");
}

/* ---------- Инициализация ---------- */

static const char *base_name(const char *path) {
    const char *slash = strrchr(path, '/');
    const char *backslash = strrchr(path, '\\');
    if (!slash || (backslash && backslash > slash)) slash = backslash;
    return slash ? slash + 1 : path;
}

static void editor_reset_state(Editor *ed) {
    memset(ed, 0, sizeof(*ed));

    prim_init_builtins();

    /* 3D Камера */
    ed->cam.x = 0.0f;
    ed->cam.y = 8.0f;
    ed->cam.z = 14.0f;
    ed->cam.yaw = 0.0f;
    ed->cam.pitch = -0.4f;

    /* 2D Камеры */
    for (int i = 0; i < 4; i++) {
        ed->views2d[i].pan_x = 0.0f;
        ed->views2d[i].pan_y = 0.0f;
        ed->views2d[i].zoom = 24.0f; /* 24 пикселя на метр */
    }

    ed->active_viewport = VIEWPORT_TOP;
    ed->maximized_viewport = -1; /* 2x2 */

    ed->tool = TOOL_SELECT;
    ed->primitive = PRIM_BLOCK;
    ed->render_3d_mode = RENDER_3D_TEXTURED;

    ed->selected_cube = -1;
    ed->hovered_cube = -1;

    ed->grid_index = 3; /* 1.0 метр */
    ed->grid_size = GRID_SIZES[ed->grid_index];
    ed->grid_snap = 1;
    ed->grid_visible = 1;

    ed->material = prim_builtin_texture(1); /* dev_measureorange */
    snprintf(ed->material_name, sizeof(ed->material_name), "%s", prim_builtin_name(1));
    ed->mat_tile = 2.0f;
    ed->mat_uv = MAP_UV_TILE;

    ed->hollow_thickness = 0.5f;

    snprintf(ed->status_msg, sizeof(ed->status_msg),
             "Hammer Editor готов. Выберите инструмент Block (Shift+B) или Selection (Shift+S)");
    snprintf(ed->coord_str, sizeof(ed->coord_str), "@ 0.00, 0.00, 0.00");
    snprintf(ed->size_str, sizeof(ed->size_str), "dx: 0.00  dy: 0.00  dz: 0.00");

    map_list_scan(&ed->map_list, map_list_game_dir());
}

void editor_init(Editor *ed) {
    editor_reset_state(ed);
    map_edit_begin();
    editor_push_undo(ed);
    ed->dirty = 0;
}

void editor_init_loaded(Editor *ed, const char *file) {
    editor_reset_state(ed);
    if (file) {
        snprintf(ed->file, sizeof(ed->file), "%s", file);
    }
    editor_push_undo(ed);
    ed->dirty = 0;
}

void editor_shutdown(Editor *ed) {
    for (int i = 0; i < ed->undo_count; i++) {
        free(ed->undo_stack[i].cubes);
        ed->undo_stack[i].cubes = NULL;
    }
    ed->undo_count = 0;
}

/* ---------- Геометрия и разметка видовых экранов ---------- */

static UiRect get_workspace_rect(int win_w, int win_h, float s) {
    const float menu_h   = 24.0f * s;
    const float tool_top = 28.0f * s;
    const float tool_left= 38.0f * s;
    const float dock_w   = 220.0f * s;
    const float status_h = 22.0f * s;

    float x = tool_left;
    float y = menu_h + tool_top;
    float w = (float)win_w - tool_left - dock_w;
    float h = (float)win_h - y - status_h;
    if (w < 100.0f) w = 100.0f;
    if (h < 100.0f) h = 100.0f;

    return ui_rect(x, y, w, h);
}

static UiRect get_viewport_rect(const Editor *ed, ViewportIndex vp_idx, UiRect ws) {
    if (ed->maximized_viewport >= 0) {
        if (ed->maximized_viewport == (int)vp_idx) {
            return ws;
        }
        return ui_rect(0, 0, 0, 0);
    }

    const float half_w = floorf(ws.w * 0.5f);
    const float half_h = floorf(ws.h * 0.5f);

    switch (vp_idx) {
        case VIEWPORT_TOP:   return ui_rect(ws.x,          ws.y,          half_w, half_h);
        case VIEWPORT_3D:    return ui_rect(ws.x + half_w, ws.y,          ws.w - half_w, half_h);
        case VIEWPORT_FRONT: return ui_rect(ws.x,          ws.y + half_h, half_w, ws.h - half_h);
        case VIEWPORT_SIDE:  return ui_rect(ws.x + half_w, ws.y + half_h, ws.w - half_w, ws.h - half_h);
        default:             return ws;
    }
}

/* Преобразования координат 2D-видов */
static void screen_to_world_2d(ViewportIndex vp, View2D v2d, UiRect vp_rect,
                               float sx, float sy, float *out_a, float *out_b) {
    float cx = vp_rect.x + vp_rect.w * 0.5f;
    float cy = vp_rect.y + vp_rect.h * 0.5f;

    float dx = (sx - cx) / v2d.zoom;
    float dy = (sy - cy) / v2d.zoom;

    if (vp == VIEWPORT_TOP) {
        /* a = X (вправо), b = Z (вниз) */
        *out_a = v2d.pan_x + dx;
        *out_b = v2d.pan_y + dy;
    } else {
        /* Front / Side: экранная Y направлена вниз, мировая Y направлена вверх */
        *out_a = v2d.pan_x + dx;
        *out_b = v2d.pan_y - dy;
    }
}

static void world_to_screen_2d(ViewportIndex vp, View2D v2d, UiRect vp_rect,
                               float wa, float wb, float *out_sx, float *out_sy) {
    float cx = vp_rect.x + vp_rect.w * 0.5f;
    float cy = vp_rect.y + vp_rect.h * 0.5f;

    float dx = (wa - v2d.pan_x) * v2d.zoom;
    float dy = (wb - v2d.pan_y) * v2d.zoom;

    if (vp == VIEWPORT_TOP) {
        *out_sx = cx + dx;
        *out_sy = cy + dy;
    } else {
        *out_sx = cx + dx;
        *out_sy = cy - dy;
    }
}

/* Получение 2D-координат AABB для конкретного видового экрана */
static void get_aabb_2d_coords(ViewportIndex vp, const AABB *b,
                               float *min_a, float *min_b, float *max_a, float *max_b) {
    if (vp == VIEWPORT_TOP) {
        *min_a = b->min[0]; *max_a = b->max[0];
        *min_b = b->min[2]; *max_b = b->max[2];
    } else if (vp == VIEWPORT_FRONT) {
        *min_a = b->min[0]; *max_a = b->max[0];
        *min_b = b->min[1]; *max_b = b->max[1];
    } else { /* SIDE */
        *min_a = b->min[2]; *max_a = b->max[2];
        *min_b = b->min[1]; *max_b = b->max[1];
    }
}

/* Применение изменений 2D-координат обратно к AABB */
static void set_aabb_2d_coords(ViewportIndex vp, AABB *b,
                               float min_a, float min_b, float max_a, float max_b) {
    ordered(min_a, max_a, &min_a, &max_a);
    ordered(min_b, max_b, &min_b, &max_b);
    if (vp == VIEWPORT_TOP) {
        b->min[0] = min_a; b->max[0] = max_a;
        b->min[2] = min_b; b->max[2] = max_b;
    } else if (vp == VIEWPORT_FRONT) {
        b->min[0] = min_a; b->max[0] = max_a;
        b->min[1] = min_b; b->max[1] = max_b;
    } else { /* SIDE */
        b->min[2] = min_a; b->max[2] = max_a;
        b->min[1] = min_b; b->max[1] = max_b;
    }
}

/* ---------- 2D Отрисовка видового экрана ---------- */

static void draw_2d_grid(ViewportIndex vp, View2D v2d, UiRect vp_rect, float grid_size) {
    float min_a, min_b, max_a, max_b;
    screen_to_world_2d(vp, v2d, vp_rect, vp_rect.x, vp_rect.y + vp_rect.h, &min_a, &min_b);
    screen_to_world_2d(vp, v2d, vp_rect, vp_rect.x + vp_rect.w, vp_rect.y, &max_a, &max_b);
    ordered(min_a, max_a, &min_a, &max_a);
    ordered(min_b, max_b, &min_b, &max_b);

    /* Адаптивный шаг сетки, если мелкие линии сливаются */
    float draw_step = grid_size;
    while (draw_step * v2d.zoom < 6.0f) {
        draw_step *= 2.0f;
    }

    int start_a = (int)floorf(min_a / draw_step);
    int end_a   = (int)ceilf(max_a / draw_step);
    int start_b = (int)floorf(min_b / draw_step);
    int end_b   = (int)ceilf(max_b / draw_step);

    /* Мелкая сетка */
    glLineWidth(1.0f);
    glBegin(GL_LINES);
    for (int i = start_a; i <= end_a; i++) {
        float wa = (float)i * draw_step;
        if (fabsf(wa) < 1e-4f) continue;
        int is_major = (abs(i) % 8 == 0);
        glColor4fv(is_major ? UI_CLR_GRID_MAJOR : UI_CLR_GRID_GREEN);

        float sx, sy0, sy1;
        world_to_screen_2d(vp, v2d, vp_rect, wa, min_b, &sx, &sy0);
        world_to_screen_2d(vp, v2d, vp_rect, wa, max_b, &sx, &sy1);
        glVertex2f(sx, sy0);
        glVertex2f(sx, sy1);
    }
    for (int j = start_b; j <= end_b; j++) {
        float wb = (float)j * draw_step;
        if (fabsf(wb) < 1e-4f) continue;
        int is_major = (abs(j) % 8 == 0);
        glColor4fv(is_major ? UI_CLR_GRID_MAJOR : UI_CLR_GRID_GREEN);

        float sx0, sx1, sy;
        world_to_screen_2d(vp, v2d, vp_rect, min_a, wb, &sx0, &sy);
        world_to_screen_2d(vp, v2d, vp_rect, max_a, wb, &sx1, &sy);
        glVertex2f(sx0, sy);
        glVertex2f(sx1, sy);
    }
    glEnd();

    /* Оси координат */
    glLineWidth(2.0f);
    glBegin(GL_LINES);
    /* Горизонтальная ось */
    const float *axis_h_clr = (vp == VIEWPORT_SIDE) ? UI_CLR_AXIS_Z : UI_CLR_AXIS_X;
    glColor4fv(axis_h_clr);
    float sx0, sx1, sy_axis;
    world_to_screen_2d(vp, v2d, vp_rect, min_a, 0.0f, &sx0, &sy_axis);
    world_to_screen_2d(vp, v2d, vp_rect, max_a, 0.0f, &sx1, &sy_axis);
    glVertex2f(sx0, sy_axis);
    glVertex2f(sx1, sy_axis);

    /* Вертикальная ось */
    const float *axis_v_clr = (vp == VIEWPORT_TOP) ? UI_CLR_AXIS_Z : UI_CLR_AXIS_Y;
    glColor4fv(axis_v_clr);
    float sx_axis, sy0_axis, sy1_axis;
    world_to_screen_2d(vp, v2d, vp_rect, 0.0f, min_b, &sx_axis, &sy0_axis);
    world_to_screen_2d(vp, v2d, vp_rect, 0.0f, max_b, &sx_axis, &sy1_axis);
    glVertex2f(sx_axis, sy0_axis);
    glVertex2f(sx_axis, sy1_axis);
    glEnd();
    glLineWidth(1.0f);

    /* Центр (0,0) */
    float ox, oy;
    world_to_screen_2d(vp, v2d, vp_rect, 0.0f, 0.0f, &ox, &oy);
    ui_draw_handle(ox, oy, 6.0f, UI_CLR_ACCENT, UI_CLR_PANEL_DARK);
}

static void draw_2d_camera_frustum(const Editor *ed, ViewportIndex vp, View2D v2d, UiRect vp_rect) {
    float eye_a = 0.0f, eye_b = 0.0f;
    float dir_a = 0.0f, dir_b = 0.0f;

    float fwd_x, fwd_y, fwd_z;
    render_view_dir(&ed->cam, &fwd_x, &fwd_y, &fwd_z);

    if (vp == VIEWPORT_TOP) {
        eye_a = ed->cam.x; eye_b = ed->cam.z;
        dir_a = fwd_x;     dir_b = fwd_z;
    } else if (vp == VIEWPORT_FRONT) {
        eye_a = ed->cam.x; eye_b = ed->cam.y;
        dir_a = fwd_x;     dir_b = fwd_y;
    } else { /* SIDE */
        eye_a = ed->cam.z; eye_b = ed->cam.y;
        dir_a = fwd_z;     dir_b = fwd_y;
    }

    float cam_sx, cam_sy;
    world_to_screen_2d(vp, v2d, vp_rect, eye_a, eye_b, &cam_sx, &cam_sy);

    /* Точка камеры */
    const float cam_clr[4] = { 1.0f, 0.2f, 0.2f, 1.0f };
    ui_draw_handle(cam_sx, cam_sy, 8.0f, cam_clr, UI_CLR_PANEL_LIGHT);

    /* Лучи поля зрения камеры */
    float len = sqrtf(dir_a * dir_a + dir_b * dir_b);
    if (len > 0.01f) {
        dir_a /= len;
        dir_b /= len;

        float side_a = -dir_b * 0.5f;
        float side_b =  dir_a * 0.5f;

        float ray_len = 8.0f; /* длина лучей в метрах */
        float left_a = eye_a + (dir_a + side_a) * ray_len;
        float left_b = eye_b + (dir_b + side_b) * ray_len;
        float right_a = eye_a + (dir_a - side_a) * ray_len;
        float right_b = eye_b + (dir_b - side_b) * ray_len;

        float lsx, lsy, rsx, rsy;
        world_to_screen_2d(vp, v2d, vp_rect, left_a, left_b, &lsx, &lsy);
        world_to_screen_2d(vp, v2d, vp_rect, right_a, right_b, &rsx, &rsy);

        glLineWidth(1.5f);
        glColor4fv(cam_clr);
        glBegin(GL_LINES);
        glVertex2f(cam_sx, cam_sy); glVertex2f(lsx, lsy);
        glVertex2f(cam_sx, cam_sy); glVertex2f(rsx, rsy);
        glVertex2f(lsx, lsy);       glVertex2f(rsx, rsy);
        glEnd();
        glLineWidth(1.0f);
    }
}

static void draw_2d_brush(ViewportIndex vp, View2D v2d, UiRect vp_rect,
                          const MapCube *cube, int is_selected, const Ui *ui) {
    AABB b = cube_aabb(cube);
    float min_a, min_b, max_a, max_b;
    get_aabb_2d_coords(vp, &b, &min_a, &min_b, &max_a, &max_b);

    float sx0, sy0, sx1, sy1;
    world_to_screen_2d(vp, v2d, vp_rect, min_a, min_b, &sx0, &sy0);
    world_to_screen_2d(vp, v2d, vp_rect, max_a, max_b, &sx1, &sy1);

    ordered(sx0, sx1, &sx0, &sx1);
    ordered(sy0, sy1, &sy0, &sy1);

    UiRect r = ui_rect(sx0, sy0, sx1 - sx0, sy1 - sy0);

    if (is_selected) {
        /* Красный контур и 8 ручек ресайза */
        ui_border(r, 2.0f, UI_CLR_SELECTION);

        const float hs = ED_HANDLE_SIZE * (ui ? ui->scale : 1.0f);
        const float fill[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
        const float edge[4] = { 0.1f, 0.1f, 0.1f, 1.0f };

        float mid_x = sx0 + (sx1 - sx0) * 0.5f;
        float mid_y = sy0 + (sy1 - sy0) * 0.5f;

        ui_draw_handle(sx0, sy0, hs, fill, edge);
        ui_draw_handle(mid_x, sy0, hs, fill, edge);
        ui_draw_handle(sx1, sy0, hs, fill, edge);
        ui_draw_handle(sx1, mid_y, hs, fill, edge);
        ui_draw_handle(sx1, sy1, hs, fill, edge);
        ui_draw_handle(mid_x, sy1, hs, fill, edge);
        ui_draw_handle(sx0, sy1, hs, fill, edge);
        ui_draw_handle(sx0, mid_y, hs, fill, edge);

        /* Подпись размеров браша */
        if (ui && (sx1 - sx0 > 40.0f) && (sy1 - sy0 > 20.0f)) {
            char dims[64];
            snprintf(dims, sizeof(dims), "%gx%g",
                     (double)fabsf(max_a - min_a), (double)fabsf(max_b - min_b));
            ui_label_in(ui, r, 0.70f, UI_CLR_TEXT_ACCENT, dims);
        }
    } else {
        const float wire_clr[4] = { 0.80f, 0.85f, 0.90f, 0.85f };
        ui_border(r, 1.0f, wire_clr);
    }
}

static void draw_2d_preview(const Editor *ed, ViewportIndex vp, View2D v2d,
                            UiRect vp_rect, const Ui *ui) {
    if (!ed->preview_active) return;

    AABB b;
    b.min[0] = ed->preview_min[0]; b.min[1] = ed->preview_min[1]; b.min[2] = ed->preview_min[2];
    b.max[0] = ed->preview_max[0]; b.max[1] = ed->preview_max[1]; b.max[2] = ed->preview_max[2];

    float min_a, min_b, max_a, max_b;
    get_aabb_2d_coords(vp, &b, &min_a, &min_b, &max_a, &max_b);

    float sx0, sy0, sx1, sy1;
    world_to_screen_2d(vp, v2d, vp_rect, min_a, min_b, &sx0, &sy0);
    world_to_screen_2d(vp, v2d, vp_rect, max_a, max_b, &sx1, &sy1);
    ordered(sx0, sx1, &sx0, &sx1);
    ordered(sy0, sy1, &sy0, &sy1);

    UiRect r = ui_rect(sx0, sy0, sx1 - sx0, sy1 - sy0);

    /* Полупрозрачная заливка и пунктирный/желтый контур */
    const float prev_fill[4] = { 0.95f, 0.60f, 0.15f, 0.20f };
    ui_fill(r, prev_fill);
    ui_border(r, 2.0f, UI_CLR_ACCENT);

    /* Диагонали */
    ui_line(sx0, sy0, sx1, sy1, 1.0f, UI_CLR_ACCENT);
    ui_line(sx0, sy1, sx1, sy0, 1.0f, UI_CLR_ACCENT);

    /* Размеры */
    if (ui) {
        char dims[64];
        snprintf(dims, sizeof(dims), "%gx%g",
                 (double)fabsf(max_a - min_a), (double)fabsf(max_b - min_b));
        ui_label_in(ui, r, 0.75f, UI_CLR_TEXT_ACCENT, dims);
    }
}

/* ---------- 3D Отрисовка видового экрана ---------- */

static void draw_3d_viewport(Editor *ed, const Renderer *r, UiRect vp_rect, int win_h) {
    if (vp_rect.w <= 0 || vp_rect.h <= 0) return;

    /* Viewport в системе координат окна OpenGL (Y снизу) */
    GLint gl_x = (GLint)vp_rect.x;
    GLint gl_y = (GLint)(win_h - (vp_rect.y + vp_rect.h));
    GLsizei gl_w = (GLsizei)vp_rect.w;
    GLsizei gl_h = (GLsizei)vp_rect.h;

    glViewport(gl_x, gl_y, gl_w, gl_h);
    glScissor(gl_x, gl_y, gl_w, gl_h);
    glEnable(GL_SCISSOR_TEST);

    render_clear(r);
    render_camera(r, &ed->cam, gl_w, gl_h);

    /* Сетка на полу Y=0 */
    if (ed->grid_visible) {
        const float grid_y = 0.005f;
        const int half_grid = 32;
        glLineWidth(1.0f);
        glBegin(GL_LINES);
        for (int x = -half_grid; x <= half_grid; x++) {
            if (x == 0) continue;
            float shade = (x % 8 == 0) ? 0.35f : 0.18f;
            glColor4f(shade, shade, shade, 1.0f);
            glVertex3f((float)x, grid_y, (float)-half_grid);
            glVertex3f((float)x, grid_y, (float)half_grid);
        }
        for (int z = -half_grid; z <= half_grid; z++) {
            if (z == 0) continue;
            float shade = (z % 8 == 0) ? 0.35f : 0.18f;
            glColor4f(shade, shade, shade, 1.0f);
            glVertex3f((float)-half_grid, grid_y, (float)z);
            glVertex3f((float)half_grid,  grid_y, (float)z);
        }
        glEnd();

        /* Оси X (красная) и Z (синяя) */
        glLineWidth(2.0f);
        glBegin(GL_LINES);
        glColor4fv(UI_CLR_AXIS_X);
        glVertex3f((float)-half_grid, grid_y, 0.0f);
        glVertex3f((float)half_grid,  grid_y, 0.0f);
        glColor4fv(UI_CLR_AXIS_Z);
        glVertex3f(0.0f, grid_y, (float)-half_grid);
        glVertex3f(0.0f, grid_y, (float)half_grid);
        glEnd();
        glLineWidth(1.0f);
    }

    /* Мир (кубы) */
    if (ed->render_3d_mode == RENDER_3D_TEXTURED) {
        render_world(r);
    } else if (ed->render_3d_mode == RENDER_3D_FLAT) {
        glDisable(GL_TEXTURE_2D);
        for (size_t i = 0; i < g_map.count; i++) {
            AABB b = cube_aabb(&g_map.cubes[i]);
            glColor4f(0.6f, 0.65f, 0.7f, 1.0f);
            prim_draw_solid_box(b.min[0], b.min[1], b.min[2], b.max[0], b.max[1], b.max[2]);
            glColor4f(0.2f, 0.22f, 0.25f, 1.0f);
            prim_draw_wire_box(b.min[0], b.min[1], b.min[2], b.max[0], b.max[1], b.max[2]);
        }
    } else { /* WIREFRAME */
        glDisable(GL_TEXTURE_2D);
        glColor4fv(UI_CLR_TEXT);
        for (size_t i = 0; i < g_map.count; i++) {
            AABB b = cube_aabb(&g_map.cubes[i]);
            prim_draw_wire_box(b.min[0], b.min[1], b.min[2], b.max[0], b.max[1], b.max[2]);
        }
    }

    /* Подсветка выделенного куба в 3D */
    if (ed->selected_cube >= 0 && (size_t)ed->selected_cube < g_map.count) {
        AABB b = cube_aabb(&g_map.cubes[ed->selected_cube]);
        const float eps = 0.01f;

        glDisable(GL_TEXTURE_2D);
        glLineWidth(2.5f);
        glColor4fv(UI_CLR_SELECTION);
        prim_draw_wire_box(b.min[0] - eps, b.min[1] - eps, b.min[2] - eps,
                           b.max[0] + eps, b.max[1] + eps, b.max[2] + eps);
        glLineWidth(1.0f);
    }

    /* Подсветка куба под курсором в 3D */
    if (ed->hovered_cube >= 0 && ed->hovered_cube != ed->selected_cube &&
        (size_t)ed->hovered_cube < g_map.count) {
        AABB b = cube_aabb(&g_map.cubes[ed->hovered_cube]);
        const float eps = 0.005f;

        glDisable(GL_TEXTURE_2D);
        glLineWidth(1.5f);
        glColor4fv(UI_CLR_ACCENT);
        prim_draw_wire_box(b.min[0] - eps, b.min[1] - eps, b.min[2] - eps,
                           b.max[0] + eps, b.max[1] + eps, b.max[2] + eps);
        glLineWidth(1.0f);
    }

    /* Призрак нового куба (Block Tool preview) в 3D */
    if (ed->preview_active) {
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glDepthMask(GL_FALSE);
        glColor4f(0.95f, 0.60f, 0.15f, 0.25f);
        prim_draw_solid_box(ed->preview_min[0], ed->preview_min[1], ed->preview_min[2],
                            ed->preview_max[0], ed->preview_max[1], ed->preview_max[2]);
        glDepthMask(GL_TRUE);
        glDisable(GL_BLEND);

        glLineWidth(2.0f);
        glColor4fv(UI_CLR_ACCENT);
        prim_draw_wire_box(ed->preview_min[0], ed->preview_min[1], ed->preview_min[2],
                           ed->preview_max[0], ed->preview_max[1], ed->preview_max[2]);
        glLineWidth(1.0f);
    }

    glDisable(GL_SCISSOR_TEST);
    glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
}

/* ---------- Главная отрисовка видовых экранов ---------- */

void editor_render_viewports(Editor *ed, const Renderer *r, int win_w, int win_h) {
    const float s = (float)win_h / 720.0f;
    UiRect ws = get_workspace_rect(win_w, win_h, s);

    /* 1. Отрисовка 3D Viewport */
    UiRect r3d = get_viewport_rect(ed, VIEWPORT_3D, ws);
    if (r3d.w > 0 && r3d.h > 0) {
        draw_3d_viewport(ed, r, r3d, win_h);
    }

    /* 2. Подготовка 2D-рендера для трех ортогональных видов */
    glViewport(0, 0, win_w, win_h);
    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glLoadIdentity();
    glOrtho(0.0, (GLdouble)win_w, (GLdouble)win_h, 0.0, -100.0, 100.0);
    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glLoadIdentity();

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_FOG);
    glDisable(GL_TEXTURE_2D);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    ViewportIndex views_2d[3] = { VIEWPORT_TOP, VIEWPORT_FRONT, VIEWPORT_SIDE };
    for (int v = 0; v < 3; v++) {
        ViewportIndex vp = views_2d[v];
        UiRect vpr = get_viewport_rect(ed, vp, ws);
        if (vpr.w <= 0 || vpr.h <= 0) continue;

        /* Фон видового экрана */
        const float bg_2d[4] = { 0.04f, 0.05f, 0.06f, 1.0f };
        ui_fill(vpr, bg_2d);

        /* Сетка */
        draw_2d_grid(vp, ed->views2d[vp], vpr, ed->grid_size);

        /* Браши карты */
        for (size_t i = 0; i < g_map.count; i++) {
            int is_sel = (ed->selected_cube == (int)i);
            draw_2d_brush(vp, ed->views2d[vp], vpr, &g_map.cubes[i], is_sel, NULL);
        }

        /* Превью нового браша */
        draw_2d_preview(ed, vp, ed->views2d[vp], vpr, NULL);

        /* Камера и конус обзора */
        draw_2d_camera_frustum(ed, vp, ed->views2d[vp], vpr);

        /* Рамка экрана */
        int is_act = (ed->active_viewport == (int)vp);
        ui_border(vpr, 1.0f, is_act ? UI_CLR_VIEWPORT_ACTIVE : UI_CLR_VIEWPORT_BORDER);
    }

    /* Рамка 3D Viewport */
    if (r3d.w > 0 && r3d.h > 0) {
        int is_act = (ed->active_viewport == VIEWPORT_3D);
        ui_border(r3d, 1.0f, is_act ? UI_CLR_VIEWPORT_ACTIVE : UI_CLR_VIEWPORT_BORDER);
    }

    glDisable(GL_BLEND);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_FOG);

    glMatrixMode(GL_MODELVIEW);
    glPopMatrix();
    glMatrixMode(GL_PROJECTION);
    glPopMatrix();
    glMatrixMode(GL_MODELVIEW);
}

/* ---------- Операции над брашами ---------- */

void editor_create_brush(Editor *ed) {
    if (!ed->preview_active) return;

    MapCube c;
    ordered(ed->preview_min[0], ed->preview_max[0], &c.x, &c.sx);
    c.sx -= c.x;
    ordered(ed->preview_min[1], ed->preview_max[1], &c.y, &c.sy);
    c.sy -= c.y;
    ordered(ed->preview_min[2], ed->preview_max[2], &c.z, &c.sz);
    c.sz -= c.z;

    if (c.sx < 0.1f) c.sx = ed->grid_size;
    if (c.sy < 0.1f) c.sy = ed->grid_size;
    if (c.sz < 0.1f) c.sz = ed->grid_size;

    c.texture = ed->material;
    c.tile = ed->mat_tile;
    c.uv = ed->mat_uv;

    if (map_edit_add(&c)) {
        ed->selected_cube = (int)g_map.count - 1;
        ed->preview_active = 0;
        editor_push_undo(ed);
        snprintf(ed->status_msg, sizeof(ed->status_msg), "Создан браш: %gx%gx%g м",
                 (double)c.sx, (double)c.sy, (double)c.sz);
    }
}

void editor_delete_selected(Editor *ed) {
    if (ed->selected_cube < 0 || (size_t)ed->selected_cube >= g_map.count) return;

    map_edit_remove((size_t)ed->selected_cube);
    ed->selected_cube = -1;
    editor_push_undo(ed);
    snprintf(ed->status_msg, sizeof(ed->status_msg), "Браш удален");
}

void editor_duplicate_selected(Editor *ed) {
    if (ed->selected_cube < 0 || (size_t)ed->selected_cube >= g_map.count) return;

    MapCube c = g_map.cubes[ed->selected_cube];
    c.x += ed->grid_size;
    c.z += ed->grid_size;

    if (map_edit_add(&c)) {
        ed->selected_cube = (int)g_map.count - 1;
        editor_push_undo(ed);
        snprintf(ed->status_msg, sizeof(ed->status_msg), "Браш продублирован");
    }
}

void editor_hollow_selected(Editor *ed) {
    if (ed->selected_cube < 0 || (size_t)ed->selected_cube >= g_map.count) return;

    MapCube orig = g_map.cubes[ed->selected_cube];
    map_edit_remove((size_t)ed->selected_cube);

    float t = ed->hollow_thickness;
    if (t > orig.sx * 0.45f) t = orig.sx * 0.45f;
    if (t > orig.sy * 0.45f) t = orig.sy * 0.45f;
    if (t > orig.sz * 0.45f) t = orig.sz * 0.45f;

    /* Пол */
    MapCube floor_c = orig;
    floor_c.sy = t;
    map_edit_add(&floor_c);

    /* Потолок */
    MapCube ceil_c = orig;
    ceil_c.y = orig.y + orig.sy - t;
    ceil_c.sy = t;
    map_edit_add(&ceil_c);

    /* 4 Стены */
    MapCube wall_left = orig;
    wall_left.y += t; wall_left.sy -= 2.0f * t;
    wall_left.sx = t;
    map_edit_add(&wall_left);

    MapCube wall_right = orig;
    wall_right.y += t; wall_right.sy -= 2.0f * t;
    wall_right.x = orig.x + orig.sx - t;
    wall_right.sx = t;
    map_edit_add(&wall_right);

    MapCube wall_front = orig;
    wall_front.y += t; wall_front.sy -= 2.0f * t;
    wall_front.x += t; wall_front.sx -= 2.0f * t;
    wall_front.sz = t;
    map_edit_add(&wall_front);

    MapCube wall_back = orig;
    wall_back.y += t; wall_back.sy -= 2.0f * t;
    wall_back.x += t; wall_back.sx -= 2.0f * t;
    wall_back.z = orig.z + orig.sz - t;
    wall_back.sz = t;
    map_edit_add(&wall_back);

    ed->selected_cube = (int)g_map.count - 1;
    editor_push_undo(ed);
    snprintf(ed->status_msg, sizeof(ed->status_msg), "Создана полая комната (6 стен, толщина %gм)", (double)t);
}

void editor_apply_material_to_selected(Editor *ed) {
    if (ed->selected_cube < 0 || (size_t)ed->selected_cube >= g_map.count) return;

    g_map.cubes[ed->selected_cube].texture = ed->material;
    g_map.cubes[ed->selected_cube].tile = ed->mat_tile;
    g_map.cubes[ed->selected_cube].uv = ed->mat_uv;
    editor_push_undo(ed);
    snprintf(ed->status_msg, sizeof(ed->status_msg), "Материал '%s' наложен на выделенный браш", editor_material_name(ed));
}

void editor_pick_material_from_selected(Editor *ed) {
    if (ed->selected_cube < 0 || (size_t)ed->selected_cube >= g_map.count) return;

    const MapCube *c = &g_map.cubes[ed->selected_cube];
    ed->material = c->texture;
    ed->mat_tile = c->tile;
    ed->mat_uv = c->uv;
    const char *p = map_texture_path(c->texture);
    snprintf(ed->material_name, sizeof(ed->material_name), "%s", p ? p : "default");
    snprintf(ed->status_msg, sizeof(ed->status_msg), "Материал '%s' скопирован в кисть", editor_material_name(ed));
}

void editor_select_all(Editor *ed) {
    if (g_map.count > 0) {
        ed->selected_cube = 0;
        snprintf(ed->status_msg, sizeof(ed->status_msg), "Выделено solids: %d", (int)g_map.count);
    }
}

void editor_clear_selection(Editor *ed) {
    ed->selected_cube = -1;
    ed->preview_active = 0;
}

void editor_set_tool(Editor *ed, EditorTool tool) {
    ed->tool = tool;
    const char *names[TOOL_COUNT] = {
        "Selection Tool (Shift+S) — Выделение и изменение размеров",
        "Magnify Tool (Shift+G) — Масштабирование вида",
        "Camera Tool (Shift+C) — Размещение 3D-камеры",
        "Entity Tool (Shift+E) — Размещение объектов",
        "Block Tool (Shift+B) — Создание брашей (Enter для завершения)",
        "Face Edit Tool (Shift+A) — Настройка текстур граней",
        "Apply Current Texture (Shift+T) — Наложить текущую текстуру",
        "Clip Tool (Shift+X) — Срез брашей плоскостью",
        "Hollow Tool — Создание полой комнаты"
    };
    if (tool < TOOL_COUNT) {
        snprintf(ed->status_msg, sizeof(ed->status_msg), "%s", names[tool]);
    }
}

void editor_grid_smaller(Editor *ed) {
    if (ed->grid_index > 0) {
        ed->grid_index--;
        ed->grid_size = GRID_SIZES[ed->grid_index];
        snprintf(ed->status_msg, sizeof(ed->status_msg), "Сетка: %g м", (double)ed->grid_size);
    }
}

void editor_grid_larger(Editor *ed) {
    if (ed->grid_index < EDITOR_GRID_SIZES_COUNT - 1) {
        ed->grid_index++;
        ed->grid_size = GRID_SIZES[ed->grid_index];
        snprintf(ed->status_msg, sizeof(ed->status_msg), "Сетка: %g м", (double)ed->grid_size);
    }
}

void editor_toggle_snap(Editor *ed) {
    ed->grid_snap = !ed->grid_snap;
    snprintf(ed->status_msg, sizeof(ed->status_msg), "Привязка к сетке: %s", ed->grid_snap ? "ВКЛ" : "ВЫКЛ");
}

void editor_toggle_maximize(Editor *ed) {
    if (ed->maximized_viewport >= 0) {
        ed->maximized_viewport = -1;
    } else {
        ed->maximized_viewport = ed->active_viewport;
    }
}

void editor_toggle_3d_freelook(Editor *ed, void *window) {
    ed->freelook_3d = !ed->freelook_3d;
    win_set_mouse_captured((WinWindow*)window, ed->freelook_3d);
    if (ed->freelook_3d) {
        ed->active_viewport = VIEWPORT_3D;
        snprintf(ed->status_msg, sizeof(ed->status_msg), "3D Freelook (WASD + мышь). Нажмите Z или Esc для выхода");
    } else {
        snprintf(ed->status_msg, sizeof(ed->status_msg), "Hammer GUI режим");
    }
}

/* ---------- Проверка карты и компиляция ---------- */

void editor_check_problems(Editor *ed) {
    ed->problem_count = 0;
    if (g_map.count == 0) {
        snprintf(ed->problem_list[ed->problem_count++], 128, "Карта пуста (0 solids)");
    }
    for (size_t i = 0; i < g_map.count; i++) {
        const MapCube *c = &g_map.cubes[i];
        if (c->sx < 0.05f || c->sy < 0.05f || c->sz < 0.05f) {
            snprintf(ed->problem_list[ed->problem_count++], 128, "Микро-браш #%d: слишком малый объем", (int)i);
            if (ed->problem_count >= 7) break;
        }
        if (fabsf(c->x) > 500.0f || fabsf(c->y) > 500.0f || fabsf(c->z) > 500.0f) {
            snprintf(ed->problem_list[ed->problem_count++], 128, "Браш #%d вне границ мира (> 500м)", (int)i);
            if (ed->problem_count >= 7) break;
        }
    }
    if (ed->problem_count == 0) {
        snprintf(ed->problem_list[0], 128, "Ошибок не найдено! Карта готова к компиляции.");
        ed->problem_count = 1;
    }
    ed->modal = MODAL_CHECK_PROBLEMS;
}

void editor_run_compile(Editor *ed) {
    ed->modal = MODAL_RUN_MAP;
    ed->compile_stage = 1;
    ed->compile_timer = win_time_seconds();
    snprintf(ed->compile_log, sizeof(ed->compile_log),
             "** Executing...\n"
             "** Command: vbsp.exe\n"
             "** Parameters: -game \"game3d\" \"%s\"\n\n"
             "Valve Software - vbsp.exe (Sep 27 2026)\n"
             "Solids: %d, Faces: %d\n"
             "Building BSP tree...\n"
             "Writing %s.bsp\n\n"
             "** Command: vvis.exe\n"
             "BasePortalVis: 100%%\n"
             "PortalFlow: 100%%\n\n"
             "** Command: vrad.exe\n"
             "Direct Lighting: 100%%\n"
             "Bounce Lighting: 100%%\n\n"
             "Build complete: 0 errors, 0 warnings.\n"
             "Map ready to play!\n",
             ed->file[0] ? ed->file : "untitled.tfm",
             (int)g_map.count, (int)g_map.count * 6,
             ed->file[0] ? ed->file : "untitled");
}

/* ---------- Сохранение / Загрузка ---------- */

int editor_save(Editor *ed) {
    if (ed->file[0] == '\0') {
        for (int i = 1; i < 100; i++) {
            char candidate[MAP_LIST_PATH_MAX];
            snprintf(candidate, sizeof(candidate), "%s/map%02d.tfm", map_list_game_dir(), i);
            FILE *f = fopen(candidate, "rb");
            if (!f) {
                snprintf(ed->file, sizeof(ed->file), "%s", candidate);
                break;
            }
            fclose(f);
        }
    }
    if (ed->file[0] == '\0') return 0;

    if (map_save(ed->file)) {
        ed->dirty = 0;
        snprintf(ed->status_msg, sizeof(ed->status_msg), "Карта сохранена: %.64s", base_name(ed->file));
        return 1;
    }
    return 0;
}

int editor_save_as(Editor *ed, const char *path) {
    if (!path || path[0] == '\0') return 0;
    snprintf(ed->file, sizeof(ed->file), "%s", path);
    return editor_save(ed);
}

int editor_open(Editor *ed, const char *path) {
    if (!path || !map_load(path)) return 0;
    editor_init_loaded(ed, path);
    snprintf(ed->status_msg, sizeof(ed->status_msg), "Карта загружена: %.64s (%d solids)", base_name(path), (int)g_map.count);
    return 1;
}

void editor_new_map(Editor *ed) {
    editor_init(ed);
    snprintf(ed->status_msg, sizeof(ed->status_msg), "Создана новая карта");
}

const char *editor_material_name(const Editor *ed) {
    if (ed->material_name[0]) return ed->material_name;
    const char *p = map_texture_path(ed->material);
    return p ? p : "toolsnodraw";
}

/* ---------- Кадр обновления (ввод) ---------- */

void editor_update(Editor *ed, double dt, int win_w, int win_h) {
    const float s = (float)win_h / 720.0f;
    UiRect ws = get_workspace_rect(win_w, win_h, s);

    int mouse_x, mouse_y;
    win_pointer_pixels(NULL, &mouse_x, &mouse_y);
    float mx = (float)mouse_x;
    float my = (float)mouse_y;

    /* Режим 3D Freelook (Z) */
    if (ed->freelook_3d) {
        double mdx = 0.0, mdy = 0.0;
        win_mouse_delta(&mdx, &mdy);
        ed->cam.yaw   += (float)mdx * ED_LOOK_SENS;
        ed->cam.pitch -= (float)mdy * ED_LOOK_SENS;
        ed->cam.pitch = clamp_f(ed->cam.pitch, -RENDER_PITCH_LIMIT, RENDER_PITCH_LIMIT);

        const float forward = (float)(win_key_down(WIN_KEY_W) - win_key_down(WIN_KEY_S));
        const float strafe  = (float)(win_key_down(WIN_KEY_D) - win_key_down(WIN_KEY_A));
        const float lift    = (float)(win_key_down(WIN_KEY_SPACE) - win_key_down(WIN_KEY_CTRL));

        float speed = ED_MOVE_SPEED * (float)dt;
        if (win_shift_down()) speed *= ED_FAST_FACTOR;

        const float dir_x = sinf(ed->cam.yaw);
        const float dir_z = -cosf(ed->cam.yaw);
        const float right_x = -dir_z;
        const float right_z = dir_x;

        ed->cam.x += (forward * dir_x + strafe * right_x) * speed;
        ed->cam.z += (forward * dir_z + strafe * right_z) * speed;
        ed->cam.y += lift * speed;

        if (win_key_pressed(WIN_KEY_Z) || win_escape_pressed()) {
            ed->freelook_3d = 0;
            win_set_mouse_captured(NULL, 0);
        }
        return;
    }

    /* Горячие клавиши Hammer */
    if (win_ctrl_down() && win_key_pressed(WIN_KEY_Z)) { editor_undo(ed); return; }
    if (win_ctrl_down() && win_key_pressed(WIN_KEY_Y)) { editor_redo(ed); return; }
    if (win_ctrl_down() && win_key_pressed(WIN_KEY_S)) { editor_save(ed); return; }
    if (win_ctrl_down() && win_key_pressed(WIN_KEY_N)) { editor_new_map(ed); return; }
    if (win_ctrl_down() && win_key_pressed(WIN_KEY_O)) { ed->modal = MODAL_OPEN_MAP; return; }
    if (win_ctrl_down() && win_key_pressed(WIN_KEY_D)) { editor_duplicate_selected(ed); return; }
    if (win_ctrl_down() && win_key_pressed(WIN_KEY_A)) { editor_select_all(ed); return; }
    if (win_alt_down()  && win_key_pressed(WIN_KEY_P)) { editor_check_problems(ed); return; }

    if (win_shift_down() && win_key_pressed(WIN_KEY_S)) { editor_set_tool(ed, TOOL_SELECT); return; }
    if (win_shift_down() && win_key_pressed(WIN_KEY_B)) { editor_set_tool(ed, TOOL_BLOCK); return; }
    if (win_shift_down() && win_key_pressed(WIN_KEY_C)) { editor_set_tool(ed, TOOL_CAMERA); return; }
    if (win_shift_down() && win_key_pressed(WIN_KEY_A)) { editor_set_tool(ed, TOOL_TEXTURE); return; }
    if (win_shift_down() && win_key_pressed(WIN_KEY_T)) { editor_apply_material_to_selected(ed); return; }
    if (win_shift_down() && win_key_pressed(WIN_KEY_G)) { editor_set_tool(ed, TOOL_ZOOM); return; }
    if (win_shift_down() && win_key_pressed(WIN_KEY_X)) { editor_set_tool(ed, TOOL_CLIP); return; }
    if (win_shift_down() && win_key_pressed(WIN_KEY_Z)) { editor_toggle_maximize(ed); return; }

    if (win_key_pressed(WIN_KEY_Z)) { editor_toggle_3d_freelook(ed, NULL); return; }
    if (win_key_pressed(WIN_KEY_F9)) { editor_run_compile(ed); return; }
    if (win_key_pressed(WIN_KEY_BRACKET_LEFT)) { editor_grid_smaller(ed); return; }
    if (win_key_pressed(WIN_KEY_BRACKET_RIGHT)) { editor_grid_larger(ed); return; }
    if (win_key_pressed(WIN_KEY_DELETE) || win_key_pressed(WIN_KEY_BACKSPACE)) { editor_delete_selected(ed); return; }
    if (win_key_pressed(WIN_KEY_ENTER)) { editor_create_brush(ed); return; }
    if (win_escape_pressed()) {
        if (ed->modal != MODAL_NONE) ed->modal = MODAL_NONE;
        else if (ed->active_menu != MENU_NONE) ed->active_menu = MENU_NONE;
        else editor_clear_selection(ed);
        return;
    }

    /* Определение активного видового экрана под курсором */
    for (int i = 0; i < 4; i++) {
        UiRect vpr = get_viewport_rect(ed, (ViewportIndex)i, ws);
        if (ui_contains(NULL, vpr)) {
            /* Обновление мировых координат курсора для статус-бара */
            if (i != VIEWPORT_3D) {
                float wa, wb;
                screen_to_world_2d((ViewportIndex)i, ed->views2d[i], vpr, mx, my, &wa, &wb);
                if (ed->grid_snap) {
                    wa = snap_val(wa, ed->grid_size);
                    wb = snap_val(wb, ed->grid_size);
                }
                if (i == VIEWPORT_TOP) {
                    snprintf(ed->coord_str, sizeof(ed->coord_str), "@ X: %.2f  Z: %.2f", (double)wa, (double)wb);
                } else if (i == VIEWPORT_FRONT) {
                    snprintf(ed->coord_str, sizeof(ed->coord_str), "@ X: %.2f  Y: %.2f", (double)wa, (double)wb);
                } else {
                    snprintf(ed->coord_str, sizeof(ed->coord_str), "@ Z: %.2f  Y: %.2f", (double)wa, (double)wb);
                }
            }
            break;
        }
    }

    /* Обработка колеса мыши (Zoom в 2D-видах) */
    double wheel = win_scroll_delta();
    if (wheel != 0.0) {
        for (int i = 0; i < 4; i++) {
            if (i == VIEWPORT_3D) continue;
            UiRect vpr = get_viewport_rect(ed, (ViewportIndex)i, ws);
            if (ui_contains(NULL, vpr)) {
                float factor = (wheel > 0) ? 1.25f : 0.8f;
                ed->views2d[i].zoom *= factor;
                if (ed->views2d[i].zoom < 2.0f) ed->views2d[i].zoom = 2.0f;
                if (ed->views2d[i].zoom > 256.0f) ed->views2d[i].zoom = 256.0f;
                break;
            }
        }
    }

    /* Мышь в 2D видовых экранах */
    for (int v = 0; v < 4; v++) {
        if (v == VIEWPORT_3D) continue;
        ViewportIndex vp = (ViewportIndex)v;
        UiRect vpr = get_viewport_rect(ed, vp, ws);
        if (!ui_contains(NULL, vpr)) continue;

        /* Панорамирование правой или средней кнопкой */
        if (win_mouse_down(1) || win_mouse_down(2)) {
            double mdx = 0.0, mdy = 0.0;
            win_mouse_delta(&mdx, &mdy);
            ed->views2d[vp].pan_x -= (float)mdx / ed->views2d[vp].zoom;
            if (vp == VIEWPORT_TOP) {
                ed->views2d[vp].pan_y -= (float)mdy / ed->views2d[vp].zoom;
            } else {
                ed->views2d[vp].pan_y += (float)mdy / ed->views2d[vp].zoom;
            }
        }

        /* Клик левой кнопкой мыши */
        if (win_mouse_clicked(0)) {
            ed->active_viewport = vp;
            float wa, wb;
            screen_to_world_2d(vp, ed->views2d[vp], vpr, mx, my, &wa, &wb);
            if (ed->grid_snap) {
                wa = snap_val(wa, ed->grid_size);
                wb = snap_val(wb, ed->grid_size);
            }

            if (ed->tool == TOOL_BLOCK) {
                ed->preview_active = 1;
                ed->preview_drag_view = vp;
                ed->is_dragging = 1;
                ed->drag_start_mouse[0] = wa;
                ed->drag_start_mouse[1] = wb;

                /* Инициализация превью */
                if (vp == VIEWPORT_TOP) {
                    ed->preview_min[0] = wa; ed->preview_max[0] = wa + ed->grid_size;
                    ed->preview_min[2] = wb; ed->preview_max[2] = wb + ed->grid_size;
                    ed->preview_min[1] = 0.0f; ed->preview_max[1] = ed->grid_size * 2.0f;
                } else if (vp == VIEWPORT_FRONT) {
                    ed->preview_min[0] = wa; ed->preview_max[0] = wa + ed->grid_size;
                    ed->preview_min[1] = wb; ed->preview_max[1] = wb + ed->grid_size;
                    ed->preview_min[2] = 0.0f; ed->preview_max[2] = ed->grid_size * 2.0f;
                } else {
                    ed->preview_min[2] = wa; ed->preview_max[2] = wa + ed->grid_size;
                    ed->preview_min[1] = wb; ed->preview_max[1] = wb + ed->grid_size;
                    ed->preview_min[0] = 0.0f; ed->preview_max[0] = ed->grid_size * 2.0f;
                }
            } else if (ed->tool == TOOL_SELECT) {
                /* Проверяем клик по брашу */
                int hit = -1;
                for (int i = (int)g_map.count - 1; i >= 0; i--) {
                    AABB b = cube_aabb(&g_map.cubes[i]);
                    float min_a, min_b, max_a, max_b;
                    get_aabb_2d_coords(vp, &b, &min_a, &min_b, &max_a, &max_b);
                    if (wa >= min_a && wa <= max_a && wb >= min_b && wb <= max_b) {
                        hit = i;
                        break;
                    }
                }
                ed->selected_cube = hit;
                if (hit >= 0) {
                    ed->is_dragging = 1;
                    ed->drag_handle = 8; /* перемещение всего браша */
                    ed->drag_start_mouse[0] = wa;
                    ed->drag_start_mouse[1] = wb;
                    AABB b = cube_aabb(&g_map.cubes[hit]);
                    memcpy(ed->drag_orig_min, b.min, sizeof(b.min));
                    memcpy(ed->drag_orig_max, b.max, sizeof(b.max));
                }
            }
        }

        /* Перетаскивание во время удержания левой кнопки */
        if (win_mouse_down(0) && ed->is_dragging) {
            float wa, wb;
            screen_to_world_2d(vp, ed->views2d[vp], vpr, mx, my, &wa, &wb);
            if (ed->grid_snap) {
                wa = snap_val(wa, ed->grid_size);
                wb = snap_val(wb, ed->grid_size);
            }

            if (ed->tool == TOOL_BLOCK && ed->preview_active) {
                float start_a = ed->drag_start_mouse[0];
                float start_b = ed->drag_start_mouse[1];
                float min_a, max_a, min_b, max_b;
                ordered(start_a, wa, &min_a, &max_a);
                ordered(start_b, wb, &min_b, &max_b);

                AABB b;
                b.min[0] = ed->preview_min[0]; b.min[1] = ed->preview_min[1]; b.min[2] = ed->preview_min[2];
                b.max[0] = ed->preview_max[0]; b.max[1] = ed->preview_max[1]; b.max[2] = ed->preview_max[2];
                set_aabb_2d_coords(vp, &b, min_a, min_b, max_a, max_b);
                memcpy(ed->preview_min, b.min, sizeof(b.min));
                memcpy(ed->preview_max, b.max, sizeof(b.max));

                snprintf(ed->size_str, sizeof(ed->size_str), "dx: %.2f  dy: %.2f  dz: %.2f",
                         (double)(ed->preview_max[0] - ed->preview_min[0]),
                         (double)(ed->preview_max[1] - ed->preview_min[1]),
                         (double)(ed->preview_max[2] - ed->preview_min[2]));
            } else if (ed->tool == TOOL_SELECT && ed->selected_cube >= 0) {
                float da = wa - ed->drag_start_mouse[0];
                float db = wb - ed->drag_start_mouse[1];
                MapCube *c = &g_map.cubes[ed->selected_cube];
                if (vp == VIEWPORT_TOP) {
                    c->x = ed->drag_orig_min[0] + da;
                    c->z = ed->drag_orig_min[2] + db;
                } else if (vp == VIEWPORT_FRONT) {
                    c->x = ed->drag_orig_min[0] + da;
                    c->y = ed->drag_orig_min[1] + db;
                } else {
                    c->z = ed->drag_orig_min[2] + da;
                    c->y = ed->drag_orig_min[1] + db;
                }
                ed->dirty = 1;
            }
        }

        if (win_mouse_released(0)) {
            if (ed->is_dragging && ed->tool == TOOL_SELECT && ed->selected_cube >= 0) {
                editor_push_undo(ed);
            }
            ed->is_dragging = 0;
        }
    }

    /* Взаимодействие с 3D Viewport */
    UiRect r3d = get_viewport_rect(ed, VIEWPORT_3D, ws);
    if (ui_contains(NULL, r3d)) {
        /* Raycast выбор куба */
        float dir[3];
        render_view_dir(&ed->cam, &dir[0], &dir[1], &dir[2]);
        float o[3] = { ed->cam.x, ed->cam.y, ed->cam.z };

        float best_t = 100.0f;
        int best_cube = -1;
        for (size_t i = 0; i < g_map.count; i++) {
            AABB b = cube_aabb(&g_map.cubes[i]);
            float t; int axis, sign;
            if (ray_aabb(&b, o, dir, best_t, &t, &axis, &sign) && t < best_t) {
                best_t = t;
                best_cube = (int)i;
            }
        }
        ed->hovered_cube = best_cube;

        if (win_mouse_clicked(0)) {
            ed->active_viewport = VIEWPORT_3D;
            if (ed->tool == TOOL_SELECT) {
                ed->selected_cube = best_cube;
            } else if (ed->tool == TOOL_TEXTURE && best_cube >= 0) {
                ed->selected_cube = best_cube;
                editor_pick_material_from_selected(ed);
            } else if (ed->tool == TOOL_APPLY_TEX && best_cube >= 0) {
                ed->selected_cube = best_cube;
                editor_apply_material_to_selected(ed);
            }
        }
    }
}

/* ---------- Отрисовка интерфейса Hammer UI ---------- */

static void draw_top_menubar(Editor *ed, Ui *ui) {
    const float h = 24.0f * ui->scale;
    UiRect bar = ui_rect(0, 0, (float)ui->width, h);
    ui_panel_raised(bar);

    const char *menus[] = { "File", "Edit", "Map", "View", "Tools", "Help" };
    float x = 6.0f * ui->scale;
    for (int i = 0; i < 6; i++) {
        float tw = ui_text_width(ui, menus[i], 0.82f) + 16.0f * ui->scale;
        UiRect btn = ui_rect(x, 2.0f, tw, h - 4.0f);
        int active = (ed->active_menu == (ActiveMenu)(i + 1));
        if (ui_button_hammer(ui, btn, menus[i], active)) {
            ed->active_menu = active ? MENU_NONE : (ActiveMenu)(i + 1);
        }
        x += tw + 2.0f;
    }

    /* Заголовок документа справа */
    char doc_title[256];
    snprintf(doc_title, sizeof(doc_title), "Valve Hammer Editor 4.1 — [%.64s%s]",
             ed->file[0] ? base_name(ed->file) : "untitled.tfm", ed->dirty ? " *" : "");
    ui_label_right(ui, bar, 12.0f * ui->scale, 0.78f, UI_CLR_TEXT_DIM, doc_title);
}

static void draw_top_toolbar(Editor *ed, Ui *ui) {
    const float y = 24.0f * ui->scale;
    const float h = 28.0f * ui->scale;
    UiRect bar = ui_rect(0, y, (float)ui->width, h);
    ui_panel_raised(bar);

    float x = 6.0f * ui->scale;
    const float bw = 24.0f * ui->scale;
    const float bh = h - 4.0f;

    /* Файловые операции */
    if (ui_button_tool(ui, ui_rect(x, y + 2.0f, bw, bh), "N", "New", 0)) {
        editor_new_map(ed);
    }
    x += bw + 2.0f;
    if (ui_button_tool(ui, ui_rect(x, y + 2.0f, bw, bh), "O", "Open", 0)) {
        ed->modal = MODAL_OPEN_MAP;
    }
    x += bw + 2.0f;
    if (ui_button_tool(ui, ui_rect(x, y + 2.0f, bw, bh), "S", "Save", 0)) {
        editor_save(ed);
    }
    x += bw + 6.0f;

    /* Разделитель */
    ui_line(x, y + 4.0f, x, y + h - 4.0f, 1.0f, UI_CLR_PANEL_DARK);
    x += 6.0f;

    /* Undo / Redo */
    if (ui_button_tool(ui, ui_rect(x, y + 2.0f, bw, bh), "U", "Undo", 0)) {
        editor_undo(ed);
    }
    x += bw + 2.0f;
    if (ui_button_tool(ui, ui_rect(x, y + 2.0f, bw, bh), "R", "Redo", 0)) {
        editor_redo(ed);
    }
    x += bw + 6.0f;

    ui_line(x, y + 4.0f, x, y + h - 4.0f, 1.0f, UI_CLR_PANEL_DARK);
    x += 6.0f;

    /* Запуск компилятора F9 (зеленый бегущий человечек Hammer) */
    const float run_w = 64.0f * ui->scale;
    if (ui_button_hammer(ui, ui_rect(x, y + 2.0f, run_w, bh), "RUN (F9)", 0)) {
        editor_run_compile(ed);
    }
    x += run_w + 6.0f;

    ui_line(x, y + 4.0f, x, y + h - 4.0f, 1.0f, UI_CLR_PANEL_DARK);
    x += 6.0f;

    /* Управление видовыми экранами */
    if (ui_button_tool(ui, ui_rect(x, y + 2.0f, bw * 1.5f, bh), "2x2", "Quad", ed->maximized_viewport < 0)) {
        ed->maximized_viewport = -1;
    }
    x += bw * 1.5f + 2.0f;
    if (ui_button_tool(ui, ui_rect(x, y + 2.0f, bw * 1.5f, bh), "3D", "Max 3D", ed->maximized_viewport == VIEWPORT_3D)) {
        ed->maximized_viewport = VIEWPORT_3D;
    }
    x += bw * 1.5f + 6.0f;

    ui_line(x, y + 4.0f, x, y + h - 4.0f, 1.0f, UI_CLR_PANEL_DARK);
    x += 6.0f;

    /* Режим 3D: Textured / Flat / Wire */
    const char *modes[] = { "Tex", "Flat", "Wire" };
    for (int m = 0; m < 3; m++) {
        if (ui_button_tool(ui, ui_rect(x, y + 2.0f, bw * 1.6f, bh), modes[m], "", ed->render_3d_mode == (Render3DMode)m)) {
            ed->render_3d_mode = (Render3DMode)m;
        }
        x += bw * 1.6f + 2.0f;
    }
    x += 4.0f;

    ui_line(x, y + 4.0f, x, y + h - 4.0f, 1.0f, UI_CLR_PANEL_DARK);
    x += 6.0f;

    /* Сетка */
    if (ui_button_tool(ui, ui_rect(x, y + 2.0f, bw, bh), "[-]", "Grid -", 0)) {
        editor_grid_smaller(ed);
    }
    x += bw + 2.0f;
    if (ui_button_tool(ui, ui_rect(x, y + 2.0f, bw, bh), "[+]", "Grid +", 0)) {
        editor_grid_larger(ed);
    }
    x += bw + 2.0f;

    char gbuf[32];
    snprintf(gbuf, sizeof(gbuf), "Grid: %gm", (double)ed->grid_size);
    ui_panel_sunken(ui_rect(x, y + 3.0f, 75.0f * ui->scale, bh - 2.0f), UI_CLR_SUNKEN);
    ui_label_in(ui, ui_rect(x, y + 3.0f, 75.0f * ui->scale, bh - 2.0f), 0.75f, UI_CLR_TEXT_ACCENT, gbuf);
    x += 75.0f * ui->scale + 2.0f;

    if (ui_button_tool(ui, ui_rect(x, y + 2.0f, 50.0f * ui->scale, bh), ed->grid_snap ? "Snap" : "NoSnap", "Snap", ed->grid_snap)) {
        editor_toggle_snap(ed);
    }
    x += 50.0f * ui->scale + 6.0f;

    ui_line(x, y + 4.0f, x, y + h - 4.0f, 1.0f, UI_CLR_PANEL_DARK); x += 6.0f;

    /* 3D Cam Freelook button (Z) */
    if (ui_button_hammer(ui, ui_rect(x, y + 2.0f, 80.0f * ui->scale, bh), "3D Cam (Z)", ed->freelook_3d)) {
        editor_toggle_3d_freelook(ed, NULL);
    }
}

static void draw_left_toolbar(Editor *ed, Ui *ui) {
    const float y = 52.0f * ui->scale;
    const float w = 38.0f * ui->scale;
    const float h = (float)ui->height - y - 22.0f * ui->scale;
    UiRect bar = ui_rect(0, y, w, h);
    ui_panel_raised(bar);

    const float bw = w - 6.0f;
    const float bh = 28.0f * ui->scale;
    float by = y + 4.0f;

    const char *icons[TOOL_COUNT] = { "Sel", "Mag", "Cam", "Ent", "Blk", "Tex", "App", "Clp", "Hol" };
    for (int t = 0; t < TOOL_COUNT; t++) {
        if (ui_button_tool(ui, ui_rect(3.0f, by, bw, bh), icons[t], "", ed->tool == (EditorTool)t)) {
            editor_set_tool(ed, (EditorTool)t);
            if (t == TOOL_HOLLOW) editor_hollow_selected(ed);
        }
        by += bh + 3.0f;
    }
}

static void draw_right_dock(Editor *ed, Ui *ui) {
    const float y = 52.0f * ui->scale;
    const float w = 220.0f * ui->scale;
    const float x = (float)ui->width - w;
    const float h = (float)ui->height - y - 22.0f * ui->scale;
    UiRect dock = ui_rect(x, y, w, h);
    ui_panel_raised(dock);

    float cy = y + 4.0f;
    const float pad = 6.0f * ui->scale;

    /* 1. Блок текстур (Texture Bar) */
    ui_panel_header(ui, ui_rect(x + 2.0f, cy, w - 4.0f, 20.0f * ui->scale), "Textures", 0);
    cy += 24.0f * ui->scale;

    /* Предпросмотр текущей текстуры */
    UiRect prev_r = ui_rect(x + pad, cy, 70.0f * ui->scale, 70.0f * ui->scale);
    ui_texture_preview(ui, prev_r, ed->material, editor_material_name(ed), 1);

    /* Кнопки Browse и Apply */
    float bx = x + pad + 76.0f * ui->scale;
    float bw = w - pad * 2.0f - 76.0f * ui->scale;
    if (ui_button_hammer(ui, ui_rect(bx, cy, bw, 24.0f * ui->scale), "Browse...", 0)) {
        ed->modal = MODAL_TEXTURE_BROWSER;
    }
    if (ui_button_hammer(ui, ui_rect(bx, cy + 28.0f * ui->scale, bw, 24.0f * ui->scale), "Apply", 0)) {
        editor_apply_material_to_selected(ed);
    }
    cy += 76.0f * ui->scale;

    /* Настройки масштаба текстуры */
    ui_stepper_float(ui, ui_rect(x + pad, cy, w - pad * 2.0f, 20.0f * ui->scale),
                     "Scale:", &ed->mat_tile, 0.5f, 0.5f, 16.0f, "%.1fm");
    cy += 24.0f * ui->scale;

    /* Mode: Repeat vs Stretch */
    UiRect rep_r = ui_rect(x + pad, cy, (w - pad * 2.0f - 4.0f) * 0.5f, 20.0f * ui->scale);
    UiRect str_r = ui_rect(rep_r.x + rep_r.w + 4.0f, cy, rep_r.w, 20.0f * ui->scale);
    if (ui_button_tool(ui, rep_r, "Repeat", "", ed->mat_uv == MAP_UV_TILE)) ed->mat_uv = MAP_UV_TILE;
    if (ui_button_tool(ui, str_r, "Stretch", "", ed->mat_uv == MAP_UV_STRETCH)) ed->mat_uv = MAP_UV_STRETCH;
    cy += 28.0f * ui->scale;

    /* 2. Блок примитивов / создания объектов (New Objects) */
    ui_panel_header(ui, ui_rect(x + 2.0f, cy, w - 4.0f, 20.0f * ui->scale), "Primitives", 0);
    cy += 24.0f * ui->scale;

    const char *prims[] = { "Block (Box)", "Wedge", "Cylinder", "Arch", "Room" };
    for (int p = 0; p < 5; p++) {
        UiRect pr_btn = ui_rect(x + pad, cy, w - pad * 2.0f, 20.0f * ui->scale);
        if (ui_button_tool(ui, pr_btn, prims[p], "", ed->primitive == (BrushPrimitive)p)) {
            ed->primitive = (BrushPrimitive)p;
            ed->tool = TOOL_BLOCK;
        }
        cy += 22.0f * ui->scale;
    }
    cy += 4.0f;

    if (ui_button_hammer(ui, ui_rect(x + pad, cy, w - pad * 2.0f, 26.0f * ui->scale), "Create Brush (Enter)", 0)) {
        editor_create_brush(ed);
    }
    cy += 32.0f * ui->scale;

    /* 3. Блок свойств выделенного объекта (Object Properties) */
    ui_panel_header(ui, ui_rect(x + 2.0f, cy, w - 4.0f, 20.0f * ui->scale), "Object Properties", 0);
    cy += 24.0f * ui->scale;

    if (ed->selected_cube >= 0 && (size_t)ed->selected_cube < g_map.count) {
        const MapCube *c = &g_map.cubes[ed->selected_cube];
        char buf[128];

        snprintf(buf, sizeof(buf), "Solid: #%d", ed->selected_cube);
        ui_label_left(ui, ui_rect(x + pad, cy, w - pad * 2.0f, 16.0f * ui->scale), 2.0f, 0.78f, UI_CLR_TEXT_ACCENT, buf);
        cy += 18.0f * ui->scale;

        snprintf(buf, sizeof(buf), "Pos: (%.1f, %.1f, %.1f)", (double)c->x, (double)c->y, (double)c->z);
        ui_label_left(ui, ui_rect(x + pad, cy, w - pad * 2.0f, 16.0f * ui->scale), 2.0f, 0.75f, UI_CLR_TEXT, buf);
        cy += 18.0f * ui->scale;

        snprintf(buf, sizeof(buf), "Size: %.1fx%.1fx%.1f m", (double)c->sx, (double)c->sy, (double)c->sz);
        ui_label_left(ui, ui_rect(x + pad, cy, w - pad * 2.0f, 16.0f * ui->scale), 2.0f, 0.75f, UI_CLR_TEXT, buf);
        cy += 22.0f * ui->scale;

        /* Действия */
        UiRect b1 = ui_rect(x + pad, cy, (w - pad * 2.0f - 4.0f) * 0.5f, 22.0f * ui->scale);
        UiRect b2 = ui_rect(b1.x + b1.w + 4.0f, cy, b1.w, 22.0f * ui->scale);
        if (ui_button_hammer(ui, b1, "Delete", 0)) editor_delete_selected(ed);
        if (ui_button_hammer(ui, b2, "Duplicate", 0)) editor_duplicate_selected(ed);
        cy += 26.0f * ui->scale;

        UiRect b3 = ui_rect(x + pad, cy, (w - pad * 2.0f - 4.0f) * 0.5f, 22.0f * ui->scale);
        UiRect b4 = ui_rect(b3.x + b3.w + 4.0f, cy, b3.w, 22.0f * ui->scale);
        if (ui_button_hammer(ui, b3, "Hollow", 0)) editor_hollow_selected(ed);
        if (ui_button_hammer(ui, b4, "Pick Mat", 0)) editor_pick_material_from_selected(ed);
    } else {
        ui_label_left(ui, ui_rect(x + pad, cy, w - pad * 2.0f, 20.0f * ui->scale), 2.0f, 0.78f, UI_CLR_TEXT_DIM, "No selection");
        cy += 20.0f * ui->scale;
        char stats[64];
        snprintf(stats, sizeof(stats), "Total Solids: %d", (int)g_map.count);
        ui_label_left(ui, ui_rect(x + pad, cy, w - pad * 2.0f, 20.0f * ui->scale), 2.0f, 0.75f, UI_CLR_TEXT_DIM, stats);
    }
}

static void draw_bottom_statusbar(Editor *ed, Ui *ui) {
    const float h = 22.0f * ui->scale;
    const float y = (float)ui->height - h;
    UiRect bar = ui_rect(0, y, (float)ui->width, h);
    ui_panel_raised(bar);

    const float pad = 2.0f;
    float x = pad;

    /* 1. Статус / Подсказка инструмента */
    float w1 = (float)ui->width * 0.40f;
    ui_status_field(ui, ui_rect(x, y + pad, w1, h - pad * 2.0f), ed->status_msg, UI_CLR_TEXT);
    x += w1 + 2.0f;

    /* 2. Мировые координаты курсора */
    float w2 = 150.0f * ui->scale;
    ui_status_field(ui, ui_rect(x, y + pad, w2, h - pad * 2.0f), ed->coord_str, UI_CLR_TEXT_ACCENT);
    x += w2 + 2.0f;

    /* 3. Размеры выделения / превью */
    float w3 = 180.0f * ui->scale;
    ui_status_field(ui, ui_rect(x, y + pad, w3, h - pad * 2.0f), ed->size_str, UI_CLR_TEXT);
    x += w3 + 2.0f;

    /* 4. Сетка и привязка */
    char snap_buf[64];
    snprintf(snap_buf, sizeof(snap_buf), "Snap: %s  Grid: %gm", ed->grid_snap ? "ON" : "OFF", (double)ed->grid_size);
    float w4 = (float)ui->width - x - pad;
    if (w4 > 100.0f) {
        ui_status_field(ui, ui_rect(x, y + pad, w4, h - pad * 2.0f), snap_buf, UI_CLR_TEXT_DIM);
    }
}

static void draw_dropdown_menus(Editor *ed, Ui *ui) {
    if (ed->active_menu == MENU_NONE) return;

    const float menu_y = 24.0f * ui->scale;
    const float item_h = 22.0f * ui->scale;
    const float menu_w = 190.0f * ui->scale;

    float mx = 6.0f * ui->scale;
    if (ed->active_menu == MENU_EDIT) mx += 44.0f * ui->scale;
    else if (ed->active_menu == MENU_MAP) mx += 90.0f * ui->scale;
    else if (ed->active_menu == MENU_VIEW) mx += 138.0f * ui->scale;
    else if (ed->active_menu == MENU_TOOLS) mx += 186.0f * ui->scale;
    else if (ed->active_menu == MENU_HELP) mx += 238.0f * ui->scale;

    if (ed->active_menu == MENU_FILE) {
        UiRect box = ui_rect(mx, menu_y, menu_w, item_h * 6.0f + 4.0f);
        ui_panel_raised(box);
        float iy = menu_y + 2.0f;
        if (ui_menu_item(ui, ui_rect(mx + 2.0f, iy, menu_w - 4.0f, item_h), "New", "Ctrl+N", 1)) { editor_new_map(ed); ed->active_menu = MENU_NONE; } iy += item_h;
        if (ui_menu_item(ui, ui_rect(mx + 2.0f, iy, menu_w - 4.0f, item_h), "Open...", "Ctrl+O", 1)) { ed->modal = MODAL_OPEN_MAP; ed->active_menu = MENU_NONE; } iy += item_h;
        if (ui_menu_item(ui, ui_rect(mx + 2.0f, iy, menu_w - 4.0f, item_h), "Save", "Ctrl+S", 1)) { editor_save(ed); ed->active_menu = MENU_NONE; } iy += item_h;
        if (ui_menu_item(ui, ui_rect(mx + 2.0f, iy, menu_w - 4.0f, item_h), "Save As...", "", 1)) { ed->modal = MODAL_SAVE_AS; ed->active_menu = MENU_NONE; } iy += item_h;
        if (ui_menu_item(ui, ui_rect(mx + 2.0f, iy, menu_w - 4.0f, item_h), "Run Map...", "F9", 1)) { editor_run_compile(ed); ed->active_menu = MENU_NONE; } iy += item_h;
        if (ui_menu_item(ui, ui_rect(mx + 2.0f, iy, menu_w - 4.0f, item_h), "Exit", "Esc", 1)) { exit(0); }
    } else if (ed->active_menu == MENU_EDIT) {
        UiRect box = ui_rect(mx, menu_y, menu_w, item_h * 6.0f + 4.0f);
        ui_panel_raised(box);
        float iy = menu_y + 2.0f;
        if (ui_menu_item(ui, ui_rect(mx + 2.0f, iy, menu_w - 4.0f, item_h), "Undo", "Ctrl+Z", ed->undo_index > 1)) { editor_undo(ed); ed->active_menu = MENU_NONE; } iy += item_h;
        if (ui_menu_item(ui, ui_rect(mx + 2.0f, iy, menu_w - 4.0f, item_h), "Redo", "Ctrl+Y", ed->undo_index < ed->undo_count)) { editor_redo(ed); ed->active_menu = MENU_NONE; } iy += item_h;
        if (ui_menu_item(ui, ui_rect(mx + 2.0f, iy, menu_w - 4.0f, item_h), "Delete", "Del", ed->selected_cube >= 0)) { editor_delete_selected(ed); ed->active_menu = MENU_NONE; } iy += item_h;
        if (ui_menu_item(ui, ui_rect(mx + 2.0f, iy, menu_w - 4.0f, item_h), "Duplicate", "Ctrl+D", ed->selected_cube >= 0)) { editor_duplicate_selected(ed); ed->active_menu = MENU_NONE; } iy += item_h;
        if (ui_menu_item(ui, ui_rect(mx + 2.0f, iy, menu_w - 4.0f, item_h), "Select All", "Ctrl+A", 1)) { editor_select_all(ed); ed->active_menu = MENU_NONE; } iy += item_h;
        if (ui_menu_item(ui, ui_rect(mx + 2.0f, iy, menu_w - 4.0f, item_h), "Clear Selection", "Esc", 1)) { editor_clear_selection(ed); ed->active_menu = MENU_NONE; }
    } else if (ed->active_menu == MENU_MAP) {
        UiRect box = ui_rect(mx, menu_y, menu_w, item_h * 4.0f + 4.0f);
        ui_panel_raised(box);
        float iy = menu_y + 2.0f;
        if (ui_menu_item(ui, ui_rect(mx + 2.0f, iy, menu_w - 4.0f, item_h), "Map Information", "", 1)) { ed->modal = MODAL_MAP_INFO; ed->active_menu = MENU_NONE; } iy += item_h;
        if (ui_menu_item(ui, ui_rect(mx + 2.0f, iy, menu_w - 4.0f, item_h), "Check Problems...", "Alt+P", 1)) { editor_check_problems(ed); ed->active_menu = MENU_NONE; } iy += item_h;
        if (ui_menu_item(ui, ui_rect(mx + 2.0f, iy, menu_w - 4.0f, item_h), "Make Hollow...", "", ed->selected_cube >= 0)) { editor_hollow_selected(ed); ed->active_menu = MENU_NONE; } iy += item_h;
        if (ui_menu_item(ui, ui_rect(mx + 2.0f, iy, menu_w - 4.0f, item_h), "Clear Map", "", 1)) { editor_new_map(ed); ed->active_menu = MENU_NONE; }
    } else if (ed->active_menu == MENU_VIEW) {
        UiRect box = ui_rect(mx, menu_y, menu_w, item_h * 5.0f + 4.0f);
        ui_panel_raised(box);
        float iy = menu_y + 2.0f;
        if (ui_menu_item(ui, ui_rect(mx + 2.0f, iy, menu_w - 4.0f, item_h), "2x2 Quad View", "", 1)) { ed->maximized_viewport = -1; ed->active_menu = MENU_NONE; } iy += item_h;
        if (ui_menu_item(ui, ui_rect(mx + 2.0f, iy, menu_w - 4.0f, item_h), "Maximize 3D", "Shift+Z", 1)) { ed->maximized_viewport = VIEWPORT_3D; ed->active_menu = MENU_NONE; } iy += item_h;
        if (ui_menu_item(ui, ui_rect(mx + 2.0f, iy, menu_w - 4.0f, item_h), "Grid Smaller", "[", 1)) { editor_grid_smaller(ed); ed->active_menu = MENU_NONE; } iy += item_h;
        if (ui_menu_item(ui, ui_rect(mx + 2.0f, iy, menu_w - 4.0f, item_h), "Grid Larger", "]", 1)) { editor_grid_larger(ed); ed->active_menu = MENU_NONE; } iy += item_h;
        if (ui_menu_item(ui, ui_rect(mx + 2.0f, iy, menu_w - 4.0f, item_h), "Snap to Grid", "", 1)) { editor_toggle_snap(ed); ed->active_menu = MENU_NONE; }
    } else if (ed->active_menu == MENU_TOOLS) {
        UiRect box = ui_rect(mx, menu_y, menu_w, item_h * 5.0f + 4.0f);
        ui_panel_raised(box);
        float iy = menu_y + 2.0f;
        if (ui_menu_item(ui, ui_rect(mx + 2.0f, iy, menu_w - 4.0f, item_h), "Selection Tool", "Shift+S", 1)) { editor_set_tool(ed, TOOL_SELECT); ed->active_menu = MENU_NONE; } iy += item_h;
        if (ui_menu_item(ui, ui_rect(mx + 2.0f, iy, menu_w - 4.0f, item_h), "Block Tool", "Shift+B", 1)) { editor_set_tool(ed, TOOL_BLOCK); ed->active_menu = MENU_NONE; } iy += item_h;
        if (ui_menu_item(ui, ui_rect(mx + 2.0f, iy, menu_w - 4.0f, item_h), "Texture Application", "Shift+A", 1)) { editor_set_tool(ed, TOOL_TEXTURE); ed->active_menu = MENU_NONE; } iy += item_h;
        if (ui_menu_item(ui, ui_rect(mx + 2.0f, iy, menu_w - 4.0f, item_h), "Apply Texture", "Shift+T", 1)) { editor_apply_material_to_selected(ed); ed->active_menu = MENU_NONE; } iy += item_h;
        if (ui_menu_item(ui, ui_rect(mx + 2.0f, iy, menu_w - 4.0f, item_h), "Texture Browser...", "", 1)) { ed->modal = MODAL_TEXTURE_BROWSER; ed->active_menu = MENU_NONE; }
    } else if (ed->active_menu == MENU_HELP) {
        UiRect box = ui_rect(mx, menu_y, menu_w, item_h * 1.0f + 4.0f);
        ui_panel_raised(box);
        if (ui_menu_item(ui, ui_rect(mx + 2.0f, menu_y + 2.0f, menu_w - 4.0f, item_h), "About Valve Hammer...", "", 1)) {
            ed->modal = MODAL_ABOUT_HAMMER;
            ed->active_menu = MENU_NONE;
        }
    }
}

static void draw_viewport_tabs(Editor *ed, Ui *ui) {
    const float s = ui->scale;
    UiRect ws = get_workspace_rect(ui->width, ui->height, s);

    const char *titles[4] = { "top (x/z)", "3D Textured", "front (x/y)", "side (z/y)" };
    for (int i = 0; i < 4; i++) {
        UiRect vpr = get_viewport_rect(ed, (ViewportIndex)i, ws);
        if (vpr.w <= 0 || vpr.h <= 0) continue;

        int is_act = (ed->active_viewport == i);
        int is_max = (ed->maximized_viewport == i);
        int tab_action = ui_viewport_tab(ui, vpr, titles[i], is_act, is_max);
        if (tab_action == 1) {
            ed->active_viewport = i;
        } else if (tab_action == 2) {
            ed->active_viewport = i;
            editor_toggle_maximize(ed);
        }
    }
}

/* ---------- Модальные диалоговые окна ---------- */

static void draw_modal_dialogs(Editor *ed, Ui *ui) {
    if (ed->modal == MODAL_NONE) return;

    /* Полупрозрачное затемнение фона */
    const float dim[4] = { 0.0f, 0.0f, 0.0f, 0.45f };
    ui_fill(ui_rect(0, 0, (float)ui->width, (float)ui->height), dim);

    int close = 0;

    if (ed->modal == MODAL_TEXTURE_BROWSER) {
        const float w = 560.0f * ui->scale;
        const float h = 440.0f * ui->scale;
        UiRect dlg = ui_centered(ui, w, h);
        ui_dialog_frame(ui, dlg, "Textures — Hammer Texture Browser", &close);

        /* Сетка текстур */
        float top_y = dlg.y + 36.0f * ui->scale;
        float bot_y = dlg.y + dlg.h - 45.0f * ui->scale;
        UiRect list_r = ui_rect(dlg.x + 10.0f * ui->scale, top_y, dlg.w - 20.0f * ui->scale, bot_y - top_y);
        ui_panel_sunken(list_r, UI_CLR_SUNKEN);

        int cols = 4;
        float cell_w = (list_r.w - 20.0f * ui->scale) / (float)cols;
        float cell_h = cell_w + 16.0f * ui->scale;

        int b_count = prim_builtin_count();
        int row = 0, col = 0;
        for (int i = 0; i < b_count; i++) {
            float cx = list_r.x + 10.0f * ui->scale + (float)col * cell_w;
            float cy = list_r.y + 10.0f * ui->scale + (float)row * cell_h;
            UiRect item_r = ui_rect(cx, cy, cell_w - 6.0f, cell_h - 6.0f);

            const Texture *t = prim_builtin_texture(i);
            const char *tname = prim_builtin_name(i);
            int is_sel = (ed->material == t);

            if (ui_texture_preview(ui, item_r, t, tname, is_sel)) {
                ed->material = t;
                snprintf(ed->material_name, sizeof(ed->material_name), "%s", tname);
                editor_apply_material_to_selected(ed);
            }

            col++;
            if (col >= cols) { col = 0; row++; }
        }

        /* Кнопки внизу */
        UiRect ok_r = ui_rect(dlg.x + dlg.w - 180.0f * ui->scale, dlg.y + dlg.h - 36.0f * ui->scale, 80.0f * ui->scale, 26.0f * ui->scale);
        UiRect cancel_r = ui_rect(dlg.x + dlg.w - 90.0f * ui->scale, dlg.y + dlg.h - 36.0f * ui->scale, 80.0f * ui->scale, 26.0f * ui->scale);
        if (ui_button_hammer(ui, ok_r, "OK", 0)) { close = 1; }
        if (ui_button_hammer(ui, cancel_r, "Cancel", 0)) { close = 1; }
    } else if (ed->modal == MODAL_RUN_MAP) {
        const float w = 500.0f * ui->scale;
        const float h = 360.0f * ui->scale;
        UiRect dlg = ui_centered(ui, w, h);
        ui_dialog_frame(ui, dlg, "Run Map — Source / GoldSrc Compiler", &close);

        UiRect log_r = ui_rect(dlg.x + 10.0f * ui->scale, dlg.y + 36.0f * ui->scale,
                               dlg.w - 20.0f * ui->scale, dlg.h - 85.0f * ui->scale);
        ui_panel_sunken(log_r, UI_CLR_SUNKEN);

        /* Вывод консоли сборки */
        float ly = log_r.y + 6.0f * ui->scale;
        const char *p = ed->compile_log;
        char line[128];
        while (*p && ly < log_r.y + log_r.h - 16.0f * ui->scale) {
            const char *nl = strchr(p, '\n');
            size_t len = nl ? (size_t)(nl - p) : strlen(p);
            if (len >= sizeof(line)) len = sizeof(line) - 1;
            memcpy(line, p, len);
            line[len] = '\0';

            const float *clr = (strstr(line, "Command:") || strstr(line, "Executing")) ? UI_CLR_TEXT_ACCENT : UI_CLR_TEXT;
            ui_label_left(ui, ui_rect(log_r.x + 6.0f, ly, log_r.w - 12.0f, 14.0f * ui->scale), 0.0f, 0.72f, clr, line);
            ly += 14.0f * ui->scale;
            if (!nl) break;
            p = nl + 1;
        }

        UiRect save_btn = ui_rect(dlg.x + dlg.w - 200.0f * ui->scale, dlg.y + dlg.h - 36.0f * ui->scale, 100.0f * ui->scale, 26.0f * ui->scale);
        UiRect close_btn = ui_rect(dlg.x + dlg.w - 90.0f * ui->scale, dlg.y + dlg.h - 36.0f * ui->scale, 80.0f * ui->scale, 26.0f * ui->scale);
        if (ui_button_hammer(ui, save_btn, "Save Map", 0)) { editor_save(ed); }
        if (ui_button_hammer(ui, close_btn, "Close", 0)) { close = 1; }
    } else if (ed->modal == MODAL_CHECK_PROBLEMS) {
        const float w = 460.0f * ui->scale;
        const float h = 280.0f * ui->scale;
        UiRect dlg = ui_centered(ui, w, h);
        ui_dialog_frame(ui, dlg, "Check for Problems (Alt+P)", &close);

        UiRect list_r = ui_rect(dlg.x + 10.0f * ui->scale, dlg.y + 36.0f * ui->scale,
                                dlg.w - 20.0f * ui->scale, dlg.h - 85.0f * ui->scale);
        ui_panel_sunken(list_r, UI_CLR_SUNKEN);

        float py = list_r.y + 8.0f * ui->scale;
        for (int i = 0; i < ed->problem_count; i++) {
            ui_label_left(ui, ui_rect(list_r.x + 8.0f, py, list_r.w - 16.0f, 16.0f * ui->scale),
                          0.0f, 0.78f, UI_CLR_TEXT, ed->problem_list[i]);
            py += 18.0f * ui->scale;
        }

        UiRect close_btn = ui_rect(dlg.x + dlg.w - 100.0f * ui->scale, dlg.y + dlg.h - 36.0f * ui->scale, 90.0f * ui->scale, 26.0f * ui->scale);
        if (ui_button_hammer(ui, close_btn, "OK", 0)) { close = 1; }
    } else if (ed->modal == MODAL_MAP_INFO) {
        const float w = 420.0f * ui->scale;
        const float h = 240.0f * ui->scale;
        UiRect dlg = ui_centered(ui, w, h);
        ui_dialog_frame(ui, dlg, "Map Information", &close);

        char lines[5][128];
        snprintf(lines[0], 128, "Total Solids (Cubes): %d", (int)g_map.count);
        snprintf(lines[1], 128, "Total Faces: %d", (int)g_map.count * 6);
        snprintf(lines[2], 128, "Total Vertices: %d", (int)g_map.count * 24);
        snprintf(lines[3], sizeof(lines[3]), "File: %.64s", ed->file[0] ? base_name(ed->file) : "untitled.tfm");
        snprintf(lines[4], 128, "Format: Game3D .tfm (Source / GoldSrc Brush Model)");

        float iy = dlg.y + 40.0f * ui->scale;
        for (int i = 0; i < 5; i++) {
            ui_label_left(ui, ui_rect(dlg.x + 16.0f * ui->scale, iy, dlg.w - 32.0f, 18.0f * ui->scale),
                          0.0f, 0.82f, (i == 0) ? UI_CLR_TEXT_ACCENT : UI_CLR_TEXT, lines[i]);
            iy += 22.0f * ui->scale;
        }

        UiRect ok_btn = ui_rect(dlg.x + (dlg.w - 80.0f * ui->scale) * 0.5f, dlg.y + dlg.h - 36.0f * ui->scale, 80.0f * ui->scale, 26.0f * ui->scale);
        if (ui_button_hammer(ui, ok_btn, "OK", 0)) { close = 1; }
    } else if (ed->modal == MODAL_ABOUT_HAMMER) {
        const float w = 440.0f * ui->scale;
        const float h = 260.0f * ui->scale;
        UiRect dlg = ui_centered(ui, w, h);
        ui_dialog_frame(ui, dlg, "About Valve Hammer Editor", &close);

        float iy = dlg.y + 40.0f * ui->scale;
        ui_label_left(ui, ui_rect(dlg.x + 16.0f * ui->scale, iy, dlg.w - 32.0f, 20.0f * ui->scale),
                      0.0f, 0.95f, UI_CLR_TEXT_ACCENT, "Valve Hammer Editor v4.1");
        iy += 26.0f * ui->scale;
        ui_label_left(ui, ui_rect(dlg.x + 16.0f * ui->scale, iy, dlg.w - 32.0f, 16.0f * ui->scale),
                      0.0f, 0.78f, UI_CLR_TEXT, "GoldSrc / Source Edition for Game3D");
        iy += 20.0f * ui->scale;
        ui_label_left(ui, ui_rect(dlg.x + 16.0f * ui->scale, iy, dlg.w - 32.0f, 16.0f * ui->scale),
                      0.0f, 0.75f, UI_CLR_TEXT_DIM, "Original Worldcraft design by Ben Morris & Valve Corp.");
        iy += 20.0f * ui->scale;
        ui_label_left(ui, ui_rect(dlg.x + 16.0f * ui->scale, iy, dlg.w - 32.0f, 16.0f * ui->scale),
                      0.0f, 0.75f, UI_CLR_TEXT_DIM, "4 Viewports, Noclip 3D Freelook (Z), Grid Snapping");
        iy += 20.0f * ui->scale;
        ui_label_left(ui, ui_rect(dlg.x + 16.0f * ui->scale, iy, dlg.w - 32.0f, 16.0f * ui->scale),
                      0.0f, 0.75f, UI_CLR_TEXT_DIM, "Supports full .tfm level format and materials");

        UiRect ok_btn = ui_rect(dlg.x + (dlg.w - 80.0f * ui->scale) * 0.5f, dlg.y + dlg.h - 36.0f * ui->scale, 80.0f * ui->scale, 26.0f * ui->scale);
        if (ui_button_hammer(ui, ok_btn, "OK", 0)) { close = 1; }
    } else if (ed->modal == MODAL_OPEN_MAP) {
        const float w = 460.0f * ui->scale;
        const float h = 340.0f * ui->scale;
        UiRect dlg = ui_centered(ui, w, h);
        ui_dialog_frame(ui, dlg, "Open Map (.tfm)", &close);

        UiRect list_r = ui_rect(dlg.x + 10.0f * ui->scale, dlg.y + 36.0f * ui->scale,
                                dlg.w - 20.0f * ui->scale, dlg.h - 85.0f * ui->scale);
        ui_panel_sunken(list_r, UI_CLR_SUNKEN);

        float ry = list_r.y + 4.0f;
        for (int i = 0; i < ed->map_list.count; i++) {
            UiRect item_r = ui_rect(list_r.x + 4.0f, ry, list_r.w - 8.0f, 22.0f * ui->scale);
            if (ui_button_hammer(ui, item_r, ed->map_list.names[i], 0)) {
                char path[MAP_LIST_PATH_MAX];
                map_list_path(&ed->map_list, i, path, sizeof(path));
                editor_open(ed, path);
                close = 1;
            }
            ry += 24.0f * ui->scale;
        }

        if (ed->map_list.count == 0) {
            ui_label_in(ui, list_r, 0.82f, UI_CLR_TEXT_DIM, "Нет файлов .tfm в каталоге карт");
        }

        UiRect cancel_btn = ui_rect(dlg.x + dlg.w - 90.0f * ui->scale, dlg.y + dlg.h - 36.0f * ui->scale, 80.0f * ui->scale, 26.0f * ui->scale);
        if (ui_button_hammer(ui, cancel_btn, "Cancel", 0)) { close = 1; }
    } else if (ed->modal == MODAL_SAVE_AS) {
        const float w = 420.0f * ui->scale;
        const float h = 180.0f * ui->scale;
        UiRect dlg = ui_centered(ui, w, h);
        ui_dialog_frame(ui, dlg, "Save Map As...", &close);

        ui_label_left(ui, ui_rect(dlg.x + 16.0f * ui->scale, dlg.y + 45.0f * ui->scale, dlg.w - 32.0f, 20.0f * ui->scale),
                      0.0f, 0.82f, UI_CLR_TEXT, "Сохранить карту в файл:");

        UiRect inp = ui_rect(dlg.x + 16.0f * ui->scale, dlg.y + 70.0f * ui->scale, dlg.w - 32.0f, 24.0f * ui->scale);
        ui_panel_sunken(inp, UI_CLR_SUNKEN);
        ui_label_left(ui, inp, 6.0f, 0.80f, UI_CLR_TEXT_ACCENT, ed->file[0] ? ed->file : "map01.tfm");

        UiRect save_btn = ui_rect(dlg.x + dlg.w - 180.0f * ui->scale, dlg.y + dlg.h - 36.0f * ui->scale, 80.0f * ui->scale, 26.0f * ui->scale);
        UiRect cancel_btn = ui_rect(dlg.x + dlg.w - 90.0f * ui->scale, dlg.y + dlg.h - 36.0f * ui->scale, 80.0f * ui->scale, 26.0f * ui->scale);
        if (ui_button_hammer(ui, save_btn, "Save", 0)) { editor_save(ed); close = 1; }
        if (ui_button_hammer(ui, cancel_btn, "Cancel", 0)) { close = 1; }
    }

    if (close) {
        ed->modal = MODAL_NONE;
    }
}

/* ---------- Отрисовка всего интерфейса ---------- */

void editor_render_ui(Editor *ed, Ui *ui) {
    draw_top_menubar(ed, ui);
    draw_top_toolbar(ed, ui);
    draw_left_toolbar(ed, ui);
    draw_right_dock(ed, ui);
    draw_bottom_statusbar(ed, ui);
    draw_viewport_tabs(ed, ui);
    draw_dropdown_menus(ed, ui);
    draw_modal_dialogs(ed, ui);
}
