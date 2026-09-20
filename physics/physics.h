#ifndef PHYSICS_H
#define PHYSICS_H

/* ------------------------------------------------------------------
 * Физика игрока: перемещение, гравитация, прыжок и поворот камеры.
 * Модуль ничего не знает про GLFW и OpenGL: на вход идёт PlayerInput,
 * на выходе — обновлённое состояние игрока (Player).
 * ------------------------------------------------------------------ */

#define PHYS_MOVE_SPEED  8.0f   /* скорость ходьбы, мировых единиц в секунду */
#define PHYS_LOOK_SPEED  1.2f   /* скорость поворота камеры, радиан в секунду */
#define PHYS_GRAVITY    -9.8f   /* ускорение свободного падения */
#define PHYS_JUMP_FORCE  6.0f   /* начальная скорость прыжка */
#define PHYS_PITCH_LIMIT 1.5f   /* куда можно задирать/опускать взгляд */

#define PHYS_START_Y     20.0f  /* стартовая высота камеры */
#define PHYS_START_PITCH (-0.5f)

typedef struct {
    float x, y, z;   /* позиция камеры (центр коллайдера) */
    float yaw;       /* поворот влево/вправо, радианы */
    float pitch;     /* наклон вверх/вниз, радианы */
    float vel_y;     /* вертикальная скорость */
} Player;

/* Намерения игрока на текущий шаг; значения осей в [-1 .. 1]. */
typedef struct {
    float forward;   /* -1 назад, +1 вперёд */
    float strafe;    /* -1 влево, +1 вправо */
    float look_x;    /* -1 влево, +1 вправо */
    float look_y;    /* -1 вниз,  +1 вверх */
    int   jump;      /* пробел */
    int   run;       /* shift — ускорение */
} PlayerInput;

void phys_init(Player *p);

/* Один шаг физики с фиксированным dt (в секундах). */
void phys_update(Player *p, const PlayerInput *in, double dt);

/* Единичный вектор взгляда камеры. */
void phys_view_dir(const Player *p, float *dx, float *dy, float *dz);

#endif
