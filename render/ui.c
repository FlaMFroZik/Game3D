#include <stdio.h>
#include <string.h>
#include <GL/gl.h>

#include "render/ui.h"

#define UI_BASE_HEIGHT 720.0f
#define UI_SCALE_MAX   3.0f
#define UI_TEXT_FACTOR 0.55f

/* ---------- Цвета Valve Hammer (Source / GoldSrc UI) ---------- */
const float UI_CLR_BG[4]              = { 0.20f, 0.21f, 0.23f, 1.00f }; /* #33363a */
const float UI_CLR_PANEL[4]           = { 0.24f, 0.25f, 0.27f, 1.00f }; /* #3d4045 */
const float UI_CLR_PANEL_LIGHT[4]     = { 0.38f, 0.39f, 0.42f, 1.00f }; /* #61646b - верхний/левый bevel */
const float UI_CLR_PANEL_DARK[4]      = { 0.12f, 0.13f, 0.14f, 1.00f }; /* #1f2124 - нижний/правый bevel */
const float UI_CLR_SUNKEN[4]          = { 0.13f, 0.14f, 0.15f, 1.00f }; /* #212426 */
const float UI_CLR_SUNKEN_BORDER[4]   = { 0.08f, 0.08f, 0.09f, 1.00f };
const float UI_CLR_BTN[4]             = { 0.27f, 0.28f, 0.31f, 1.00f };
const float UI_CLR_BTN_HOT[4]         = { 0.34f, 0.36f, 0.40f, 1.00f };
const float UI_CLR_BTN_ACTIVE[4]      = { 0.16f, 0.18f, 0.21f, 1.00f };
const float UI_CLR_BTN_BORDER_HI[4]   = { 0.45f, 0.47f, 0.52f, 1.00f };
const float UI_CLR_BTN_BORDER_LO[4]   = { 0.10f, 0.10f, 0.11f, 1.00f };
const float UI_CLR_ACCENT[4]          = { 0.90f, 0.55f, 0.12f, 1.00f }; /* Source оранжевый */
const float UI_CLR_ACCENT_HOVER[4]    = { 1.00f, 0.65f, 0.20f, 1.00f };
const float UI_CLR_TEXT[4]            = { 0.92f, 0.94f, 0.96f, 1.00f };
const float UI_CLR_TEXT_DIM[4]        = { 0.62f, 0.65f, 0.70f, 1.00f };
const float UI_CLR_TEXT_ACCENT[4]     = { 0.98f, 0.70f, 0.20f, 1.00f };
const float UI_CLR_TEXT_DISABLED[4]   = { 0.40f, 0.42f, 0.45f, 1.00f };
const float UI_CLR_SELECTION[4]       = { 1.00f, 0.20f, 0.20f, 1.00f }; /* Ярко-красный контур выбора */
const float UI_CLR_VIEWPORT_BORDER[4] = { 0.16f, 0.17f, 0.18f, 1.00f };
const float UI_CLR_VIEWPORT_ACTIVE[4] = { 0.90f, 0.55f, 0.12f, 1.00f };
const float UI_CLR_GRID_GREEN[4]      = { 0.08f, 0.18f, 0.11f, 1.00f };
const float UI_CLR_GRID_MAJOR[4]      = { 0.15f, 0.32f, 0.20f, 1.00f };
const float UI_CLR_AXIS_X[4]          = { 0.75f, 0.20f, 0.20f, 1.00f };
const float UI_CLR_AXIS_Y[4]          = { 0.20f, 0.65f, 0.25f, 1.00f };
const float UI_CLR_AXIS_Z[4]          = { 0.20f, 0.45f, 0.80f, 1.00f };

/* ---------- Кадр интерфейса ---------- */

void ui_frame_begin(Ui *ui, const Font *font, int width, int height) {
    ui->font = font;
    ui->width = (width > 0) ? width : 1;
    ui->height = (height > 0) ? height : 1;

    ui->scale = (float)ui->height / UI_BASE_HEIGHT;
    if (ui->scale < 1.0f) ui->scale = 1.0f;
    if (ui->scale > UI_SCALE_MAX) ui->scale = UI_SCALE_MAX;

    glViewport(0, 0, ui->width, ui->height);
    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glLoadIdentity();
    glOrtho(0.0, (GLdouble)ui->width, (GLdouble)ui->height, 0.0, -100.0, 100.0);
    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glLoadIdentity();

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_FOG);
    glDisable(GL_TEXTURE_2D);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}

