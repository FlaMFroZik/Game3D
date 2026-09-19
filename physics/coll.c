#include <math.h>

#include "physics/coll.h"
#include "map/gen.h"
#include "map/map.h"

#define COLL_PROBE_POINTS 8  /* точек по окружности коллайдера */

static float s_current_feet_y = 0.0f;

void coll_set_feet_y(float feet_y) {
    s_current_feet_y = feet_y;
}

float coll_ground_height(float x, float z) {
    float h = gen_sample_height(x, z);
    if (map_is_custom()) {
        h = map_ground_height(x, z, s_current_feet_y, h);
    }
    return h;
}

int coll_point_blocked(float px, float pz, float bottom_y) {
    if (map_point_blocked(px, pz, bottom_y)) {
        return 1;
    }
    float h = coll_ground_height(px, pz);
    return (h - bottom_y > COLL_STEP_HEIGHT);
}

int coll_capsule_blocked(float cx, float cz, float feet_y) {
    const float angle_step = 2.0f * 3.14159265f / COLL_PROBE_POINTS;
    float h_center = coll_ground_height(cx, cz);

    for (int i = 0; i < COLL_PROBE_POINTS; i++) {
        float angle = (float)i * angle_step;
        float px = cx + COLL_RADIUS * cosf(angle);
        float pz = cz + COLL_RADIUS * sinf(angle);

        if (coll_point_blocked(px, pz, feet_y)) {
            return 1;
        }

        /* Резкий перепад высот относительно центра — стена или обрыв. */
        float h_point = coll_ground_height(px, pz);
        if (fabsf(h_point - h_center) > COLL_STEP_HEIGHT) {
            float max_h = fmaxf(h_point, h_center);
            if (feet_y < max_h - COLL_EPSILON) {
                return 1;
            }
        }
    }

    if (coll_point_blocked(cx, cz, feet_y)) {
        return 1;
    }

    return 0;
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
