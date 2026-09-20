#ifndef WINDOW_H
#define WINDOW_H

/* ------------------------------------------------------------------
 * Окно, контекст OpenGL и ввод — всё, что зависит от GLFW.
 * Остальные модули про GLFW не знают.
 * ------------------------------------------------------------------ */

#include <GLFW/glfw3.h>

typedef struct {
    GLFWwindow *window;
} WinWindow;

/* Создаёт окно и контекст. Возвращает 0, если что-то не удалось
 * (в этом случае окно открывать не нужно). */
int win_init(WinWindow *w, int width, int height, const char *title);

/* Разрушает контекст и окно, завершает GLFW. */
void win_shutdown(WinWindow *w);

/* Обрабатывает все накопившиеся события. Возвращает 1, если пора выходить. */
int win_poll(WinWindow *w);

/* Размер области отрисовки в пикселях (для aspect ratio). */
void win_size(const WinWindow *w, int *width, int *height);

void win_swap(const WinWindow *w);

/* ---------- Клавиши ----------
 * GLFW сообщает о клавишах по их физическому положению на клавиатуре,
 * а не по символу, поэтому WASD работает в любой раскладке
 * (латинской, русской и т.д.). */
typedef enum {
    WIN_KEY_W = 0,
    WIN_KEY_A,
    WIN_KEY_S,
    WIN_KEY_D,
    WIN_KEY_SPACE,
    WIN_KEY_SHIFT,
    WIN_KEY_UP,
    WIN_KEY_DOWN,
    WIN_KEY_LEFT,
    WIN_KEY_RIGHT,
    WIN_KEY_COUNT
} WinKey;

int win_key_down(WinKey key);
int win_button_down(int button);  /* код мыши GLFW: 0 — левая, 1 — правая (для будущего оружия) */

#endif