void ui_frame_end(void) {
    glDisable(GL_BLEND);
    glDisable(GL_TEXTURE_2D);
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
    if (thickness <= 0.0f || r.w <= 0.0f || r.h <= 0.0f) return;
    ui_fill(ui_rect(r.x, r.y, r.w, thickness), rgba);
    ui_fill(ui_rect(r.x, r.y + r.h - thickness, r.w, thickness), rgba);
    ui_fill(ui_rect(r.x, r.y, thickness, r.h), rgba);
    ui_fill(ui_rect(r.x + r.w - thickness, r.y, thickness, r.h), rgba);
}

void ui_line(float x0, float y0, float x1, float y1, float thickness, const float rgba[4]) {
    glDisable(GL_TEXTURE_2D);
    glLineWidth(thickness);
    glColor4fv(rgba);
    glBegin(GL_LINES);
    glVertex2f(x0, y0);
    glVertex2f(x1, y1);
    glEnd();
    glLineWidth(1.0f);
}

void ui_rect_outline(UiRect r, float thickness, const float rgba[4]) {
    ui_border(r, thickness, rgba);
}

void ui_draw_handle(float cx, float cy, float size, const float fill[4], const float border[4]) {
    const float half = size * 0.5f;
    UiRect r = ui_rect(cx - half, cy - half, size, size);
    ui_fill(r, fill);
    ui_border(r, 1.0f, border);
}

/* ---------- Текст ---------- */

static float ui_font_scale(const Ui *ui, float scale) {
    return scale * UI_TEXT_FACTOR * ui->scale;
}

void ui_label(const Ui *ui, float x, float y, float scale,
              const float rgba[4], const char *text) {
    if (!text || !ui || !ui->font) return;
    font_draw(ui->font, x, y, ui_font_scale(ui, scale), rgba, text);
}

void ui_label_in(const Ui *ui, UiRect r, float scale,
                 const float rgba[4], const char *text) {
    if (!text || !ui || !ui->font) return;
    const float w = ui_text_width(ui, text, scale);
    const float h = ui_text_height(ui, scale);
    ui_label(ui, r.x + (r.w - w) * 0.5f, r.y + (r.h - h) * 0.5f,
             scale, rgba, text);
}

void ui_label_left(const Ui *ui, UiRect r, float offset_x, float scale,
                   const float rgba[4], const char *text) {
    if (!text || !ui || !ui->font) return;
    const float h = ui_text_height(ui, scale);
    ui_label(ui, r.x + offset_x, r.y + (r.h - h) * 0.5f, scale, rgba, text);
}

void ui_label_right(const Ui *ui, UiRect r, float offset_x, float scale,
                    const float rgba[4], const char *text) {
    if (!text || !ui || !ui->font) return;
    const float w = ui_text_width(ui, text, scale);
    const float h = ui_text_height(ui, scale);
    ui_label(ui, r.x + r.w - w - offset_x, r.y + (r.h - h) * 0.5f, scale, rgba, text);
}

float ui_text_width(const Ui *ui, const char *text, float scale) {
    if (!ui || !ui->font || !text) return 0.0f;
    return font_text_width(ui->font, text, ui_font_scale(ui, scale));
}

float ui_text_height(const Ui *ui, float scale) {
    if (!ui || !ui->font) return 14.0f * scale;
    return font_line_height(ui->font, ui_font_scale(ui, scale));
}

/* ---------- 3D Bevel панели в стиле Hammer ---------- */

void ui_panel_raised(UiRect r) {
    ui_fill(r, UI_CLR_PANEL);
    /* Верхнее и левое ребро светлое */
    ui_fill(ui_rect(r.x, r.y, r.w, 1.0f), UI_CLR_PANEL_LIGHT);
    ui_fill(ui_rect(r.x, r.y, 1.0f, r.h), UI_CLR_PANEL_LIGHT);
    /* Нижнее и правое ребро темное */
    ui_fill(ui_rect(r.x, r.y + r.h - 1.0f, r.w, 1.0f), UI_CLR_PANEL_DARK);
    ui_fill(ui_rect(r.x + r.w - 1.0f, r.y, 1.0f, r.h), UI_CLR_PANEL_DARK);
}

