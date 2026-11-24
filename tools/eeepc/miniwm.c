/* The drag path of bridge-wm/wm.py in C with libxcb, to measure what a compiled WM costs per step.
 * miniwm IPC: a frame (_WIN4WINE_HWND) and a native window; the frame's ConfigureRequests move both,
 * each gets a synthetic ConfigureNotify, and IPC/geometry.tsv is rewritten, as wm.py does. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <xcb/xcb.h>

static xcb_connection_t *c;
static xcb_window_t frame, native;
static int fx, fy, fw, fh, nx, ny, nw, nh;
static const int L = 4, R = 4, T = 30, B = 4;
static const char *ipc;

static xcb_atom_t atom(const char *name)
{
    xcb_intern_atom_reply_t *r = xcb_intern_atom_reply(c, xcb_intern_atom(c, 0, strlen(name), name), NULL);
    xcb_atom_t a = r ? r->atom : 0;
    free(r);
    return a;
}

static void notify(xcb_window_t w, int x, int y, int width, int height)
{
    xcb_configure_notify_event_t e = {.response_type = XCB_CONFIGURE_NOTIFY, .event = w, .window = w,
                                      .x = x, .y = y, .width = width, .height = height};
    xcb_send_event(c, 0, w, XCB_EVENT_MASK_STRUCTURE_NOTIFY, (const char *)&e);
}

static void geometry(void)
{
    char tmp[512], target[512];
    snprintf(tmp, sizeof(tmp), "%s/geometry.tmp", ipc);
    snprintf(target, sizeof(target), "%s/geometry.tsv", ipc);
    FILE *f = fopen(tmp, "w");
    if (!f) return;
    fprintf(f, "%x %d %d %d %d 0 0 0 1024 570\n", native, nx, ny, nw, nh);
    fclose(f);
    rename(tmp, target);
}

int main(int argc, char **argv)
{
    ipc = argv[1];
    c = xcb_connect(NULL, NULL);
    xcb_screen_t *screen = xcb_setup_roots_iterator(xcb_get_setup(c)).data;
    uint32_t mask = XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT | XCB_EVENT_MASK_SUBSTRUCTURE_NOTIFY;
    xcb_change_window_attributes(c, screen->root, XCB_CW_EVENT_MASK, &mask);
    xcb_atom_t hwnd = atom("_WIN4WINE_HWND"), extents = atom("_NET_FRAME_EXTENTS");
    xcb_flush(c);
    xcb_generic_event_t *e;
    while ((e = xcb_wait_for_event(c)))
    {
        switch (e->response_type & 0x7f)
        {
        case XCB_MAP_REQUEST:
        {
            xcb_window_t w = ((xcb_map_request_event_t *)e)->window;
            xcb_get_property_reply_t *p = xcb_get_property_reply(c, xcb_get_property(c, 0, w, hwnd, XCB_ATOM_ANY, 0, 1), NULL);
            xcb_get_geometry_reply_t *g = xcb_get_geometry_reply(c, xcb_get_geometry(c, w), NULL);
            if (p && p->value_len) { frame = w; fx = g->x; fy = g->y; fw = g->width; fh = g->height; }
            else { native = w; nx = g->x; ny = g->y; nw = g->width; nh = g->height; }
            free(p); free(g);
            xcb_map_window(c, w);
            if (frame && native)
            {
                uint32_t v[4] = {L, R, T, B};
                xcb_change_property(c, XCB_PROP_MODE_REPLACE, native, extents, XCB_ATOM_CARDINAL, 32, 4, v);
            }
            break;
        }
        case XCB_CONFIGURE_REQUEST:
        {
            xcb_configure_request_event_t *r = (xcb_configure_request_event_t *)e;
            if (r->window == frame)
            {
                if (r->value_mask & XCB_CONFIG_WINDOW_X) fx = r->x;
                if (r->value_mask & XCB_CONFIG_WINDOW_Y) fy = r->y;
                nx = fx + L; ny = fy + T; nw = fw - L - R; nh = fh - T - B;
                uint32_t nv[4] = {nx, ny, nw, nh};
                xcb_configure_window(c, native, XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y |
                                     XCB_CONFIG_WINDOW_WIDTH | XCB_CONFIG_WINDOW_HEIGHT, nv);
                notify(native, nx, ny, nw, nh);
                uint32_t fv[2] = {fx, fy};
                xcb_configure_window(c, frame, XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y, fv);
                notify(frame, fx, fy, fw, fh);
                geometry();
            }
            else
            {
                uint32_t v[7]; int n = 0;
                if (r->value_mask & XCB_CONFIG_WINDOW_X) v[n++] = r->x;
                if (r->value_mask & XCB_CONFIG_WINDOW_Y) v[n++] = r->y;
                if (r->value_mask & XCB_CONFIG_WINDOW_WIDTH) v[n++] = r->width;
                if (r->value_mask & XCB_CONFIG_WINDOW_HEIGHT) v[n++] = r->height;
                xcb_configure_window(c, r->window, r->value_mask & 0xf, v);
            }
            break;
        }
        }
        free(e);
        xcb_flush(c);
    }
    return 0;
}
