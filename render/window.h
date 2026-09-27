#ifndef WINDOW_H
#define WINDOW_H

/* ------------------------------------------------------------------
 * Окно, контекст OpenGL и ввод — всё, что зависит от GLFW.
 * ------------------------------------------------------------------ */

#include <GLFW/glfw3.h>

typedef struct {
    GLFWwindow *window;
} WinWindow;

int win_init(WinWindow *w, int width, int height, const char *title);
void win_shutdown(WinWindow *w);
int win_poll(WinWindow *w);
double win_time_seconds(void);
void win_size(const WinWindow *w, int *width, int *height);
void win_swap(const WinWindow *w);

/* ---------- Клавиши ---------- */
typedef enum {
    WIN_KEY_W = 0,
    WIN_KEY_A,
    WIN_KEY_S,
    WIN_KEY_D,
    WIN_KEY_SPACE,
    WIN_KEY_SHIFT,
    WIN_KEY_CTRL,
    WIN_KEY_ALT,
    WIN_KEY_G,
    WIN_KEY_Y,
    WIN_KEY_Z,
    WIN_KEY_B,
    WIN_KEY_C,
    WIN_KEY_E,
    WIN_KEY_T,
    WIN_KEY_X,
    WIN_KEY_N,
    WIN_KEY_O,
    WIN_KEY_P,
    WIN_KEY_H,
    WIN_KEY_L,
    WIN_KEY_BRACKET_LEFT,   /* [ */
    WIN_KEY_BRACKET_RIGHT,  /* ] */
    WIN_KEY_ENTER,
    WIN_KEY_DELETE,
    WIN_KEY_BACKSPACE,
    WIN_KEY_TAB,
    WIN_KEY_F1,
    WIN_KEY_F2,
    WIN_KEY_F9,
    WIN_KEY_1,
    WIN_KEY_2,
    WIN_KEY_3,
    WIN_KEY_4,
    WIN_KEY_UP,
    WIN_KEY_DOWN,
    WIN_KEY_LEFT,
    WIN_KEY_RIGHT,
    WIN_KEY_COUNT
} WinKey;

int win_key_down(WinKey key);
int win_key_pressed(WinKey key);
int win_button_down(int button);

int win_shift_down(void);
int win_ctrl_down(void);
int win_alt_down(void);

/* ---------- Захват мыши ---------- */
void win_set_mouse_captured(WinWindow *w, int captured);
int  win_mouse_captured(void);
void win_mouse_delta(double *dx, double *dy);

/* ---------- Мышь и события ---------- */
void win_pointer_pixels(const WinWindow *w, int *x, int *y);
int win_mouse_clicked(int button);
int win_mouse_down(int button);
int win_mouse_released(int button);
double win_scroll_delta(void);
int win_escape_pressed(void);
void win_reset_input(void);

#endif
