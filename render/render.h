#ifndef RENDER_H
#define RENDER_H

/* ------------------------------------------------------------------
 * Рендер кадра: настройка OpenGL, камера и отрисовка мира.
 * ------------------------------------------------------------------ */

#include <GL/gl.h>

#include "physics/physics.h"
#include "render/prim.h"
#include "render/window.h"

#define RENDER_FOV        60.0   /* угол обзора по вертикали, градусы */
#define RENDER_NEAR       0.1
#define RENDER_FAR        300.0

#define RENDER_FOG_START  12.0f  /* до этой дистанции тумана нет */
#define RENDER_FOG_END    20.0f  /* дальше — полностью туман */

/* Запас к концу тумана: мир готовится чуть дальше, чтобы край рельефа
 * гарантированно не попадал в кадр (см. render_view_radius). */
#define RENDER_VIEW_MARGIN 0.5f

typedef struct {
    float sky[4];      /* цвет неба и тумана (rgba) */
    Texture texture;   /* текстура рельефа и блоков (см. render/prim.h) */
    GLdouble fov;      /* угол обзора по вертикали, градусы */
    GLdouble near_plane;
    GLdouble far_plane;
} Renderer;

/* Создаёт рендерер и включает состояние OpenGL. 0 — текстуру загрузить не удалось. */
int  render_init(Renderer *r, const char *texture_file);

/* Включает состояние OpenGL, действующее всё время работы игры
 * (глубина, туман). Вызывать после того, как контекст сделан текущим. */
void render_setup_gl(void);

/* Освобождает ресурсы рендерера. */
void render_shutdown(Renderer *r);

/* Рисует мир от лица игрока. Кадр должен быть уже очищен и настроен.
 * Рельеф рисуется на радиус render_view_radius вокруг игрока — ровно то,
 * что видно до конца тумана; дальние клетки загруженных чанков
 * пропускаются, чтобы кадр не стоил дороже, чем нужно. */
void render_world(const Renderer *r, const Player *player);

/* Очищает экран цветом неба. */
void render_clear(const Renderer *r);

/* Настраивает область отрисовки (glViewport), проекцию и камеру под размер
 * окна. Вызывать каждый кадр после render_clear: при растягивании окна мир
 * должен заполнять его целиком, а не рисоваться в старом прямоугольнике. */
void render_camera(const Renderer *r, const Player *player, int width, int height);

/* Расстояние, на котором мир обязан быть готов, чтобы кадр не оборвался:
 * конец тумана плюс запас, с учётом высоты камеры. main передаёт это
 * значение в gen_set_view_radius (см. map/gen.h). */
float render_view_radius(const Player *player);

#endif
