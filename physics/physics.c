#include <math.h>

#include "physics/coll.h"
#include "physics/physics.h"

void phys_init(Player *p) {
    p->x = 0.0f;
    p->y = PHYS_START_Y;
    p->z = 0.0f;
    p->yaw = 0.0f;
    p->pitch = PHYS_START_PITCH;
    p->vel_y = 0.0f;
}

void phys_view_dir(const Player *p, float *dx, float *dy, float *dz) {
    float cp = cosf(p->pitch);
    *dx = sinf(p->yaw) * cp;
    *dy = sinf(p->pitch);
    *dz = -cosf(p->yaw) * cp;
}

void phys_update(Player *p, const PlayerInput *in, double dt) {
    float step = (float)dt;

    /* Горизонтальные оси камеры: вперёд — взгляд, вправо — перпендикуляр. */
    float dir_x, dir_y, dir_z;
    phys_view_dir(p, &dir_x, &dir_y, &dir_z);
    float right_x = -dir_z;
    float right_z =  dir_x;

    float move = PHYS_MOVE_SPEED * step;
    float look = PHYS_LOOK_SPEED * step;

    float dx = (in->forward * dir_x + in->strafe * right_x) * move;
    float dz = (in->forward * dir_z + in->strafe * right_z) * move;
    if (in->run) {
        dx *= 2.0f;
        dz *= 2.0f;
    }

    /* Ноги на половину высоты коллайдера ниже камеры. */
    float feet_y = p->y - COLL_HEIGHT * 0.5f;
    coll_set_feet_y(feet_y);
    coll_move(&p->x, &p->z, dx, dz, feet_y);

    /* --- Вертикальная физика --- */

    /* Земля под игроком уже с учётом горизонтального шага. */
    feet_y = p->y - COLL_HEIGHT * 0.5f;
    coll_set_feet_y(feet_y);
    float target_y = coll_ground_height(p->x, p->z) + COLL_HEIGHT * 0.5f;
    int on_ground = (fabsf(p->y - target_y) < COLL_EPSILON);

    if (on_ground) {
        p->vel_y = 0.0f;
    } else {
        p->vel_y += PHYS_GRAVITY * step;
    }

    float next_y = p->y + p->vel_y * step;

    if (p->vel_y <= 0.0f && next_y <= target_y) {
        /* Приземление. */
        next_y = target_y;
        p->vel_y = 0.0f;

        /* Прыжок, если пробел зажат в момент приземления. */
        if (in->jump) {
            p->vel_y = PHYS_JUMP_FORCE;
            next_y = p->y + p->vel_y * step;
        }
    } else if (on_ground && in->jump) {
        /* Прыжок с земли. */
        p->vel_y = PHYS_JUMP_FORCE;
        next_y = p->y + p->vel_y * step;
    }

    p->y = next_y;

    /* Поворот камеры. */
    p->yaw   += in->look_x * look;
    p->pitch += in->look_y * look;

    if (p->pitch >  PHYS_PITCH_LIMIT) p->pitch =  PHYS_PITCH_LIMIT;
    if (p->pitch < -PHYS_PITCH_LIMIT) p->pitch = -PHYS_PITCH_LIMIT;
}
