#include <math.h>

#include "physics/coll.h"
#include "map/gen.h"
#include "map/map.h"

#define COLL_PROBE_POINTS 16  /* точек по окружности коллайдера */
#define COLL_PI           3.14159265f

static float s_current_feet_y = 0.0f;

void coll_set_feet_y(float feet_y) {
    s_current_feet_y = feet_y;
}

/* Точка i из COLL_PROBE_POINTS на окружности коллайдера с центром (cx, cz). */
static void probe_point(int i, float cx, float cz, float *px, float *pz) {
    const float angle = (float)i * (2.0f * COLL_PI / COLL_PROBE_POINTS);
    *px = cx + COLL_RADIUS * cosf(angle);
    *pz = cz + COLL_RADIUS * sinf(angle);
}

/* Можно ли встать на землю высотой h: она не выше ног больше чем на шаг. */
static int within_step(float h) {
    return h <= s_current_feet_y + COLL_STEP_HEIGHT + COLL_EPSILON;
}

float coll_ground_height(float x, float z) {
    /* Карта и процедурная генерация взаимно исключают: если карта
     * загружена, земля — только вершины её кубов (вне кубов — пол y = 0),
     * процедурный рельеф в мир не должен просачиваться. */
    if (map_is_custom()) {
        return map_ground_height(x, z, s_current_feet_y, 0.0f);
    }
    return gen_sample_height(x, z);
}

float coll_player_ground_height(float cx, float cz) {
    if (map_is_custom()) {
        return map_cylinder_ground_height(cx, cz, COLL_RADIUS, s_current_feet_y, 0.0f);
    }

    float highest = gen_sample_height(cx, cz);
    for (int i = 0; i < COLL_PROBE_POINTS; i++) {
        float px, pz;
        probe_point(i, cx, cz, &px, &pz);

        float h = gen_sample_height(px, pz);
        if (within_step(h) && h > highest) {
            highest = h;
        }
    }
    return highest;
}

float coll_ceiling_height(float x, float z) {
    if (map_is_custom()) {
        return map_ceiling_height(x, z, s_current_feet_y);
    }
    return INFINITY;
}

float coll_player_ceiling_height(float cx, float cz) {
    if (map_is_custom()) {
        return map_cylinder_ceiling_height(cx, cz, COLL_RADIUS, s_current_feet_y);
    }
    return INFINITY;
}

int coll_point_blocked(float px, float pz, float bottom_y) {
    if (map_point_blocked(px, pz, bottom_y)) {
        return 1;
    }
    float h = coll_ground_height(px, pz);
    return (h - bottom_y > COLL_STEP_HEIGHT);
}

int coll_capsule_blocked(float cx, float cz, float feet_y) {
    if (map_is_custom()) {
        return map_capsule_blocked(cx, cz, feet_y);
    }

    float h_center = coll_ground_height(cx, cz);

    for (int i = 0; i < COLL_PROBE_POINTS; i++) {
        float px, pz;
        probe_point(i, cx, cz, &px, &pz);

        if (coll_point_blocked(px, pz, feet_y)) {
            return 1;
        }

        /* Резкий перепад высот относительно центра — стена или обрыв. */
        float h_point = coll_ground_height(px, pz);
        if (fabsf(h_point - h_center) > COLL_STEP_HEIGHT
            && feet_y < fmaxf(h_point, h_center) - COLL_EPSILON) {
            return 1;
        }
    }

    return coll_point_blocked(cx, cz, feet_y);
}

void coll_move(float *x, float *z, float dx, float dz, float feet_y) {
    coll_set_feet_y(feet_y);

    /* Два прохода: сначала X, потом Z — это и даёт скольжение вдоль стены. */
    float new_x = *x + dx;
    if (!coll_capsule_blocked(new_x, *z, feet_y)) {
        *x = new_x;
    }

    float new_z = *z + dz;
    if (!coll_capsule_blocked(*x, new_z, feet_y)) {
        *z = new_z;
    }
}
