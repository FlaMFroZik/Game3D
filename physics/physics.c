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

/* Низ коллайдера: ноги на половину высоты ниже камеры. */
static float feet_y(const Player *p) {
    return p->y - COLL_HEIGHT * 0.5f;
}

/* Ходьба и бег со скольжением вдоль стен. */
static void move_horizontal(Player *p, const PlayerInput *in, float step) {
    /* Горизонтальные оси движения зависят только от yaw: pitch не должен
     * влиять на скорость ходьбы (иначе при взгляде вверх/вниз идём медленнее). */
    const float dir_x =  sinf(p->yaw);
    const float dir_z = -cosf(p->yaw);
    const float right_x = -dir_z;
    const float right_z =  dir_x;

    float move = PHYS_MOVE_SPEED * step;
    float dx = (in->forward * dir_x + in->strafe * right_x) * move;
    float dz = (in->forward * dir_z + in->strafe * right_z) * move;
    if (in->run) {
        dx *= PHYS_RUN_FACTOR;
        dz *= PHYS_RUN_FACTOR;
    }

    coll_move(&p->x, &p->z, dx, dz, feet_y(p));
}

/* Гравитация, приземление и прыжок. */
static void move_vertical(Player *p, const PlayerInput *in, float step) {
    /* Земля и потолок под/над игроком уже с учётом горизонтального шага. */
    const float cur_feet = feet_y(p);
    coll_set_feet_y(cur_feet);

    const float ground_y = coll_player_ground_height(p->x, p->z);
    const float ceiling_y = coll_player_ceiling_height(p->x, p->z);

    const float target_y = ground_y + COLL_HEIGHT * 0.5f;
    const float max_y = ceiling_y - COLL_HEIGHT * 0.5f;
    const int on_ground = (fabsf(p->y - target_y) < COLL_EPSILON);

    if (on_ground) {
        p->vel_y = 0.0f;
    } else {
        p->vel_y += PHYS_GRAVITY * step;
    }

    float next_y = p->y + p->vel_y * step;

    if (next_y <= target_y) {
        /* Приземление или удержание на поверхности. */
        next_y = target_y;
        if (p->vel_y < 0.0f) {
            p->vel_y = 0.0f;
        }

        /* Прыжок, если пробел зажат в момент приземления. */
        if (in->jump && target_y < max_y - COLL_EPSILON) {
            p->vel_y = PHYS_JUMP_FORCE;
            next_y = target_y + p->vel_y * step;
        }
    } else if (on_ground && in->jump && target_y < max_y - COLL_EPSILON) {
        /* Прыжок с земли. */
        p->vel_y = PHYS_JUMP_FORCE;
        next_y = p->y + p->vel_y * step;
    }

    /* Столкновение с потолком (низом блоков сверху). */
    if (next_y >= max_y) {
        next_y = max_y;
        if (p->vel_y > 0.0f) {
            p->vel_y = 0.0f;
        }
    }

    /* Если зазор между полом и потолком меньше роста игрока, удерживаем на полу. */
    if (next_y < target_y) {
        next_y = target_y;
    }

    p->y = next_y;
}

/* Поворот камеры; наклон ограничен PHYS_PITCH_LIMIT. */
static void look(Player *p, const PlayerInput *in, float step) {
    const float angle = PHYS_LOOK_SPEED * step;

    p->yaw   += in->look_x * angle;
    p->pitch += in->look_y * angle;

    if (p->pitch >  PHYS_PITCH_LIMIT) p->pitch =  PHYS_PITCH_LIMIT;
    if (p->pitch < -PHYS_PITCH_LIMIT) p->pitch = -PHYS_PITCH_LIMIT;
}

void phys_update(Player *p, const PlayerInput *in, double dt) {
    const float step = (float)dt;

    move_horizontal(p, in, step);
    move_vertical(p, in, step);
    look(p, in, step);
}
