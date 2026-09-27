#include <stdio.h>
#include <string.h>

#include "render/window.h"

#define WIN_BUTTON_COUNT 8

static int key_state[WIN_KEY_COUNT];
static int button_state[WIN_BUTTON_COUNT];

static int key_pressed_once[WIN_KEY_COUNT];
static int button_clicked[WIN_BUTTON_COUNT];
static int button_released_once[WIN_BUTTON_COUNT];
static double scroll_accum = 0.0;
static int escape_pressed = 0;

static int shift_is_down = 0;
static int ctrl_is_down = 0;
static int alt_is_down = 0;

static int mouse_captured = 0;
static int mouse_have_last = 0;
static double mouse_last_x = 0.0, mouse_last_y = 0.0;
static double mouse_dx = 0.0, mouse_dy = 0.0;

static int glfw_ready = 0;

static const char *glfw_error_string(void) {
    const char *description = NULL;
    glfwGetError(&description);
    return description ? description : "unknown error";
}

static int key_slot(int key) {
    switch (key) {
        case GLFW_KEY_W:            return WIN_KEY_W;
        case GLFW_KEY_A:            return WIN_KEY_A;
        case GLFW_KEY_S:            return WIN_KEY_S;
        case GLFW_KEY_D:            return WIN_KEY_D;
        case GLFW_KEY_SPACE:        return WIN_KEY_SPACE;
        case GLFW_KEY_LEFT_SHIFT:
        case GLFW_KEY_RIGHT_SHIFT:  return WIN_KEY_SHIFT;
        case GLFW_KEY_LEFT_CONTROL:
        case GLFW_KEY_RIGHT_CONTROL: return WIN_KEY_CTRL;
        case GLFW_KEY_LEFT_ALT:
        case GLFW_KEY_RIGHT_ALT:    return WIN_KEY_ALT;
        case GLFW_KEY_G:            return WIN_KEY_G;
        case GLFW_KEY_Y:            return WIN_KEY_Y;
        case GLFW_KEY_Z:            return WIN_KEY_Z;
        case GLFW_KEY_B:            return WIN_KEY_B;
        case GLFW_KEY_C:            return WIN_KEY_C;
        case GLFW_KEY_E:            return WIN_KEY_E;
        case GLFW_KEY_T:            return WIN_KEY_T;
        case GLFW_KEY_X:            return WIN_KEY_X;
        case GLFW_KEY_N:            return WIN_KEY_N;
        case GLFW_KEY_O:            return WIN_KEY_O;
        case GLFW_KEY_P:            return WIN_KEY_P;
        case GLFW_KEY_H:            return WIN_KEY_H;
        case GLFW_KEY_L:            return WIN_KEY_L;
        case GLFW_KEY_LEFT_BRACKET: return WIN_KEY_BRACKET_LEFT;
        case GLFW_KEY_RIGHT_BRACKET: return WIN_KEY_BRACKET_RIGHT;
        case GLFW_KEY_ENTER:
        case GLFW_KEY_KP_ENTER:     return WIN_KEY_ENTER;
        case GLFW_KEY_DELETE:       return WIN_KEY_DELETE;
        case GLFW_KEY_BACKSPACE:    return WIN_KEY_BACKSPACE;
        case GLFW_KEY_TAB:          return WIN_KEY_TAB;
        case GLFW_KEY_F1:           return WIN_KEY_F1;
        case GLFW_KEY_F2:           return WIN_KEY_F2;
        case GLFW_KEY_F9:           return WIN_KEY_F9;
        case GLFW_KEY_1:            return WIN_KEY_1;
        case GLFW_KEY_2:            return WIN_KEY_2;
        case GLFW_KEY_3:            return WIN_KEY_3;
        case GLFW_KEY_4:            return WIN_KEY_4;
        case GLFW_KEY_UP:           return WIN_KEY_UP;
        case GLFW_KEY_DOWN:         return WIN_KEY_DOWN;
        case GLFW_KEY_LEFT:         return WIN_KEY_LEFT;
        case GLFW_KEY_RIGHT:        return WIN_KEY_RIGHT;
        default:                    return -1;
    }
}

static void key_callback(GLFWwindow *window, int key, int scancode, int action, int mods) {
    (void)window; (void)scancode;
    shift_is_down = (mods & GLFW_MOD_SHIFT) != 0;
    ctrl_is_down  = (mods & GLFW_MOD_CONTROL) != 0;
    alt_is_down   = (mods & GLFW_MOD_ALT) != 0;

    int slot = key_slot(key);
    if (slot >= 0) {
        key_state[slot] = (action != GLFW_RELEASE);
        if (action == GLFW_PRESS) key_pressed_once[slot] = 1;
    }
    if (action == GLFW_PRESS && key == GLFW_KEY_ESCAPE) {
        escape_pressed = 1;
    }
}

