#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>
#include <time.h>
#include <unistd.h>
#include <X11/X.h>
#include <X11/Xlib.h>
#include <X11/keysym.h>
#include <GL/gl.h>
#include <GL/glx.h>
#include <GL/glu.h>

#include "image.h"
#include "gen.h"

/* ---------- Параметры ---------- */

#define SIZE             0.25f   /* размер одной клетки рельефа в мировых единицах */
#define CHUNK_SIZE       32      /* клеток по стороне чанка */
#define VIEW_RADIUS      2       /* радиус видимости в чанках */
#define MAX_CHUNKS       256
#define MOVE_SPEED       8.0f
#define LOOK_SPEED       1.2f
#define MAX_CHUNKS_PER_FRAME 8

/* Коллизии */
#define COLLIDER_RADIUS   0.4f
#define COLLISION_EPSILON 0.01f
#define STEP_HEIGHT       1.0f   /* максимальная высота подъёма за один шаг */
#define COLLIDER_HEIGHT   1.8f

/* ---------- Физика ---------- */
float vel_y = 0.0f;
float GRAVITY = -9.8f;
float JUMP_FORCE = 6.0f;
float TERRAIN_OFFSET = 1.0f;

/* ---------- GL / X11 ---------- */

int att[] = { GLX_RGBA, GLX_DEPTH_SIZE, 24, GLX_DOUBLEBUFFER, None };
GLXContext glc;
Display *display;
Window window;
XWindowAttributes gwa;
GLuint texture_id = 0;

double TARGET_FPS = 60.0;
double FRAME_DURATION = 1.0 / TARGET_FPS;

float SKY_COLOR[4] = { 0.45f, 0.65f, 0.85f, 1.0f };

/* ---------- Клавиши ----------
 * Состояние хранится по логическим (keysym) клавишам, а не по keycode:
 * так работают и латинская, и русская раскладка (W=A=ЦФЫВ по позиции),
 * и переключение раскладки на лету. */

int key_w, key_a, key_s, key_d;
int key_space, key_shift;
int key_up, key_down, key_left, key_right;

void handle_key_event(XEvent *e) {
    KeySym ks = XLookupKeysym(&e->xkey, 0);
    int down = (e->type == KeyPress);

    switch (ks) {
        case XK_w: case XK_Cyrillic_tse:  key_w     = down; break; /* W / Ц */
        case XK_a: case XK_Cyrillic_ef:   key_a     = down; break; /* A / Ф */
        case XK_s: case XK_Cyrillic_ze:   key_s     = down; break; /* S / Ы */
        case XK_d: case XK_Cyrillic_ve:   key_d     = down; break; /* D / В */
        case XK_space:                    key_space = down; break;
        case XK_Shift_L:
        case XK_Shift_R:                  key_shift = down; break;
        case XK_Up:                       key_up    = down; break;
        case XK_Down:                     key_down  = down; break;
        case XK_Left:                     key_left  = down; break;
        case XK_Right:                    key_right = down; break;
        default: break;
    }
}

/* ---------- Камера и физика ---------- */

float cam_x, cam_y, cam_z;
float cam_yaw, cam_pitch;

/* Предварительное объявление */
float get_height(int wx, int wz);

float sample_height(float x, float z) {
    float fx = x / SIZE;
    float fz = z / SIZE;

    int wx0 = (int)floorf(fx);
    int wz0 = (int)floorf(fz);
    float tx = fx - wx0;
    float tz = fz - wz0;

    float h00 = get_height(wx0,     wz0)     * SIZE + TERRAIN_OFFSET;
    float h10 = get_height(wx0 + 1, wz0)     * SIZE + TERRAIN_OFFSET;
    float h01 = get_height(wx0,     wz0 + 1) * SIZE + TERRAIN_OFFSET;
    float h11 = get_height(wx0 + 1, wz0 + 1) * SIZE + TERRAIN_OFFSET;

    float hx0 = h00 * (1.0f - tx) + h10 * tx;
    float hx1 = h01 * (1.0f - tx) + h11 * tx;
    return hx0 * (1.0f - tz) + hx1 * tz;
}

int check_collision_point(float px, float pz, float bottom_y) {
    float h = sample_height(px, pz);
    return (h - bottom_y > STEP_HEIGHT);
}

