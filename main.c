#include <stdio.h>

#include "map/gen.h"
#include "map/map.h"
#include "physics/physics.h"
#include "render/render.h"
#include "render/window.h"

/* ---------- Параметры ---------- */

#define WINDOW_WIDTH  800
#define WINDOW_HEIGHT 600
#define PHYSICS_HZ    60.0
#define PHYSICS_STEP  (1.0 / PHYSICS_HZ)
#define MAX_FRAME_DELTA 0.2   /* защита от «скачка» после зависания */

/* Состояние клавиш превращаем в намерения игрока — physics не знает про GLFW. */
static void read_input(PlayerInput *in) {
    in->forward = (float)(win_key_down(WIN_KEY_W) - win_key_down(WIN_KEY_S));
    in->strafe  = (float)(win_key_down(WIN_KEY_D) - win_key_down(WIN_KEY_A));
    in->look_x  = (float)(win_key_down(WIN_KEY_RIGHT) - win_key_down(WIN_KEY_LEFT));
    in->look_y  = (float)(win_key_down(WIN_KEY_UP) - win_key_down(WIN_KEY_DOWN));
    in->jump    = win_key_down(WIN_KEY_SPACE);
    in->run     = win_key_down(WIN_KEY_SHIFT);
}

int main(int argc, char **argv) {
    if (argc < 2 || argc > 3) {
        fprintf(stderr, "Usage: %s <texture-file> [map-file]\n", argv[0]);
        return 1;
    }

    const char *texture_file = argv[1];
    const char *map_file = (argc >= 3 && argv[2][0] != '\0') ? argv[2] : NULL;

    WinWindow window;
    if (!win_init(&window, WINDOW_WIDTH, WINDOW_HEIGHT, "Game3D")) {
        return 1;
    }

    Renderer renderer;
    if (!render_init(&renderer, texture_file)) {
        fprintf(stderr, "Failed to load texture: %s\n", texture_file);
        render_shutdown(&renderer);
        win_shutdown(&window);
        return 1;
    }

    render_setup_gl();

    map_init();
    if (map_file) {
        if (!map_load(map_file)) {
            printf("Fallback to procedural terrain generation.\n");
        }
    }

    gen_init();

    Player player;
    phys_init(&player);

    /* GLFW предоставляет монотонные часы на Windows, Linux и macOS. */
    double last_time = win_time_seconds();
    double accumulator = 0.0;

    while (!win_poll(&window)) {
        double frame_start = win_time_seconds();

        double delta = frame_start - last_time;
        last_time = frame_start;

        if (delta > MAX_FRAME_DELTA) delta = MAX_FRAME_DELTA;
        if (delta < 0.0) delta = 0.0;
        accumulator += delta;

        /* Фиксированный шаг физики: поведение не зависит от FPS. */
        while (accumulator >= PHYSICS_STEP) {
            PlayerInput in;
            read_input(&in);
            phys_update(&player, &in, PHYSICS_STEP);
            accumulator -= PHYSICS_STEP;
        }

        /* Генерация — fallback на случай, если карта не загружена;
         * с кастомной картой чанки не нужны ни рендеру, ни физике. */
        if (!map_is_custom()) {
            gen_update_chunks(player.x, player.z);
        }

        int width, height;
        win_size(&window, &width, &height);

        render_clear(&renderer);
        render_camera(&renderer, &player, width, height);
        render_world(&renderer, &player);

        win_swap(&window);
    }

    render_shutdown(&renderer);
    map_free();
    win_shutdown(&window);
    return 0;
}
