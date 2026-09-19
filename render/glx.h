#ifndef GLX_GAME_H
#define GLX_GAME_H

/* ------------------------------------------------------------------
 * Окно, контекст OpenGL и ввод — всё, что зависит от X11/GLX.
 * Остальные модули про X11 не знают.
 * ------------------------------------------------------------------ */

#include <X11/Xlib.h>
#include <GL/gl.h>
#include <GL/glx.h>
#include <GL/glu.h>

typedef struct {
    Display *display;
    Window   window;
    GLXContext context;
} GlxWindow;

/* Создаёт окно и контекст. Возвращает 0, если что-то не удалось
 * (в этом случае окно открывать не нужно). */
int glx_init(GlxWindow *w, int width, int height, const char *title);

/* Разрушает контекст, окно и соединение с сервером. */
void glx_shutdown(GlxWindow *w);

/* Обрабатывает все накопившиеся события. Возвращает 1, если пора выходить. */
int glx_poll(GlxWindow *w);

/* Размер клиентской области окна в пикселях (для aspect ratio). */
void glx_size(const GlxWindow *w, int *width, int *height);

void glx_swap(const GlxWindow *w);

/* ---------- Клавиши ----------
 * Состояние хранится по логическим (keysym) клавишам, а не по keycode:
 * так работают и латинская, и русская раскладка. */
typedef enum {
    GLX_KEY_W = 0,
    GLX_KEY_A,
    GLX_KEY_S,
    GLX_KEY_D,
    GLX_KEY_SPACE,
    GLX_KEY_SHIFT,
    GLX_KEY_UP,
    GLX_KEY_DOWN,
    GLX_KEY_LEFT,
    GLX_KEY_RIGHT,
    GLX_KEY_COUNT
} GlxKey;

int glx_key_down(GlxKey key);
int glx_button_down(int button);  /* 1 — левая, 3 — правая (для будущего оружия) */

#endif