void ui_panel_sunken(UiRect r, const float *bg_rgba) {
    ui_fill(r, bg_rgba ? bg_rgba : UI_CLR_SUNKEN);
    /* Верхнее и левое ребро темное (вдавленное) */
    ui_fill(ui_rect(r.x, r.y, r.w, 1.0f), UI_CLR_PANEL_DARK);
    ui_fill(ui_rect(r.x, r.y, 1.0f, r.h), UI_CLR_PANEL_DARK);
    /* Нижнее и правое ребро светлое */
    ui_fill(ui_rect(r.x, r.y + r.h - 1.0f, r.w, 1.0f), UI_CLR_PANEL_LIGHT);
    ui_fill(ui_rect(r.x + r.w - 1.0f, r.y, 1.0f, r.h), UI_CLR_PANEL_LIGHT);
}

void ui_panel_flat(UiRect r, const float *bg_rgba, const float *border_rgba) {
    ui_fill(r, bg_rgba ? bg_rgba : UI_CLR_PANEL);
    if (border_rgba) {
        ui_border(r, 1.0f, border_rgba);
    }
}

void ui_panel_header(const Ui *ui, UiRect r, const char *title, int active) {
    const float bg[4] = { 0.18f, 0.20f, 0.22f, 1.0f };
    ui_fill(r, bg);
    ui_border(r, 1.0f, active ? UI_CLR_ACCENT : UI_CLR_PANEL_DARK);
    ui_label_left(ui, r, 6.0f * ui->scale, 0.85f, active ? UI_CLR_TEXT_ACCENT : UI_CLR_TEXT, title);
}

/* Заголовок видового окна с кнопкой развертывания */
int ui_viewport_tab(Ui *ui, UiRect r, const char *title, int is_active, int is_maximized) {
    const float h = 20.0f * ui->scale;
    UiRect bar = ui_rect(r.x, r.y, r.w, h);
    int hot = ui_contains(ui, bar);

    const float bg[4] = { 0.15f, 0.16f, 0.18f, 0.95f };
    ui_fill(bar, bg);
    ui_border(bar, 1.0f, is_active ? UI_CLR_ACCENT : UI_CLR_PANEL_DARK);

    /* Заголовок видового окна */
    ui_label_left(ui, bar, 6.0f * ui->scale, 0.80f,
                  is_active ? UI_CLR_TEXT_ACCENT : UI_CLR_TEXT, title);

    /* Кнопка разворачивания/сворачивания справа */
    const float btn_w = 16.0f * ui->scale;
    UiRect btn = ui_rect(bar.x + bar.w - btn_w - 2.0f, bar.y + 2.0f, btn_w, h - 4.0f);
    int btn_hot = ui_contains(ui, btn);

    ui_panel_raised(btn);
    ui_label_in(ui, btn, 0.70f, btn_hot ? UI_CLR_TEXT_ACCENT : UI_CLR_TEXT_DIM,
                is_maximized ? "[-]" : "[+]");

    if (btn_hot && ui->clicked) {
        return 2; /* клик по максимизации */
    }
    if (hot && ui->clicked) {
        return 1; /* клик по заголовку (фокус) */
    }
    return 0;
}

/* Кнопка на панели инструментов (стиль Hammer) */
int ui_button_tool(Ui *ui, UiRect r, const char *icon, const char *shortcut, int active) {
    int hot = ui_contains(ui, r);

    if (active) {
        ui_panel_sunken(r, UI_CLR_BTN_ACTIVE);
        ui_border(r, 1.0f, UI_CLR_ACCENT);
    } else if (hot) {
        ui_fill(r, UI_CLR_BTN_HOT);
        ui_fill(ui_rect(r.x, r.y, r.w, 1.0f), UI_CLR_BTN_BORDER_HI);
        ui_fill(ui_rect(r.x, r.y, 1.0f, r.h), UI_CLR_BTN_BORDER_HI);
        ui_fill(ui_rect(r.x, r.y + r.h - 1.0f, r.w, 1.0f), UI_CLR_BTN_BORDER_LO);
        ui_fill(ui_rect(r.x + r.w - 1.0f, r.y, 1.0f, r.h), UI_CLR_BTN_BORDER_LO);
    } else {
        ui_panel_raised(r);
    }

    /* Иконка / Текст инструмента */
    const float *text_clr = active ? UI_CLR_TEXT_ACCENT : (hot ? UI_CLR_TEXT : UI_CLR_TEXT_DIM);
    ui_label_in(ui, r, 0.82f, text_clr, icon);

    /* Если курсор над кнопкой, можно показать хинт */
    if (hot && shortcut && shortcut[0] != '\0') {
        /* хинт виден на панели статуса */
    }

    return hot && ui->clicked;
}

