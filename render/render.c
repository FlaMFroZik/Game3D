#include <math.h>

#include <GL/gl.h>

#include "map/gen.h"
#include "map/map.h"
#include "physics/coll.h"
#include "physics/physics.h"
#include "render/prim.h"
#include "render/render.h"

#define RENDER_PI 3.14159265358979323846

/* ------------------------------------------------------------------
 * Матрицы задаются фиксированным конвейером (glMatrixMode/glFrustum),
 * шейдеров нет — менять эту часть можно без правок остальных модулей.
 * ------------------------------------------------------------------ */

static void render_perspective(GLdouble fov, GLdouble aspect,
                               GLdouble near_plane, GLdouble far_plane) {
    const GLdouble half_angle = fov * RENDER_PI / 360.0;
    const GLdouble top = near_plane * tan(half_angle);
    const GLdouble right = top * aspect;

    glFrustum(-right, right, -top, top, near_plane, far_plane);
}

/* Замена gluLookAt без зависимости от GLU. Направление взгляда уже единичное
 * (его создаёт phys_view_dir), поэтому здесь нужен только базис камеры. */
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

/* Камера стоит в середине коллайдера и при наклоне взгляда не сдвигается
 * (см. physics), поэтому её высота — это высота центра, спроецированная
 * на вертикаль. Нужна, чтобы посчитать, докуда достаёт луч в горизонт. */
static double render_eye_height(const Player *player) {
    const double half_height = 0.5 * (double)COLL_HEIGHT;
    double pitch = (double)player->pitch;
    if (pitch < -PHYS_PITCH_LIMIT) pitch = -PHYS_PITCH_LIMIT;
    if (pitch > PHYS_PITCH_LIMIT) pitch = PHYS_PITCH_LIMIT;

    return (double)player->y - half_height * cos(pitch);
}

float render_view_radius(const Player *player) {
    /* Всё, что дальше конца тумана, залито его цветом и на картинке уже не
     * видно. Значит, мир должен быть готов ровно на эту глубину — и ни
     * метром меньше, иначе на краю кадра появится обрыв рельефа.
     * Пиксель на горизонте уходит вдаль тем дальше по земле, чем выше
     * камера, отсюда гипотенуза. От размера окна радиус не зависит: широкое
     * окно показывает больше мира по сторонам, а не «за туман». */
    const double eye_y = render_eye_height(player);
    const double fog_end = (double)RENDER_FOG_END;

    return (float)(sqrt(eye_y * eye_y + fog_end * fog_end) + RENDER_VIEW_MARGIN);
}

int render_init(Renderer *r, const char *texture_file) {
    r->sky[0] = 0.45f;
    r->sky[1] = 0.65f;
    r->sky[2] = 0.85f;
    r->sky[3] = 1.0f;

    r->fov = RENDER_FOV;
    r->near_plane = RENDER_NEAR;
    r->far_plane = RENDER_FAR;

    r->texture = prim_load_texture(texture_file);
    return r->texture.id != 0;
}

void render_setup_gl(void) {
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_FOG);
    glFogi(GL_FOG_MODE, GL_LINEAR);   /* либо GL_EXP / GL_EXP2 (тогда нужен GL_FOG_DENSITY) */
    glFogf(GL_FOG_START, RENDER_FOG_START);
    glFogf(GL_FOG_END,   RENDER_FOG_END);
    glHint(GL_FOG_HINT, GL_NICEST);   /* расчёт на каждый фрагмент, если драйвер позволяет */
}

void render_shutdown(Renderer *r) {
    prim_free_texture(&r->texture);
}

void render_clear(const Renderer *r) {
    glClearColor(r->sky[0], r->sky[1], r->sky[2], r->sky[3]);  /* небо */
    /* Цвет тумана = цвет неба => горизонт бесшовный, а дальние
     * края чанков плавно растворяются. */
    glFogfv(GL_FOG_COLOR, r->sky);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

void render_camera(const Renderer *r, const Player *player, int width, int height) {
    /* Область отрисовки — окно целиком. Без этого OpenGL рисует в размер,
     * который был при создании контекста: растянутое окно показывало бы
     * картинку в углу, а остальное оставалось чёрным. */
    if (width <= 0 || height <= 0) {
        width = 1;
        height = 1;
    }
    glViewport(0, 0, width, height);

    float aspect = (height > 0) ? (float)width / (float)height : 1.0f;

    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    render_perspective(r->fov, (GLdouble)aspect, r->near_plane, r->far_plane);

    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();

    float dir_x, dir_y, dir_z;
    phys_view_dir(player, &dir_x, &dir_y, &dir_z);
    render_look_at(player->x, player->y, player->z, dir_x, dir_y, dir_z);
}

void render_world(const Renderer *r, const Player *player) {
    glEnable(GL_TEXTURE_2D);

    /* Карта и процедурная генерация взаимно исключают: генерация —
     * fallback, её рисуем только когда карта из файла не загружена,
     * иначе рельеф накладывается на кубы карты. */
    if (map_is_custom()) {
        /* У кубов карты могут быть свои текстуры, поэтому map_render сам
         * привязывает нужную текстуру каждому кубу; r->texture — та, которой
         * нарисуются кубы без своей. */
        map_render(&r->texture);
    } else {
        glBindTexture(GL_TEXTURE_2D, r->texture.id);

        /* Рисуем всё загруженное вокруг игрока, но за пределами радиуса
         * видимости клетки пропускаем: там они всё равно залиты цветом
         * тумана, а платить за них кадром не нужно. Границы «нарисовано»
         * и «видно» совпадают, поэтому обрыва рельефа в кадре не бывает
         * при любом размере окна. */
        const float reach = render_view_radius(player);
        const float reach_sq = reach * reach;

        for (int i = 0; i < gen_chunk_count(); i++) {
            const Chunk *c = gen_chunk_at(i);

            for (int x = 0; x < GEN_CHUNK_SIZE; x++) {
                for (int z = 0; z < GEN_CHUNK_SIZE; z++) {
                    CellQuad quad;

                    quad.x0 = (float)(c->cx * GEN_CHUNK_SIZE + x) * GEN_CELL_SIZE;
                    quad.z0 = (float)(c->cz * GEN_CHUNK_SIZE + z) * GEN_CELL_SIZE;
                    quad.x1 = quad.x0 + GEN_CELL_SIZE;
                    quad.z1 = quad.z0 + GEN_CELL_SIZE;

                    /* ближайшая к игроку точка клетки: если она дальше
                     * радиуса видимости, клетку не видно совсем */
                    float nx = player->x;
                    if (nx < quad.x0) nx = quad.x0;
                    if (nx > quad.x1) nx = quad.x1;
                    float nz = player->z;
                    if (nz < quad.z0) nz = quad.z0;
                    if (nz > quad.z1) nz = quad.z1;
                    float dx = player->x - nx;
                    float dz = player->z - nz;
                    if (dx * dx + dz * dz > reach_sq) continue;

                    quad.y00 = gen_chunk_y(c, x,     z);
                    quad.y10 = gen_chunk_y(c, x + 1, z);
                    quad.y01 = gen_chunk_y(c, x,     z + 1);
                    quad.y11 = gen_chunk_y(c, x + 1, z + 1);

                    prim_draw_cell(&r->texture, &quad);
                }
            }
        }
    }

    glDisable(GL_TEXTURE_2D);
}