static void mouse_button_callback(GLFWwindow *window, int button, int action, int mods) {
    (void)window;
    shift_is_down = (mods & GLFW_MOD_SHIFT) != 0;
    ctrl_is_down  = (mods & GLFW_MOD_CONTROL) != 0;
    alt_is_down   = (mods & GLFW_MOD_ALT) != 0;

    if (button >= 0 && button < WIN_BUTTON_COUNT) {
        button_state[button] = (action != GLFW_RELEASE);
        if (action == GLFW_PRESS) button_clicked[button] = 1;
        if (action == GLFW_RELEASE) button_released_once[button] = 1;
    }
}

static void scroll_callback(GLFWwindow *window, double xoffset, double yoffset) {
    (void)window; (void)xoffset;
    scroll_accum += yoffset;
}

static void cursor_pos_callback(GLFWwindow *window, double x, double y) {
    (void)window;
    if (!mouse_captured) return;

    if (mouse_have_last) {
        mouse_dx += x - mouse_last_x;
        mouse_dy += y - mouse_last_y;
    }
    mouse_last_x = x;
    mouse_last_y = y;
    mouse_have_last = 1;
}

int win_key_down(WinKey key) {
    if (key < 0 || key >= WIN_KEY_COUNT) return 0;
    return key_state[key];
}

int win_key_pressed(WinKey key) {
    if (key < 0 || key >= WIN_KEY_COUNT) return 0;
    const int pressed = key_pressed_once[key];
    key_pressed_once[key] = 0;
    return pressed;
}

int win_shift_down(void) {
    return shift_is_down || key_state[WIN_KEY_SHIFT];
}

int win_ctrl_down(void) {
    return ctrl_is_down || key_state[WIN_KEY_CTRL];
}

int win_alt_down(void) {
    return alt_is_down || key_state[WIN_KEY_ALT];
}

void win_set_mouse_captured(WinWindow *w, int captured) {
    captured = captured ? 1 : 0;
    if (mouse_captured == captured) return;
    mouse_captured = captured;

    if (w && w->window) {
        glfwSetInputMode(w->window, GLFW_CURSOR,
                         captured ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
        if (glfwRawMouseMotionSupported()) {
            glfwSetInputMode(w->window, GLFW_RAW_MOUSE_MOTION,
                             captured ? GLFW_TRUE : GLFW_FALSE);
        }
    }

    mouse_have_last = 0;
    mouse_dx = 0.0;
    mouse_dy = 0.0;
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

int win_button_down(int button) {
    if (button < 0 || button >= WIN_BUTTON_COUNT) return 0;
    return button_state[button];
}

int win_mouse_down(int button) {
    return win_button_down(button);
}

void win_pointer_pixels(const WinWindow *w, int *x, int *y) {
    if (!w || !w->window) {
        if (x) *x = 0;
        if (y) *y = 0;
        return;
    }
    double sx = 0.0, sy = 0.0;
    glfwGetCursorPos(w->window, &sx, &sy);

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

int win_mouse_released(int button) {
    if (button < 0 || button >= WIN_BUTTON_COUNT) return 0;
    const int released = button_released_once[button];
    button_released_once[button] = 0;
    return released;
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
    for (int i = 0; i < WIN_BUTTON_COUNT; i++) {
        button_clicked[i] = 0;
        button_released_once[i] = 0;
    }
    for (int i = 0; i < WIN_KEY_COUNT; i++) key_pressed_once[i] = 0;
    scroll_accum = 0.0;
    escape_pressed = 0;
    mouse_dx = 0.0;
    mouse_dy = 0.0;
}

static void set_base_hints(void) {
    glfwWindowHint(GLFW_DEPTH_BITS, 24);
    glfwWindowHint(GLFW_DOUBLEBUFFER, GLFW_TRUE);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
}

int win_init(WinWindow *w, int width, int height, const char *title) {
    memset(w, 0, sizeof(*w));
    memset(key_state, 0, sizeof(key_state));
    memset(button_state, 0, sizeof(button_state));
    mouse_captured = 0;
    mouse_have_last = 0;
    win_reset_input();
    glfw_ready = 0;

    if (!glfwInit()) {
        fprintf(stderr, "Cannot initialize GLFW: %s\n", glfw_error_string());
        return 0;
    }
    glfw_ready = 1;

    set_base_hints();
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
    glfwSetWindowSizeLimits(w->window, 320, 240, GLFW_DONT_CARE, GLFW_DONT_CARE);
    glfwSwapInterval(1);

    glfwSetKeyCallback(w->window, key_callback);
    glfwSetMouseButtonCallback(w->window, mouse_button_callback);
    glfwSetScrollCallback(w->window, scroll_callback);
    glfwSetCursorPosCallback(w->window, cursor_pos_callback);
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
    if (w->window && glfwWindowShouldClose(w->window)) {
        return 1;
    }
    return 0;
}

double win_time_seconds(void) {
    return glfwGetTime();
}

void win_size(const WinWindow *w, int *width, int *height) {
    if (w && w->window) {
        glfwGetFramebufferSize(w->window, width, height);
    } else {
        if (width) *width = 800;
        if (height) *height = 600;
    }
}

void win_swap(const WinWindow *w) {
    if (w && w->window) {
        glfwSwapBuffers(w->window);
    }
}
