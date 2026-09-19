/* clock_gettime/nanosleep — POSIX, в строгом -std=c11 без этого макроса скрыты */
#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <time.h>

#include "gen.h"
#include "physics/physics.h"
#include "render/glx.h"
#include "render/render.h"

/* ---------- Параметры ---------- */

#define WINDOW_WIDTH  800
#define WINDOW_HEIGHT 600
#define TARGET_FPS    60.0f
#define FRAME_DURATION (1.0 / TARGET_FPS)
#define MAX_FRAME_DELTA 0.2   /* защита от «скачка» после зависания */

static double get_time(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

/* Состояние клавиш превращаем в намерения игрока — physics не знает про X11. */
static void read_input(PlayerInput *in) {
    in->forward = (float)(glx_key_down(GLX_KEY_W) - glx_key_down(GLX_KEY_S));
    in->strafe  = (float)(glx_key_down(GLX_KEY_D) - glx_key_down(GLX_KEY_A));
    in->look_x  = (float)(glx_key_down(GLX_KEY_RIGHT) - glx_key_down(GLX_KEY_LEFT));
    in->look_y  = (float)(glx_key_down(GLX_KEY_UP) - glx_key_down(GLX_KEY_DOWN));
    in->jump    = glx_key_down(GLX_KEY_SPACE);
    in->run     = glx_key_down(GLX_KEY_SHIFT);
}

int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <texture-file>\n", argv[0]);
        return 1;
    }

    GlxWindow window;
    if (!glx_init(&window, WINDOW_WIDTH, WINDOW_HEIGHT, "Game3D")) {
        return 1;
    }

    Renderer renderer;
    if (!render_init(&renderer, argv[1])) {
        fprintf(stderr, "Failed to load texture: %s\n", argv[1]);
        render_shutdown(&renderer);
        glx_shutdown(&window);
        return 1;
    }

    render_setup_gl();

    gen_init();

    Player player;
    phys_init(&player);

    double last_time = get_time();
    double accumulator = 0.0;

    while (!glx_poll(&window)) {
        double frame_start = get_time();

        double delta = frame_start - last_time;
        last_time = frame_start;

        if (delta > MAX_FRAME_DELTA) delta = MAX_FRAME_DELTA;
        accumulator += delta;

        /* Фиксированный шаг физики: поведение не зависит от FPS. */
        while (accumulator >= FRAME_DURATION) {
            PlayerInput in;
            read_input(&in);
            phys_update(&player, &in, FRAME_DURATION);
            accumulator -= FRAME_DURATION;
        }

        gen_update_chunks(player.x, player.z);

        int width, height;
        glx_size(&window, &width, &height);

        render_clear(&renderer);
        render_camera(&renderer, &player, width, height);
        render_world(&renderer, &player);

        glx_swap(&window);

        /* удержание ~60 FPS даже без вертикальной синхронизации */
        double elapsed = get_time() - frame_start;
        if (elapsed < FRAME_DURATION) {
            struct timespec rest = {0, 0};
            double seconds = FRAME_DURATION - elapsed;
            rest.tv_sec = (time_t)seconds;
            rest.tv_nsec = (long)((seconds - (double)rest.tv_sec) * 1e9);
            nanosleep(&rest, NULL);
        }
    }

    render_shutdown(&renderer);
    glx_shutdown(&window);
    return 0;
}