int check_collision_capsule(float cx, float cz, float feet_y) {
    const int num_points = 8;
    const float angle_step = 2.0f * 3.14159265f / num_points;

    float h_center = sample_height(cx, cz);

    /* Проверяем точки по окружности радиуса коллайдера */
    for (int i = 0; i < num_points; i++) {
        float angle = i * angle_step;
        float px = cx + COLLIDER_RADIUS * cosf(angle);
        float pz = cz + COLLIDER_RADIUS * sinf(angle);

        if (check_collision_point(px, pz, feet_y)) {
            return 1;
        }

        /* Проверка резкого перепада высот (стены/обрывы) относительно центра */
        float h_point = sample_height(px, pz);
        if (fabsf(h_point - h_center) > STEP_HEIGHT) {
            float max_h = fmaxf(h_point, h_center);
            if (feet_y < max_h - COLLISION_EPSILON) {
                return 1;
            }
        }
    }

    /* Проверка центральной точки */
    if (check_collision_point(cx, cz, feet_y)) {
        return 1;
    }

    return 0;
}

void update_camera(double dt) {
    float dt_f = (float)dt;

    /* Направление взгляда (горизонтальная проекция для движения) */
    float dir_x = sinf(cam_yaw) * cosf(cam_pitch);
    float dir_z = -cosf(cam_yaw) * cosf(cam_pitch);

    float right_x = -dir_z;
    float right_z =  dir_x;

    float move = MOVE_SPEED * dt_f;
    float look = LOOK_SPEED * dt_f;

    /* Горизонтальное перемещение */
    float dx = 0.0f, dz = 0.0f;

    if (key_w) { dx += dir_x * move; dz += dir_z * move; }
    if (key_s) { dx -= dir_x * move; dz -= dir_z * move; }
    if (key_d) { dx += right_x * move; dz += right_z * move; }
    if (key_a) { dx -= right_x * move; dz -= right_z * move; }

    if (key_shift) { dx *= 2.0f; dz *= 2.0f; }

    /* Ноги игрока находятся ниже камеры */
    float feet_y = cam_y - COLLIDER_HEIGHT * 0.5f;

    /* Двухпроходное движение: сначала X, потом Z */
    float new_x = cam_x + dx;
    if (!check_collision_capsule(new_x, cam_z, feet_y)) {
        cam_x = new_x;
    }

    float new_z = cam_z + dz;
    if (!check_collision_capsule(cam_x, new_z, feet_y)) {
        cam_z = new_z;
    }

    /* --- Вертикальная физика --- */

    /* Текущая высота земли под игроком и целевая высота камеры */
    float ground_h = sample_height(cam_x, cam_z);
    float target_y = ground_h + COLLIDER_HEIGHT * 0.5f;

    /* Стоим ли на земле? (до горизонтального движения) */
    int on_ground = (fabsf(cam_y - target_y) < COLLISION_EPSILON);

    /* Гравитация только если не на земле */
    if (!on_ground) {
        vel_y += GRAVITY * dt_f;
    } else {
        vel_y = 0.0f;
    }

    /* Обновление вертикальной позиции */
    float next_y = cam_y + vel_y * dt_f;

    /* Пересчёт земли после горизонтального движения */
    ground_h = sample_height(cam_x, cam_z);
    target_y = ground_h + COLLIDER_HEIGHT * 0.5f;

    if (vel_y <= 0.0f && next_y <= target_y) {
        /* Приземление */
        next_y = target_y;
        vel_y = 0.0f;

        /* Прыжок при приземлении, если держим пробел */
        if (key_space) {
            vel_y = JUMP_FORCE;
            next_y = cam_y + vel_y * dt_f;
        }
    } else if (on_ground && key_space) {
        /* Прыжок с земли */
        vel_y = JUMP_FORCE;
        next_y = cam_y + vel_y * dt_f;
    }

    cam_y = next_y;

    /* Вращение камеры */
    if (key_right) cam_yaw  += look;
    if (key_left)  cam_yaw  -= look;
    if (key_up)    cam_pitch += look;
    if (key_down)  cam_pitch -= look;

    if (cam_pitch >  1.5f) cam_pitch =  1.5f;
    if (cam_pitch < -1.5f) cam_pitch = -1.5f;
}

/* ---------- Время ---------- */

