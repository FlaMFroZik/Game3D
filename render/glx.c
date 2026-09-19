#include <stdio.h>
#include <string.h>

#include <X11/keysym.h>

#include "render/glx.h"

static int att[] = { GLX_RGBA, GLX_DEPTH_SIZE, 24, GLX_DOUBLEBUFFER, None };

static char key_state[GLX_KEY_COUNT];
static char button_state[8];

static int quit_requested = 0;

/* ---------- Клавиши ---------- */

static int key_slot(KeySym ks) {
    switch (ks) {
        case XK_w: case XK_Cyrillic_tse: return GLX_KEY_W;  /* W / Ц */
        case XK_a: case XK_Cyrillic_ef:  return GLX_KEY_A;  /* A / Ф */
        case XK_s: case XK_Cyrillic_yeru: return GLX_KEY_S; /* S / Ы */
        case XK_d: case XK_Cyrillic_ve:  return GLX_KEY_D;  /* D / В */
        case XK_space:                   return GLX_KEY_SPACE;
        case XK_Shift_L: case XK_Shift_R: return GLX_KEY_SHIFT;
        case XK_Up:                      return GLX_KEY_UP;
        case XK_Down:                    return GLX_KEY_DOWN;
        case XK_Left:                    return GLX_KEY_LEFT;
        case XK_Right:                   return GLX_KEY_RIGHT;
        default: return -1;
    }
}

static void handle_key_event(const XEvent *e) {
    int slot = key_slot(XLookupKeysym((XKeyEvent *)&e->xkey, 0));
    if (slot >= 0) key_state[slot] = (e->type == KeyPress);
}

static void handle_button_event(const XEvent *e) {
    if (e->xbutton.button < sizeof(button_state)) {
        button_state[e->xbutton.button] = (e->type == ButtonPress);
    }
}

int glx_key_down(GlxKey key) {
    if (key < 0 || key >= GLX_KEY_COUNT) return 0;
    return key_state[key];
}

int glx_button_down(int button) {
    if (button < 0 || button >= (int)sizeof(button_state)) return 0;
    return button_state[button];
}

/* ---------- Окно ---------- */

int glx_init(GlxWindow *w, int width, int height, const char *title) {
    memset(w, 0, sizeof(*w));
    memset(key_state, 0, sizeof(key_state));
    memset(button_state, 0, sizeof(button_state));
    quit_requested = 0;

    w->display = XOpenDisplay(NULL);
    if (!w->display) {
        fprintf(stderr, "Cannot open display\n");
        return 0;
    }

    int screen_num = DefaultScreen(w->display);
    XVisualInfo *vi = glXChooseVisual(w->display, screen_num, att);
    if (!vi) {
        fprintf(stderr, "No suitable visual found\n");
        glx_shutdown(w);
        return 0;
    }

    w->window = XCreateSimpleWindow(w->display, RootWindow(w->display, screen_num),
                                    50, 50, (unsigned)width, (unsigned)height, 1,
                                    BlackPixel(w->display, screen_num),
                                    WhitePixel(w->display, screen_num));

    XStoreName(w->display, w->window, title);
    XSelectInput(w->display, w->window,
                 ExposureMask | KeyPressMask | KeyReleaseMask |
                 ButtonPressMask | ButtonReleaseMask | StructureNotifyMask);

    XMapWindow(w->display, w->window);

    w->context = glXCreateContext(w->display, vi, NULL, True);
    XFree(vi);

    if (!w->context) {
        fprintf(stderr, "Cannot create GLX context\n");
        glx_shutdown(w);
        return 0;
    }

    glXMakeCurrent(w->display, w->window, w->context);
    return 1;
}

void glx_shutdown(GlxWindow *w) {
    if (!w->display) return;

    if (w->context) {
        glXMakeCurrent(w->display, None, NULL);
        glXDestroyContext(w->display, w->context);
        w->context = NULL;
    }
    if (w->window) {
        XDestroyWindow(w->display, w->window);
        w->window = 0;
    }
    XCloseDisplay(w->display);
    w->display = NULL;
}

int glx_poll(GlxWindow *w) {
    XEvent event;

    while (XPending(w->display) > 0) {
        XNextEvent(w->display, &event);

        switch (event.type) {
            case KeyPress:
            case KeyRelease:
                handle_key_event(&event);
                if (event.type == KeyPress &&
                    XLookupKeysym(&event.xkey, 0) == XK_Escape) {
                    quit_requested = 1;
                }
                break;

            case ButtonPress:
            case ButtonRelease:
                handle_button_event(&event);
                break;

            case Expose:
            case ConfigureNotify:
                break;  /* кадр и так рисуется каждый тик */

            default:
                break;
        }
    }

    return quit_requested;
}

void glx_size(const GlxWindow *w, int *width, int *height) {
    XWindowAttributes gwa;
    XGetWindowAttributes(w->display, w->window, &gwa);
    *width = gwa.width;
    *height = gwa.height;
}

void glx_swap(const GlxWindow *w) {
    glXSwapBuffers(w->display, w->window);
}
