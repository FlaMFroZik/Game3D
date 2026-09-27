#ifndef RENDER_H
#define RENDER_H

/* ------------------------------------------------------------------
 * Рендер кадра: настройка OpenGL, камера и отрисовка мира.
 *
 * Мир редактора — это кубы карты (map/map.c); процедурного рельефа
 * здесь нет, а вместо игрока с физикой — свободная камера Camera.
 * ------------------------------------------------------------------ */

#include <GL/gl.h>

#include "render/prim.h"
#include "render/window.h"

#define RENDER_FOV        60.0   /* угол обзора по вертикали, градусы */
#define RENDER_NEAR       0.1
#define RENDER_FAR        300.0

/* В редакторе туман — только лёгкая глубина кадра: карту нужно видеть
 * целиком, поэтому он начинается много дальше, чем в игре. */
#define RENDER_FOG_START  60.0f
#define RENDER_FOG_END    110.0f

/* Наклон камеры чуть меньше вертикали, чтобы взгляд не «переворачивался». */
#define RENDER_PITCH_LIMIT 1.55f

/* Свободная камера редактора: позиция и направление взгляда. */
typedef struct {
    float x, y, z;
    float yaw;      /* поворот влево/вправо, радианы */
    float pitch;    /* наклон вверх/вниз, радианы */
} Camera;

typedef struct {
    float sky[4];      /* цвет неба и тумана (rgba) */
    Texture texture;   /* текстура кубов без своей (см. render/prim.h) */
    GLdouble fov;      /* угол обзора по вертикали, градусы */
    GLdouble near_plane;
    GLdouble far_plane;
} Renderer;

/* Создаёт рендерер. texture_file может быть NULL или не читаться —
 * тогда кубам без своей текстуры рисуется встроенная «шахматка».
 * 0 — только если не удалось создать вообще никакую текстуру. */
int  render_init(Renderer *r, const char *texture_file);

/* Включает состояние OpenGL, действующее всё время работы редактора
 * (глубина, туман). Вызывать после того, как контекст сделан текущим. */
void render_setup_gl(void);

/* Освобождает ресурсы рендерера. */
void render_shutdown(Renderer *r);

/* Единичный вектор взгляда камеры. */
void render_view_dir(const Camera *cam, float *dx, float *dy, float *dz);

/* Рисует кубы карты. Кадр должен быть уже очищен и настроен. */
void render_world(const Renderer *r);

/* Очищает экран цветом неба. */
void render_clear(const Renderer *r);

/* Настраивает область отрисовки (glViewport), проекцию и камеру под размер
 * окна. Вызывать каждый кадр после render_clear: при растягивании окна мир
 * должен заполнять его целиком, а не рисоваться в старом прямоугольнике. */
void render_camera(const Renderer *r, const Camera *cam, int width, int height);

#endif