double get_time(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

/* ---------- Рендер мира ---------- */

void draw_world(void) {
    /* чанк камеры в координатах клеток (мировые единицы -> клетки) */
    int pcx = floor_div((int)floorf(cam_x / SIZE), CHUNK_SIZE);
    int pcz = floor_div((int)floorf(cam_z / SIZE), CHUNK_SIZE);

    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, texture_id);

    for (int i = 0; i < chunk_count; i++) {
        Chunk *c = &chunks[i];

        /* рисуем только чанки в радиусе видимости */
        if (c->cx < pcx - VIEW_RADIUS || c->cx > pcx + VIEW_RADIUS) continue;
        if (c->cz < pcz - VIEW_RADIUS || c->cz > pcz + VIEW_RADIUS) continue;

        int base_wx = c->cx * CHUNK_SIZE;
        int base_wz = c->cz * CHUNK_SIZE;

        for (int x = 0; x < CHUNK_SIZE; x++) {
            for (int z = 0; z < CHUNK_SIZE; z++) {
                int wx0 = base_wx + x;
                int wz0 = base_wz + z;

                float px0 = wx0 * SIZE;
                float pz0 = wz0 * SIZE;
                float px1 = (wx0 + 1) * SIZE;
                float pz1 = (wz0 + 1) * SIZE;

                /* та же формула, что в sample_height:
                 *                   h * SIZE + TERRAIN_OFFSET */
                float y00 = get_height(wx0,     wz0)     * SIZE + TERRAIN_OFFSET;
                float y10 = get_height(wx0 + 1, wz0)     * SIZE + TERRAIN_OFFSET;
                float y01 = get_height(wx0,     wz0 + 1) * SIZE + TERRAIN_OFFSET;
                float y11 = get_height(wx0 + 1, wz0 + 1) * SIZE + TERRAIN_OFFSET;

                glBegin(GL_TRIANGLES);

                glTexCoord2f(0, 0); glVertex3f(px0, y00, pz0);
                glTexCoord2f(1, 0); glVertex3f(px1, y10, pz0);
                glTexCoord2f(1, 1); glVertex3f(px1, y11, pz1);

                glTexCoord2f(0, 0); glVertex3f(px0, y00, pz0);
                glTexCoord2f(1, 1); glVertex3f(px1, y11, pz1);
                glTexCoord2f(0, 1); glVertex3f(px0, y01, pz1);

                glEnd();
            }
        }
    }

    glDisable(GL_TEXTURE_2D);
}

/* ---------- Текстуры ---------- */

int data_size(unsigned char format, int n) {
    switch (format) {
        case 0: return n * 4;
        case 1: return n * 4;
        case 2: return (n * 12 + 7) / 8;
        case 3: return n * 2;
    }
    return 0;
}

int get_pixel(const Image *img, int i) {
    int total = (img->x * img->y * 12 + 7) / 8;
    switch (img->format) {
        case 0: case 1: return ((int*)img->data)[i];
        case 2: {
            const uint8_t *p = (const uint8_t*)img->data;
            int bit = i * 12;
            int byte = bit / 8;
            int shift = bit % 8;
            /* не читать за конец буфера на последнем пикселе */
            uint32_t v = p[byte];
            if (byte + 1 < total) v |= (uint32_t)p[byte + 1] << 8;
            if (byte + 2 < total) v |= (uint32_t)p[byte + 2] << 16;
            return (v >> shift) & 0x0FFF;
        }
        case 3: return ((unsigned short*)img->data)[i];
    }
    return 0;
}

void unpack_rgb888(int px, uint8_t *r, uint8_t *g, uint8_t *b) {
    *r = (px >> 16) & 0xFF; *g = (px >> 8) & 0xFF; *b = px & 0xFF;
}
void unpack_argb888(int px, uint8_t *a, uint8_t *r, uint8_t *g, uint8_t *b) {
    *a = (px >> 24) & 0xFF; *r = (px >> 16) & 0xFF; *g = (px >> 8) & 0xFF; *b = px & 0xFF;
}
void unpack_rgb444(int px, uint8_t *r, uint8_t *g, uint8_t *b) {
    *r = ((px >> 8) & 0x0F) * 17; *g = ((px >> 4) & 0x0F) * 17; *b = (px & 0x0F) * 17;
}
void unpack_argb4444(int px, uint8_t *a, uint8_t *r, uint8_t *g, uint8_t *b) {
    *a = ((px >> 12) & 0x0F) * 17; *r = ((px >> 8) & 0x0F) * 17;
    *g = ((px >> 4) & 0x0F) * 17; *b = (px & 0x0F) * 17;
}