/* Стандартная кнопка Hammer */
int ui_button_hammer(Ui *ui, UiRect r, const char *label, int active) {
    int hot = ui_contains(ui, r);

    if (active) {
        ui_panel_sunken(r, UI_CLR_BTN_ACTIVE);
        ui_border(r, 1.0f, UI_CLR_ACCENT);
    } else if (hot) {
        ui_fill(r, UI_CLR_BTN_HOT);
        ui_fill(ui_rect(r.x, r.y, r.w, 1.0f), UI_CLR_BTN_BORDER_HI);
        ui_fill(ui_rect(r.x, r.y, 1.0f, r.h), UI_CLR_BTN_BORDER_HI);
        ui_fill(ui_rect(r.x, r.y + r.h - 1.0f, r.w, 1.0f), UI_CLR_BTN_BORDER_LO);
        ui_fill(ui_rect(r.x + r.w - 1.0f, r.y, 1.0f, r.h), UI_CLR_BTN_BORDER_LO);
    } else {
        ui_panel_raised(r);
    }

    const float *clr = active ? UI_CLR_TEXT_ACCENT : (hot ? UI_CLR_TEXT : UI_CLR_TEXT_DIM);
    ui_label_in(ui, r, 0.85f, clr, label);

    return hot && ui->clicked;
}

int ui_button_hammer_small(Ui *ui, UiRect r, const char *label) {
    return ui_button_hammer(ui, r, label, 0);
}

/* Пункт выпадающего меню */
int ui_menu_item(Ui *ui, UiRect r, const char *label, const char *shortcut, int enabled) {
    int hot = ui_contains(ui, r) && enabled;

    if (hot) {
        ui_fill(r, UI_CLR_ACCENT);
    } else {
        ui_fill(r, UI_CLR_PANEL);
    }

    const float *text_clr = !enabled ? UI_CLR_TEXT_DISABLED : (hot ? UI_CLR_TEXT : UI_CLR_TEXT);
    ui_label_left(ui, r, 12.0f * ui->scale, 0.82f, text_clr, label);

    if (shortcut && shortcut[0] != '\0') {
        const float *sc_clr = !enabled ? UI_CLR_TEXT_DISABLED : (hot ? UI_CLR_TEXT : UI_CLR_TEXT_DIM);
        ui_label_right(ui, r, 12.0f * ui->scale, 0.78f, sc_clr, shortcut);
    }

    return hot && ui->clicked;
}

/* Ячейка статус-бара */
void ui_status_field(const Ui *ui, UiRect r, const char *text, const float *text_rgba) {
    ui_panel_sunken(r, UI_CLR_SUNKEN);
    if (text) {
        ui_label_left(ui, r, 6.0f * ui->scale, 0.78f,
                      text_rgba ? text_rgba : UI_CLR_TEXT, text);
    }
}

/* Предпросмотр текстуры */
int ui_texture_preview(Ui *ui, UiRect r, const Texture *tex, const char *name, int selected) {
    int hot = ui_contains(ui, r);

    ui_panel_sunken(r, UI_CLR_SUNKEN);
    if (selected) {
        ui_border(r, 2.0f, UI_CLR_ACCENT);
    } else if (hot) {
        ui_border(r, 1.0f, UI_CLR_BTN_BORDER_HI);
    }

    /* Область под картинку */
    const float pad = 3.0f * ui->scale;
    const float label_h = 16.0f * ui->scale;
    UiRect img_r = ui_rect(r.x + pad, r.y + pad, r.w - pad * 2.0f, r.h - pad * 2.0f - label_h);

    if (tex && tex->id != 0 && img_r.w > 0 && img_r.h > 0) {
        glEnable(GL_TEXTURE_2D);
        glBindTexture(GL_TEXTURE_2D, tex->id);
        glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
        glBegin(GL_QUADS);
        glTexCoord2f(0.0f, 0.0f); glVertex2f(img_r.x,            img_r.y);
        glTexCoord2f(1.0f, 0.0f); glVertex2f(img_r.x + img_r.w,  img_r.y);
        glTexCoord2f(1.0f, 1.0f); glVertex2f(img_r.x + img_r.w,  img_r.y + img_r.h);
        glTexCoord2f(0.0f, 1.0f); glVertex2f(img_r.x,            img_r.y + img_r.h);
        glEnd();
        glDisable(GL_TEXTURE_2D);
    } else {
        /* Заглушка, если текстуры нет */
        const float gray[4] = { 0.2f, 0.2f, 0.2f, 1.0f };
        ui_fill(img_r, gray);
    }

    /* Подпись текстуры снизу */
    if (name) {
        const char *short_name = strrchr(name, '/');
        short_name = short_name ? short_name + 1 : name;
        UiRect txt_r = ui_rect(r.x + pad, r.y + r.h - label_h - pad, r.w - pad * 2.0f, label_h);
        ui_label_in(ui, txt_r, 0.70f, selected ? UI_CLR_TEXT_ACCENT : UI_CLR_TEXT_DIM, short_name);
    }

    return hot && ui->clicked;
}

