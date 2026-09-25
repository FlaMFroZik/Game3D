#include <GL/gl.h>

#include "render/ui.h"

/* Интерфейс рассчитан на окно высотой 600 px; на большем окне всё
 * растёт вместе с ним, чтобы кнопки не становились мелкими. */
#define UI_BASE_HEIGHT 600.0f
#define UI_SCALE_MAX   4.0f

/* Атлас запечён под FONT_PIXEL_SIZE пикселей; этот множитель превращает
 * его в обычный размер текста меню (~19 px при высоте окна 600). */
#define UI_TEXT_FACTOR 0.6f

/* Палитра меню: тёмные панели, светлый текст, красная кнопка выхода. */
const float UI_COLOR_PANEL[4]       = { 0.05f, 0.06f, 0.08f, 0.88f };
const float UI_COLOR_PANEL_EDGE[4]  = { 0.80f, 0.85f, 0.92f, 0.25f };
const float UI_COLOR_BUTTON[4]      = { 0.15f, 0.18f, 0.23f, 0.95f };
const float UI_COLOR_BUTTON_HOT[4]  = { 0.26f, 0.31f, 0.39f, 1.00f };
const float UI_COLOR_BUTTON_EDGE[4] = { 0.70f, 0.76f, 0.84f, 0.35f };
const float UI_COLOR_DANGER[4]      = { 0.42f, 0.13f, 0.12f, 0.95f };
const float UI_COLOR_DANGER_HOT[4]  = { 0.62f, 0.20f, 0.18f, 1.00f };
const float UI_COLOR_TEXT[4]        = { 0.92f, 0.94f, 0.97f, 1.00f };
const float UI_COLOR_TEXT_DIM[4]    = { 0.60f, 0.65f, 0.72f, 1.00f };

/* ---------- Кадр ---------- */

void ui_frame_begin(Ui *ui, const Font *font, int width, int height) {
    ui->font = font;
    ui->width = (width > 0) ? width : 1;
    ui->height = (height > 0) ? height : 1;

    ui->scale = (float)ui->height / UI_BASE_HEIGHT;
    if (ui->scale < 1.0f) ui->scale = 1.0f;
    if (ui->scale > UI_SCALE_MAX) ui->scale = UI_SCALE_MAX;

    /* Проекция в пикселях окна, Y вниз — те же координаты, что у курсора. */
    glViewport(0, 0, ui->width, ui->height);
    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glLoadIdentity();
    glOrtho(0.0, (GLdouble)ui->width, (GLdouble)ui->height, 0.0, -1.0, 1.0);
    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glLoadIdentity();

    /* Меню плоское: глубина и туман мира ему только мешают. */
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_FOG);
    glDisable(GL_TEXTURE_2D);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
}

void ui_frame_end(void) {
    glDisable(GL_BLEND);
    glDisable(GL_TEXTURE_2D);
    /* То, что включил render_setup_gl и что ожидает следующий кадр мира. */
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_FOG);

    glMatrixMode(GL_MODELVIEW);
    glPopMatrix();
    glMatrixMode(GL_PROJECTION);
    glPopMatrix();
    glMatrixMode(GL_MODELVIEW);
}

/* ---------- Геометрия ---------- */

UiRect ui_rect(float x, float y, float w, float h) {
    UiRect r;
    r.x = x; r.y = y; r.w = w; r.h = h;
    return r;
}

UiRect ui_centered(const Ui *ui, float w, float h) {
    return ui_rect(((float)ui->width - w) * 0.5f,
                   ((float)ui->height - h) * 0.5f, w, h);
}

UiRect ui_inset(UiRect parent, float inset) {
    UiRect r;
    r.x = parent.x + inset;
    r.y = parent.y + inset;
    r.w = parent.w - 2.0f * inset;
    r.h = parent.h - 2.0f * inset;
    if (r.w < 0.0f) r.w = 0.0f;
    if (r.h < 0.0f) r.h = 0.0f;
    return r;
}

int ui_contains(const Ui *ui, UiRect r) {
    const float px = (float)ui->pointer_x;
    const float py = (float)ui->pointer_y;
    return px >= r.x && px < r.x + r.w && py >= r.y && py < r.y + r.h;
}

/* ---------- Рисование ---------- */

void ui_fill(UiRect r, const float rgba[4]) {
    if (r.w <= 0.0f || r.h <= 0.0f) return;
    glDisable(GL_TEXTURE_2D);
    glColor4fv(rgba);
    glBegin(GL_QUADS);
    glVertex2f(r.x,        r.y);
    glVertex2f(r.x + r.w,  r.y);
    glVertex2f(r.x + r.w,  r.y + r.h);
    glVertex2f(r.x,        r.y + r.h);
    glEnd();
}

void ui_border(UiRect r, float thickness, const float rgba[4]) {
    if (thickness <= 0.0f) return;
    ui_fill(ui_rect(r.x, r.y, r.w, thickness), rgba);
    ui_fill(ui_rect(r.x, r.y + r.h - thickness, r.w, thickness), rgba);
    ui_fill(ui_rect(r.x, r.y, thickness, r.h), rgba);
    ui_fill(ui_rect(r.x + r.w - thickness, r.y, thickness, r.h), rgba);
}

static float ui_font_scale(const Ui *ui, float scale) {
    return scale * UI_TEXT_FACTOR * ui->scale;
}

void ui_label(const Ui *ui, float x, float y, float scale,
              const float rgba[4], const char *text) {
    if (!text) return;
    font_draw(ui->font, x, y, ui_font_scale(ui, scale), rgba, text);
}

void ui_label_in(const Ui *ui, UiRect r, float scale,
                 const float rgba[4], const char *text) {
    if (!text) return;
    const float w = ui_text_width(ui, text, scale);
    const float h = ui_text_height(ui, scale);
    ui_label(ui, r.x + (r.w - w) * 0.5f, r.y + (r.h - h) * 0.5f,
             scale, rgba, text);
}

float ui_text_width(const Ui *ui, const char *text, float scale) {
    return font_text_width(ui->font, text, ui_font_scale(ui, scale));
}

float ui_text_height(const Ui *ui, float scale) {
    return font_line_height(ui->font, ui_font_scale(ui, scale));
}

/* ---------- Кнопки ---------- */

int ui_button(Ui *ui, UiRect r, const char *label, UiButtonStyle style) {
    const int hot = ui_contains(ui, r);
    const float thickness = (ui->scale > 1.5f) ? 2.0f : 1.0f;

    if (style == UI_BUTTON_DANGER) {
        ui_fill(r, hot ? UI_COLOR_DANGER_HOT : UI_COLOR_DANGER);
    } else {
        ui_fill(r, hot ? UI_COLOR_BUTTON_HOT : UI_COLOR_BUTTON);
    }
    ui_border(r, thickness, UI_COLOR_BUTTON_EDGE);
    ui_label_in(ui, r, 1.0f, UI_COLOR_TEXT, label);

    return hot && ui->clicked;
}