int raw_load(const char *filename, Image *img) {
    FILE *f = fopen(filename, "rb");
    if (!f) return 0;
    if (fread(&img->x, sizeof(short), 1, f) != 1) { fclose(f); return 0; }
    if (fread(&img->y, sizeof(short), 1, f) != 1) { fclose(f); return 0; }
    if (fread(&img->format, sizeof(unsigned char), 1, f) != 1) { fclose(f); return 0; }

    int n = img->x * img->y;
    int sz = data_size(img->format, n);
    img->data = malloc(sz);
    if (!img->data) { fclose(f); return 0; }

    if (fread(img->data, 1, sz, f) != (size_t)sz) {
        free(img->data); img->data = NULL; fclose(f); return 0;
    }
    fclose(f);
    return 1;
}

GLuint load_texture(const char *filename) {
    Image img = {0};
    if (!raw_load(filename, &img)) {
        fprintf(stderr, "Error: cannot load texture file '%s'\n", filename);
        return 0;
    }
    printf("Loaded texture: %dx%d, format %d\n", img.x, img.y, img.format);

    int n = img.x * img.y;
    int has_alpha = (img.format == 1 || img.format == 3);

    unsigned char *pixels = malloc(n * (has_alpha ? 4 : 3));
    if (!pixels) {
        free(img.data);
        return 0;
    }

    for (int i = 0; i < n; i++) {
        int px = get_pixel(&img, i);
        uint8_t r, g, b, a = 255;
        switch (img.format) {
            case 0: unpack_rgb888(px, &r, &g, &b); break;
            case 1: unpack_argb888(px, &a, &r, &g, &b); break;
            case 2: unpack_rgb444(px, &r, &g, &b); break;
            case 3: unpack_argb4444(px, &a, &r, &g, &b); break;
            default: r = g = b = 0; break;
        }
        if (has_alpha) {
            pixels[i*4+0]=r; pixels[i*4+1]=g; pixels[i*4+2]=b; pixels[i*4+3]=a;
        } else {
            pixels[i*3+0]=r; pixels[i*3+1]=g; pixels[i*3+2]=b;
        }
    }

    GLuint tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

    if (has_alpha)
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, img.x, img.y, 0,
                     GL_RGBA, GL_UNSIGNED_BYTE, pixels);
        else
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, img.x, img.y, 0,
                         GL_RGB, GL_UNSIGNED_BYTE, pixels);

            free(pixels);
        free(img.data);
    return tex;
}

/* ---------- Генерация чанков вокруг камеры ---------- */

void update_chunks(void) {
    /* мировые координаты камеры -> клетки -> чанки */
    int pcx = floor_div((int)floorf(cam_x / SIZE), CHUNK_SIZE);
    int pcz = floor_div((int)floorf(cam_z / SIZE), CHUNK_SIZE);

    int generated = 0;

    for (int dx = -VIEW_RADIUS; dx <= VIEW_RADIUS && generated < MAX_CHUNKS_PER_FRAME; dx++) {
        for (int dz = -VIEW_RADIUS; dz <= VIEW_RADIUS && generated < MAX_CHUNKS_PER_FRAME; dz++) {
            int cx = pcx + dx;
            int cz = pcz + dz;

            if (find_chunk(cx, cz)) continue;

            Chunk *c = get_free_chunk();
            if (!c) break;

            c->cx = cx;
            c->cz = cz;
            generate_chunk(c);
            generated++;
        }
    }
}

/* ---------- main ---------- */

