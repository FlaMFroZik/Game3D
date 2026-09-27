#include <stdio.h>
#include <string.h>

#include "editor/editor.h"
#include "map/list.h"
#include "map/map.h"
#include "render/font.h"
#include "render/render.h"
#include "render/ui.h"
#include "render/window.h"

#define WINDOW_WIDTH    1280
#define WINDOW_HEIGHT   800
#define MAX_FRAME_DELTA 0.1

#define FONT_DIR  "assets/fonts"
#define FONT_FILE "DejaVuSans.ttf"

static int file_exists(const char *path) {
    FILE *file = fopen(path, "rb");
    if (!file) return 0;
    fclose(file);
    return 1;
}

static const char *find_font(char *out, size_t out_size) {
    const char *roots[2];
    roots[0] = map_list_game_dir();
    roots[1] = ".";

    for (int i = 0; i < 2; i++) {
        snprintf(out, out_size, "%s/%s/%s", roots[i], FONT_DIR, FONT_FILE);
        if (file_exists(out)) return out;

        snprintf(out, out_size, "%s/%s", roots[i], FONT_FILE);
        if (file_exists(out)) return out;
    }
    out[0] = '\0';
    return NULL;
}

int main(int argc, char **argv) {
    if (argc > 3) {
        fprintf(stderr, "Usage: %s [texture-file] [map-file]\n", argv[0]);
        return 1;
    }

    const char *texture_file = (argc >= 2 && argv[1][0] != '\0') ? argv[1] : NULL;
    const char *map_file = (argc >= 3 && argv[2][0] != '\0') ? argv[2] : NULL;

    WinWindow window;
    if (!win_init(&window, WINDOW_WIDTH, WINDOW_HEIGHT, "Valve Hammer Editor v4.1 — [Game3D]")) {
        return 1;
    }

    Renderer renderer;
    if (!render_init(&renderer, texture_file)) {
        fprintf(stderr, "Failed to initialize renderer.\n");
        render_shutdown(&renderer);
        win_shutdown(&window);
        return 1;
    }
    render_setup_gl();

    char font_path[MAP_LIST_PATH_MAX];
    if (!find_font(font_path, sizeof font_path)) {
        fprintf(stderr, "Cannot find font %s/%s next to the editor.\n",
                FONT_DIR, FONT_FILE);
        render_shutdown(&renderer);
        win_shutdown(&window);
        return 1;
    }

    Font *font = font_create(font_path, FONT_PIXEL_SIZE);
    if (!font) {
        render_shutdown(&renderer);
        win_shutdown(&window);
        return 1;
    }

    Editor ed;
    if (map_file && map_load(map_file)) {
        editor_init_loaded(&ed, map_file);
    } else {
        editor_init(&ed);
    }

    double last_time = win_time_seconds();

    while (!win_poll(&window)) {
        double now = win_time_seconds();
        double delta = now - last_time;
        last_time = now;

        if (delta > MAX_FRAME_DELTA) delta = MAX_FRAME_DELTA;
        if (delta < 0.0) delta = 0.0;

        int width, height;
        win_size(&window, &width, &height);

        /* 1. Обновление состояния редактора */
        editor_update(&ed, delta, width, height);

        /* 2. Отрисовка 4 видовых экранов и геометрии */
        editor_render_viewports(&ed, &renderer, width, height);

        /* 3. Отрисовка интерфейса Valve Hammer */
        Ui ui;
        ui_frame_begin(&ui, font, width, height);
        win_pointer_pixels(&window, &ui.pointer_x, &ui.pointer_y);
        ui.clicked = win_mouse_clicked(0);
        ui.right_clicked = win_mouse_clicked(1);
        ui.mouse_down = win_mouse_down(0);
        ui.mouse_released = win_mouse_released(0);
        ui.wheel = (float)win_scroll_delta();

        editor_render_ui(&ed, &ui);
        ui_frame_end();

        win_swap(&window);
    }

    editor_shutdown(&ed);
    map_free();
    font_destroy(font);
    render_shutdown(&renderer);
    win_shutdown(&window);
    return 0;
}