/* Модальное диалоговое окно в стиле Valve Hammer / Win32 */
void ui_dialog_frame(const Ui *ui, UiRect r, const char *title, int *close_clicked) {
    /* Фоновая тень под окном */
    const float shadow_offset = 6.0f * ui->scale;
    const float shadow_clr[4] = { 0.0f, 0.0f, 0.0f, 0.5f };
    ui_fill(ui_rect(r.x + shadow_offset, r.y + shadow_offset, r.w, r.h), shadow_clr);

    /* Тело диалога */
    ui_panel_raised(r);
    ui_border(r, 2.0f, UI_CLR_PANEL_LIGHT);

    /* Заголовок окна */
    const float title_h = 24.0f * ui->scale;
    UiRect title_bar = ui_rect(r.x + 3.0f, r.y + 3.0f, r.w - 6.0f, title_h);
    const float title_bg[4] = { 0.18f, 0.32f, 0.52f, 1.00f }; /* классический синий заголовок Win/Hammer */
    ui_fill(title_bar, title_bg);
    ui_label_left(ui, title_bar, 8.0f * ui->scale, 0.85f, UI_CLR_TEXT, title);

    /* Кнопка закрытия [X] */
    const float btn_size = title_h - 4.0f;
    UiRect close_r = ui_rect(title_bar.x + title_bar.w - btn_size - 2.0f,
                             title_bar.y + 2.0f, btn_size, btn_size);
    int close_hot = ui_contains(ui, close_r);
    const float close_bg[4] = { 0.75f, 0.20f, 0.20f, 1.0f };
    ui_fill(close_r, close_hot ? close_bg : UI_CLR_PANEL);
    ui_panel_raised(close_r);
    ui_label_in(ui, close_r, 0.75f, UI_CLR_TEXT, "X");

    if (close_clicked && close_hot && ui->clicked) {
        *close_clicked = 1;
    }
}

/* Числовой степпер (значение с кнопками [-] и [+]) */
int ui_stepper_float(Ui *ui, UiRect r, const char *label, float *value,
                     float step, float min_v, float max_v, const char *fmt) {
    const float label_w = 60.0f * ui->scale;
    const float btn_w = 20.0f * ui->scale;
    const float val_w = r.w - label_w - btn_w * 2.0f - 4.0f * ui->scale;

    ui_label_left(ui, ui_rect(r.x, r.y, label_w, r.h), 2.0f, 0.80f, UI_CLR_TEXT, label);

    UiRect minus_r = ui_rect(r.x + label_w, r.y, btn_w, r.h);
    UiRect val_r   = ui_rect(r.x + label_w + btn_w + 2.0f * ui->scale, r.y, val_w, r.h);
    UiRect plus_r  = ui_rect(r.x + label_w + btn_w + val_w + 4.0f * ui->scale, r.y, btn_w, r.h);

    int changed = 0;
    if (ui_button_hammer_small(ui, minus_r, "-")) {
        *value -= step;
        if (*value < min_v) *value = min_v;
        changed = 1;
    }

    ui_panel_sunken(val_r, UI_CLR_SUNKEN);
    char buf[64];
    snprintf(buf, sizeof(buf), fmt ? fmt : "%g", (double)*value);
    ui_label_in(ui, val_r, 0.80f, UI_CLR_TEXT, buf);

    if (ui_button_hammer_small(ui, plus_r, "+")) {
        *value += step;
        if (*value > max_v) *value = max_v;
        changed = 1;
    }

    return changed;
}
