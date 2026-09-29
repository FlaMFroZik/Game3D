#include <math.h>

#include <GL/gl.h>

#include "map/map.h"
#include "render/prim.h"
#include "render/render.h"

#define RENDER_PI 3.14159265358979323846

static void render_perspective(GLdouble fov, GLdouble aspect,
                               GLdouble near_plane, GLdouble far_plane) {
    const GLdouble half_angle = fov * RENDER_PI / 360.0;
    const GLdouble top = near_plane * tan(half_angle);
    const GLdouble right = top * aspect;

    glFrustum(-right, right, -top, top, near_plane, far_plane);
}

static void render_look_at(float eye_x, float eye_y, float eye_z,
                           float forward_x, float forward_y, float forward_z) {
    const float up_x = 0.0f;
    const float up_y = 1.0f;
    const float up_z = 0.0f;

    /* side = forward x up */
    float side_x = forward_y * up_z - forward_z * up_y;
    float side_y = forward_z * up_x - forward_x * up_z;
    float side_z = forward_x * up_y - forward_y * up_x;
    float side_length = sqrtf(side_x * side_x + side_y * side_y + side_z * side_z);
    if (side_length < 1e-6f) return;

    side_x /= side_length;
    side_y /= side_length;
    side_z /= side_length;

    /* camera_up = side x forward */
    const float camera_up_x = side_y * forward_z - side_z * forward_y;
    const float camera_up_y = side_z * forward_x - side_x * forward_z;
    const float camera_up_z = side_x * forward_y - side_y * forward_x;

    const GLdouble matrix[16] = {
        side_x,       camera_up_x,       -forward_x,       0.0,
        side_y,       camera_up_y,       -forward_y,       0.0,
        side_z,       camera_up_z,       -forward_z,       0.0,
        -(side_x * eye_x + side_y * eye_y + side_z * eye_z),
        -(camera_up_x * eye_x + camera_up_y * eye_y + camera_up_z * eye_z),
          forward_x * eye_x + forward_y * eye_y + forward_z * eye_z,
        1.0
    };
    glMultMatrixd(matrix);
}

static float clampf(float v, float lo, float hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

void render_view_dir(const Camera *cam, float *dx, float *dy, float *dz) {
    const float pitch = clampf(cam->pitch, -RENDER_PITCH_LIMIT, RENDER_PITCH_LIMIT);
    const float cp = cosf(pitch);

    *dx = sinf(cam->yaw) * cp;
    *dy = sinf(pitch);
    *dz = -cosf(cam->yaw) * cp;
}

int render_init(Renderer *r, const char *texture_file) {
    /* Hammer 3D Viewport темно-серый/синеватый фон неба */
    r->sky[0] = 0.14f;
    r->sky[1] = 0.16f;
    r->sky[2] = 0.19f;
    r->sky[3] = 1.0f;

    r->fov = RENDER_FOV;
    r->near_plane = RENDER_NEAR;
    r->far_plane = RENDER_FAR;

    r->texture.id = 0;
    if (texture_file && texture_file[0] != '\0') {
        r->texture = prim_load_texture(texture_file);
    }

    if (r->texture.id == 0) {
        const Texture *bt = prim_find_builtin("dev/dev_measureorange");
        if (bt) {
            r->texture = *bt;
        } else {
            r->texture = prim_make_checker_texture();
        }
    }
    return r->texture.id != 0;
}

void render_setup_gl(void) {
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_FOG);
    glFogi(GL_FOG_MODE, GL_LINEAR);
    glFogf(GL_FOG_START, RENDER_FOG_START);
    glFogf(GL_FOG_END,   RENDER_FOG_END);
    glHint(GL_FOG_HINT, GL_NICEST);
}

void render_shutdown(Renderer *r) {
    if (!prim_find_builtin(map_texture_path(&r->texture))) {
        prim_free_texture(&r->texture);
    }
}

void render_clear(const Renderer *r) {
    glClearColor(r->sky[0], r->sky[1], r->sky[2], r->sky[3]);
    glFogfv(GL_FOG_COLOR, r->sky);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

void render_camera(const Renderer *r, const Camera *cam, int width, int height) {
    if (width <= 0 || height <= 0) {
        width = 1;
        height = 1;
    }

    const float aspect = (float)width / (float)height;

    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    render_perspective(r->fov, (GLdouble)aspect, r->near_plane, r->far_plane);

    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();

    float dir_x, dir_y, dir_z;
    render_view_dir(cam, &dir_x, &dir_y, &dir_z);
    render_look_at(cam->x, cam->y, cam->z, dir_x, dir_y, dir_z);
}

void render_world(const Renderer *r) {
    glEnable(GL_TEXTURE_2D);
    map_render(&r->texture);
    glDisable(GL_TEXTURE_2D);
}