int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <texture-file>\n", argv[0]);
        return 1;
    }

    XEvent event;
    int screen_num;

    display = XOpenDisplay(NULL);
    if (!display) { fprintf(stderr, "Cannot open display\n"); return 1; }

    screen_num = DefaultScreen(display);
    XVisualInfo *vi = glXChooseVisual(display, screen_num, att);
    if (!vi) {
        fprintf(stderr, "No suitable visual found\n");
        XCloseDisplay(display);
        return 1;
    }

    window = XCreateSimpleWindow(display, RootWindow(display, screen_num),
                                 50, 50, 800, 600, 1,
                                 BlackPixel(display, screen_num),
                                 WhitePixel(display, screen_num));

    XSelectInput(display, window,
                 ExposureMask | KeyPressMask | KeyReleaseMask |
                 StructureNotifyMask);

    XMapWindow(display, window);

    glc = glXCreateContext(display, vi, NULL, True);
    if (!glc) {
        fprintf(stderr, "Cannot create GLX context\n");
        XDestroyWindow(display, window);
        XFree(vi);
        XCloseDisplay(display);
        return 1;
    }
    XFree(vi);

    glXMakeCurrent(display, window, glc);

    glEnable(GL_DEPTH_TEST);
    glClearColor(SKY_COLOR[0], SKY_COLOR[1], SKY_COLOR[2], SKY_COLOR[3]); /* небо */

    /* ---------- Туман ----------
     * Фиксированный конвейер сам смешивает пиксели геометрии с цветом
     * тумана по расстоянию (в пространстве вида, по глубине).
     * Цвет тумана = цвет неба => горизонт бесшовный,
     * а дальние края чанков плавно растворяются. */
    glEnable(GL_FOG);
    glFogi(GL_FOG_MODE, GL_LINEAR);      /* либо GL_EXP / GL_EXP2 (тогда нужен GL_FOG_DENSITY) */
    glFogf(GL_FOG_START, 12.0f);         /* до этой дистанции тумана нет */
    glFogf(GL_FOG_END,   20.0f);         /* дальше — полностью туман */
    glFogfv(GL_FOG_COLOR, SKY_COLOR);
    glHint(GL_FOG_HINT, GL_NICEST);      /* расчёт на каждый фрагмент, если драйвер позволяет */

    key_w = key_a = key_s = key_d = 0;
    key_space = key_shift = 0;
    key_up = key_down = key_left = key_right = 0;

    texture_id = load_texture(argv[1]);
    if (texture_id == 0) {
        fprintf(stderr, "Failed to load texture: %s\n", argv[1]);
        glXMakeCurrent(display, None, NULL);
        glXDestroyContext(display, glc);
        XDestroyWindow(display, window);
        XCloseDisplay(display);
        return 1;
    }

    cam_x = 0.0f;
    cam_y = 20.0f;
    cam_z = 0.0f;
    cam_yaw = 0.0f;
    cam_pitch = -0.5f;

    double last_time = get_time();
    double accumulator = 0.0;

    while (1) {
        double frame_start = get_time();

        while (XPending(display) > 0) {
            XNextEvent(display, &event);

            switch (event.type) {
                case KeyPress:
                case KeyRelease:
                    handle_key_event(&event);
                    if (event.type == KeyPress &&
                        XLookupKeysym(&event.xkey, 0) == XK_Escape) {
                        glXMakeCurrent(display, None, NULL);
                    glXDestroyContext(display, glc);
                    XDestroyWindow(display, window);
                    XCloseDisplay(display);
                    return 0;
                        }
                        break;

                case Expose:
                case ConfigureNotify:
                    break;
            }
        }

        double current_time = get_time();
        double delta = current_time - last_time;
        last_time = current_time;

        if (delta > 0.2) delta = 0.2;
        accumulator += delta;

        while (accumulator >= FRAME_DURATION) {
            update_camera(FRAME_DURATION);
            accumulator -= FRAME_DURATION;
        }

        update_chunks();

        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        glMatrixMode(GL_PROJECTION);
        glLoadIdentity();
        XGetWindowAttributes(display, window, &gwa);
        float aspect = (gwa.height > 0) ? (float)gwa.width / (float)gwa.height : 1.0f;
        gluPerspective(60.0, aspect, 0.1, 300.0);

        glMatrixMode(GL_MODELVIEW);
        glLoadIdentity();

        float dir_x =  sinf(cam_yaw) * cosf(cam_pitch);
        float dir_y =  sinf(cam_pitch);
        float dir_z = -cosf(cam_yaw) * cosf(cam_pitch);

        gluLookAt(cam_x, cam_y, cam_z,
                  cam_x + dir_x, cam_y + dir_y, cam_z + dir_z,
                  0.0f, 1.0f, 0.0f);

        draw_world();

        glXSwapBuffers(display, window);

        /* удержание ~60 FPS даже без вертикальной синхронизации */
        double elapsed = get_time() - frame_start;
        if (elapsed < FRAME_DURATION)
            usleep((useconds_t)((FRAME_DURATION - elapsed) * 1e6));
    }

    return 0;
}
