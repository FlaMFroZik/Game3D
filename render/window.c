#include <stdio.h>
#include <string.h>

#include "render/window.h"

#define WIN_BUTTON_COUNT 8   /* кнопок мыши, которые запоминаем */

static int key_state[WIN_KEY_COUNT];
static int button_state[WIN_BUTTON_COUNT];

/* События «один раз»: ставятся в колбэках GLFW и снимаются чтением,
 * поэтому меню видит ровно одно нажатие, даже если опрос был реже. */
static int button_clicked[WIN_BUTTON_COUNT];
static double scroll_accum = 0.0;
static int escape_pressed = 0;

static int glfw_ready = 0;

static const char *glfw_error_string(void) {
    const char *description = NULL;
    glfwGetError(&description);
    return description ? description : "unknown error";
}

/* ---------- Клавиши ---------- */

static int key_slot(int key) {
    switch (key) {
        case GLFW_KEY_W:    return WIN_KEY_W;
        case GLFW_KEY_A:    return WIN_KEY_A;
        case GLFW_KEY_S:    return WIN_KEY_S;
        case GLFW_KEY_D:    return WIN_KEY_D;
        case GLFW_KEY_SPACE: return WIN_KEY_SPACE;
        case GLFW_KEY_LEFT_SHIFT:
        case GLFW_KEY_RIGHT_SHIFT: return WIN_KEY_SHIFT;
        case GLFW_KEY_UP:   return WIN_KEY_UP;
        case GLFW_KEY_DOWN: return WIN_KEY_DOWN;
        case GLFW_KEY_LEFT: return WIN_KEY_LEFT;
        case GLFW_KEY_RIGHT: return WIN_KEY_RIGHT;
        default:            return -1;
    }
}

static void key_callback(GLFWwindow *window, int key, int scancode, int action, int mods) {
    (void)window; (void)scancode; (void)mods;
    int slot = key_slot(key);
    if (slot >= 0) key_state[slot] = (action != GLFW_RELEASE);
    /* Esc больше не закрывает игру сразу: его забирает меню (см. main.c). */
    if (action == GLFW_PRESS && key == GLFW_KEY_ESCAPE) {
        escape_pressed = 1;
    }
}

static void mouse_button_callback(GLFWwindow *window, int button, int action, int mods) {
    (void)window; (void)mods;
    if (button >= 0 && button < WIN_BUTTON_COUNT) {
        button_state[button] = (action != GLFW_RELEASE);
        if (action == GLFW_PRESS) button_clicked[button] = 1;
    }
}

static void scroll_callback(GLFWwindow *window, double xoffset, double yoffset) {
    (void)window; (void)xoffset;
    scroll_accum += yoffset;
}

int win_key_down(WinKey key) {
    if (key < 0 || key >= WIN_KEY_COUNT) return 0;
    return key_state[key];
}

int win_button_down(int button) {
    if (button < 0 || button >= WIN_BUTTON_COUNT) return 0;
    return button_state[button];
}

void win_pointer_pixels(const WinWindow *w, int *x, int *y) {
    double sx = 0.0, sy = 0.0;
    glfwGetCursorPos(w->window, &sx, &sy);

    /* Координаты курсора GLFW отдаёт в логических пикселях окна, а рисуем
     * мы в пикселях кадра: на экранах с масштабированием они отличаются. */
    int win_w = 1, win_h = 1;
    int fb_w = 1, fb_h = 1;
    glfwGetWindowSize(w->window, &win_w, &win_h);
    glfwGetFramebufferSize(w->window, &fb_w, &fb_h);

    const double kx = (win_w > 0) ? (double)fb_w / (double)win_w : 1.0;
    const double ky = (win_h > 0) ? (double)fb_h / (double)win_h : 1.0;

    if (x) *x = (int)(sx * kx);
    if (y) *y = (int)(sy * ky);
}

int win_mouse_clicked(int button) {
    if (button < 0 || button >= WIN_BUTTON_COUNT) return 0;
    const int clicked = button_clicked[button];
    button_clicked[button] = 0;
    return clicked;
}

double win_scroll_delta(void) {
    const double delta = scroll_accum;
    scroll_accum = 0.0;
    return delta;
}

int win_escape_pressed(void) {
    const int pressed = escape_pressed;
    escape_pressed = 0;
    return pressed;
}

void win_reset_input(void) {
    for (int i = 0; i < WIN_BUTTON_COUNT; i++) button_clicked[i] = 0;
    scroll_accum = 0.0;
    escape_pressed = 0;
}

/* ---------- Окно ---------- */

/* Подсказки, общие для основного и запасного контекста. */
static void set_base_hints(void) {
    glfwWindowHint(GLFW_DEPTH_BITS, 24);
    glfwWindowHint(GLFW_DOUBLEBUFFER, GLFW_TRUE);
    /* Окно можно свободно растягивать в обе стороны. Не задаём
     * верхний предел: оно может быть больше стартового размера и даже
     * занимать весь экран через кнопку разворачивания менеджера окон. */
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
}

int win_init(WinWindow *w, int width, int height, const char *title) {
    memset(w, 0, sizeof(*w));
    memset(key_state, 0, sizeof(key_state));
    memset(button_state, 0, sizeof(button_state));
    win_reset_input();
    glfw_ready = 0;

    if (!glfwInit()) {
        fprintf(stderr, "Cannot initialize GLFW: %s\n", glfw_error_string());
        return 0;
    }
    glfw_ready = 1;

    set_base_hints();

    /* Рендер использует фиксированный конвейер (glBegin/glMatrixMode),
     * поэтому просим совместимый профиль 3.3. Если драйвер его не даст —
     * откат на «обычный» legacy-контекст. */
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_COMPAT_PROFILE);
    w->window = glfwCreateWindow(width, height, title, NULL, NULL);

    if (!w->window) {
        glfwDefaultWindowHints();
        set_base_hints();
        w->window = glfwCreateWindow(width, height, title, NULL, NULL);
        if (!w->window) {
            fprintf(stderr, "Cannot create window: %s\n", glfw_error_string());
            win_shutdown(w);
            return 0;
        }
    }

    glfwMakeContextCurrent(w->window);
    /* GLFW не ограничивает максимальный размер окна сам по себе, но
     * явно сбрасываем возможный лимит платформы/предыдущих настроек. */
    glfwSetWindowSizeLimits(w->window, 320, 240, GLFW_DONT_CARE, GLFW_DONT_CARE);
    glfwSwapInterval(1); /* одинаковое ограничение частоты кадров на обеих ОС */

    glfwSetKeyCallback(w->window, key_callback);
    glfwSetMouseButtonCallback(w->window, mouse_button_callback);
    glfwSetScrollCallback(w->window, scroll_callback);
    return 1;
}

void win_shutdown(WinWindow *w) {
    if (w->window) {
        glfwDestroyWindow(w->window);
        w->window = NULL;
    }
    if (glfw_ready) {
        glfwTerminate();
        glfw_ready = 0;
    }
}

int win_poll(WinWindow *w) {
    glfwPollEvents();
    /* Кнопка «закрыть» оконного менеджера завершает игру. Esc — нет:
     * его нажатие ждёт win_escape_pressed, а меню решает, что делать. */
    if (w->window && glfwWindowShouldClose(w->window)) {
        return 1;
    }
    return 0;
}

double win_time_seconds(void) {
    return glfwGetTime();
}

void win_size(const WinWindow *w, int *width, int *height) {
    glfwGetFramebufferSize(w->window, width, height);
}

void win_swap(const WinWindow *w) {
    glfwSwapBuffers(w->window);
}
