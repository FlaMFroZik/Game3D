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

/* Обрабатывает все накопившиеся события. Возвращает 1, если окно просят
 * закрыть (крестик оконного менеджера). Esc больше не завершает игру:
 * его нажатие отдаётся меню — см. win_escape_pressed. */
int win_poll(WinWindow *w);

/* Монотонное время в секундах, одинаковое на Windows и Linux. */
double win_time_seconds(void);

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

/* ---------- Мышь и Esc: ввод для меню ----------
 * Все функции отдают событие один раз: чтение снимает флаг. */

/* Курсор мыши в пикселях области отрисовки. Логические пиксели окна и
 * пиксели кадра совпадают не всегда (экраны с масштабированием), поэтому
 * координата сразу пересчитывается в размер кадра — в тех же единицах
 * рисуется интерфейс. */
void win_pointer_pixels(const WinWindow *w, int *x, int *y);

/* 1 один раз на нажатие кнопки мыши (0 — левая). */
int win_mouse_clicked(int button);

/* Прокрутка колеса мыши, накопленная с прошлого чтения, в «строках». */
double win_scroll_delta(void);

/* 1 один раз на нажатие Esc. */
int win_escape_pressed(void);

/* Забыть ненажатые события (клики, колесо, Esc). Вызывается при смене
 * экрана, чтобы клик, закрывший одно меню, не нажал кнопку под ним. */
void win_reset_input(void);

#endif
