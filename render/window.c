#include <stdio.h>
#include <string.h>

#include "render/window.h"

#define WIN_BUTTON_COUNT 8   /* кнопок мыши, которые запоминаем */
#define WIN_TEXT_QUEUE_SIZE 256

static int key_state[WIN_KEY_COUNT];
static int button_state[WIN_BUTTON_COUNT];

/* События «один раз»: ставятся в колбэках GLFW и снимаются чтением,
 * поэтому меню видит ровно одно нажатие, даже если опрос был реже. */
static int button_clicked[WIN_BUTTON_COUNT];
static double scroll_accum = 0.0;
static int escape_pressed = 0;
static int text_events[WIN_TEXT_QUEUE_SIZE];
static int text_event_count = 0;

/* Смещение мыши для обзора в игре. Накапливается в колбэке движения
 * курсора и снимается чтением один раз за кадр. */
static double mouse_dx = 0.0;
static double mouse_dy = 0.0;
static double mouse_last_x = 0.0;
static double mouse_last_y = 0.0;
static int mouse_tracking = 0;
static int mouse_captured = 0;

static int glfw_ready = 0;

static void push_text_event(int event) {
    if (text_event_count < WIN_TEXT_QUEUE_SIZE) {
        text_events[text_event_count++] = event;
    }
}

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
    if ((action == GLFW_PRESS || action == GLFW_REPEAT) &&
        key == GLFW_KEY_BACKSPACE) {
        push_text_event(WIN_TEXT_BACKSPACE);
    }
    if (action == GLFW_PRESS &&
        (key == GLFW_KEY_ENTER || key == GLFW_KEY_KP_ENTER)) {
        push_text_event(WIN_TEXT_ENTER);
    }
}

static void char_callback(GLFWwindow *window, unsigned int codepoint) {
    (void)window;
    if (codepoint > 0 && codepoint <= 0x10ffffu) {
        push_text_event((int)codepoint);
    }
}

static void mouse_button_callback(GLFWwindow *window, int button, int action, int mods) {
    (void)window; (void)mods;
    if (button >= 0 && button < WIN_BUTTON_COUNT) {
        button_state[button] = (action != GLFW_RELEASE);
        if (action == GLFW_PRESS) button_clicked[button] = 1;
    }
}

/* Первое событие после захвата только запоминает позицию: иначе скачок
 * курсора в центр окна превратился бы в резкий поворот камеры. */
static void cursor_position_callback(GLFWwindow *window, double x, double y) {
    (void)window;
    /* В меню курсор обычный: его координаты нужны интерфейсу, и в обзор
     * они попадать не должны. */
    if (!mouse_captured) return;

    if (!mouse_tracking) {
        mouse_last_x = x;
        mouse_last_y = y;
        mouse_tracking = 1;
        return;
    }

    mouse_dx += x - mouse_last_x;
    mouse_dy += y - mouse_last_y;
    mouse_last_x = x;
    mouse_last_y = y;
}

/* Потеря фокуса не должна превращаться в рывок камеры: смещение,
 * накопленное до Alt+Tab, отбрасываем. */
static void focus_callback(GLFWwindow *window, int focused) {
    (void)window;
    if (!focused) {
        mouse_dx = 0.0;
        mouse_dy = 0.0;
        mouse_tracking = 0;
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

void win_mouse_capture(int capture) {
    capture = capture ? 1 : 0;
    mouse_dx = 0.0;
    mouse_dy = 0.0;

    GLFWwindow *context = glfw_ready ? glfwGetCurrentContext() : NULL;
    if (!context) {
        mouse_captured = capture;
        return;
    }

    glfwSetInputMode(context, GLFW_CURSOR,
                     capture ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
    mouse_captured = capture;
    /* Позицию запомнит первое событие курсора (см. cursor_position_callback),
     * поэтому перецентрирование не даёт скачка камеры. */
    mouse_tracking = 0;

    if (!capture) {
        /* Меню открывается с курсором в центре окна, а не там, где его
         * «оставил» захват: иначе указатель мог оказаться у края. */
        int width = 1, height = 1;
        glfwGetWindowSize(context, &width, &height);
        glfwSetCursorPos(context, (double)width * 0.5, (double)height * 0.5);
    }
}

int win_mouse_captured(void) {
    return mouse_captured;
}

void win_mouse_delta(double *dx, double *dy) {
    if (dx) *dx = mouse_dx;
    if (dy) *dy = mouse_dy;
    mouse_dx = 0.0;
    mouse_dy = 0.0;
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

int win_text_event(void) {
    int event;
    if (text_event_count <= 0) return 0;

    event = text_events[0];
    text_event_count--;
    if (text_event_count > 0) {
        memmove(text_events, text_events + 1,
                (size_t)text_event_count * sizeof text_events[0]);
    }
    return event;
}

void win_reset_input(void) {
    for (int i = 0; i < WIN_BUTTON_COUNT; i++) button_clicked[i] = 0;
    scroll_accum = 0.0;
    mouse_dx = 0.0;
    mouse_dy = 0.0;
    mouse_tracking = 0;
    escape_pressed = 0;
    text_event_count = 0;
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
    glfwSetCharCallback(w->window, char_callback);
    glfwSetMouseButtonCallback(w->window, mouse_button_callback);
    glfwSetScrollCallback(w->window, scroll_callback);
    glfwSetCursorPosCallback(w->window, cursor_position_callback);
    glfwSetWindowFocusCallback(w->window, focus_callback);
    return 1;
}

void win_shutdown(WinWindow *w) {
    if (w->window) {
        /* Курсор обязан вернуться в обычный режим: иначе после закрытия
         * окна он останется скрытым в системе. */
        if (mouse_captured) win_mouse_capture(0);
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
