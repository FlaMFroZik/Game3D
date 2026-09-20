#include "map/gen.h"
#include "map/map.h"
#include "physics/physics.h"
#include "render/prim.h"
#include "render/render.h"

/* ------------------------------------------------------------------
 * Матрицы задаются фиксированным конвейером (glMatrixMode/gluPerspective/
 * gluLookAt), шейдеров нет — менять эту часть можно без правок остальных
 * модулей.
 * ------------------------------------------------------------------ */

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
    float aspect = (height > 0) ? (float)width / (float)height : 1.0f;

    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    gluPerspective(r->fov, (double)aspect, r->near_plane, r->far_plane);

    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();

    float dir_x, dir_y, dir_z;
    phys_view_dir(player, &dir_x, &dir_y, &dir_z);

    gluLookAt(player->x, player->y, player->z,
              player->x + dir_x, player->y + dir_y, player->z + dir_z,
              0.0f, 1.0f, 0.0f);
}

void render_world(const Renderer *r, const Player *player) {
    /* чанк игрока в координатах клеток (мировые единицы -> клетки) */
    int pcx = gen_chunk_coord(gen_world_to_cell(player->x));
    int pcz = gen_chunk_coord(gen_world_to_cell(player->z));

    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, r->texture.id);

    /* Карта и процедурная генерация взаимно исключают: генерация —
     * fallback, её рисуем только когда карта из файла не загружена,
     * иначе рельеф накладывается на кубы карты. */
    if (map_is_custom()) {
        map_render(&r->texture);
    } else {
        for (int i = 0; i < gen_chunk_count(); i++) {
            const Chunk *c = gen_chunk_at(i);

            /* рисуем только чанки в радиусе видимости */
            if (c->cx < pcx - GEN_VIEW_RADIUS || c->cx > pcx + GEN_VIEW_RADIUS) continue;
            if (c->cz < pcz - GEN_VIEW_RADIUS || c->cz > pcz + GEN_VIEW_RADIUS) continue;

            for (int x = 0; x < GEN_CHUNK_SIZE; x++) {
                for (int z = 0; z < GEN_CHUNK_SIZE; z++) {
                    CellQuad quad;

                    quad.x0 = (float)(c->cx * GEN_CHUNK_SIZE + x) * GEN_CELL_SIZE;
                    quad.z0 = (float)(c->cz * GEN_CHUNK_SIZE + z) * GEN_CELL_SIZE;
                    quad.x1 = quad.x0 + GEN_CELL_SIZE;
                    quad.z1 = quad.z0 + GEN_CELL_SIZE;

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
