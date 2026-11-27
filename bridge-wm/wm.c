/* Small single-screen X11 WM and file IPC endpoint for the Win32 shell bridge.
 *
 * The C version of wm.py, with libxcb: the same command line, IPC files and
 * X behaviour. wm.py spent about 5 ms of an Atom N550's CPU on each step of a
 * window drag, nearly all of it in python-xlib (38% of a core at 60 Hz).
 * The session runs this one when bridge-wm/build.sh has built it and falls
 * back to wm.py otherwise; the two are kept in step (tests/wm_integration.py
 * runs against either).
 *
 *   wm --ipc DIR [--no-composite]
 */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <glob.h>
#include <limits.h>
#include <math.h>
#include <poll.h>
#include <signal.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/inotify.h>
#include <sys/stat.h>
#include <sys/uio.h>
#include <unistd.h>
#include <xcb/xcb.h>
#include <xcb/xcbext.h>

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

/* Keysyms (X11/keysymdef.h) */
#define XK_Tab 0xff09
#define XK_Pause 0xff13
#define XK_Break 0xff6b
#define XK_space 0x20
#define XK_Escape 0xff1b
#define XK_Return 0xff0d
#define XK_Left 0xff51
#define XK_Up 0xff52
#define XK_Right 0xff53
#define XK_Down 0xff54
#define XK_F1 0xffbe
#define XK_F4 0xffc1
#define XK_F24 0xffd5
#define XK_Super_L 0xffeb
#define XK_Super_R 0xffec
#define XK_a 0x61
#define XK_l 0x6c
#define XK_z 0x7a
#define XK_0 0x30
#define XK_9 0x39

/* ICCCM size and WM hints flags */
#define US_POSITION 1
#define P_POSITION 4
#define P_MIN_SIZE 16
#define ICON_PIXMAP_HINT 4
#define ICON_MASK_HINT 32

#define REVERT_TO_POINTER_ROOT 1
#define NOTIFY_NORMAL 0
#define NOTIFY_POINTER 5
#define NOTIFY_POINTER_ROOT 6
#define NOTIFY_DETAIL_NONE 7

/* Keys besides letters, digits and F1-F24 that XP's hotkeys use with the Windows key. */
static const struct { uint32_t keysym; int vk; } virtual_keys[] = {
    {XK_Tab, 0x09}, {XK_Pause, 0x13}, {XK_Break, 0x13}, {XK_space, 0x20}, {XK_Escape, 0x1b}, {XK_Return, 0x0d},
    {XK_Left, 0x25}, {XK_Up, 0x26}, {XK_Right, 0x27}, {XK_Down, 0x28},
};

/* The keyboard's volume keys (XF86AudioRaiseVolume, XF86AudioLowerVolume,
 * XF86AudioMute) change the default sink, as XP's set its master volume,
 * through PipeWire (wpctl) or PulseAudio (pactl) like xptray's volume flyout. */
#define VOLUME_STEP "3"
#define VOLUME_TOP "97" /* 100 - VOLUME_STEP */
static const struct { uint32_t keysym; const char *script; } volume_keys[] = {
    {0x1008ff13, "if command -v wpctl >/dev/null; then wpctl set-volume -l 1 @DEFAULT_AUDIO_SINK@ " VOLUME_STEP "%+; "
                 "else v=$(pactl get-sink-volume @DEFAULT_SINK@ | awk '/Volume/{gsub(\"%\",\"\",$5); print $5; exit}'); "
                 "if [ \"${v:-0}\" -ge " VOLUME_TOP " ]; then pactl set-sink-volume @DEFAULT_SINK@ 100%; "
                 "else pactl set-sink-volume @DEFAULT_SINK@ +" VOLUME_STEP "%; fi; fi"},
    {0x1008ff11, "if command -v wpctl >/dev/null; then wpctl set-volume @DEFAULT_AUDIO_SINK@ " VOLUME_STEP "%-; "
                 "else pactl set-sink-volume @DEFAULT_SINK@ -" VOLUME_STEP "%; fi"},
    {0x1008ff12, "if command -v wpctl >/dev/null; then wpctl set-mute @DEFAULT_AUDIO_SINK@ toggle; "
                 "else pactl set-sink-mute @DEFAULT_SINK@ toggle; fi"},
};

enum kind { KIND_NORMAL, KIND_POPUP, KIND_PROXY, KIND_DESKTOP, KIND_DOCK };

#define FOLLOWED 32

struct icon_data
{
    uint8_t *data;
    size_t size;
};

struct client
{
    xcb_window_t id;
    enum kind kind;
    int hidden, framed, icon_dirty, placed, moving, redirected;
    int has_restore, restore[4];
    /* A native client in EWMH full screen, and its rectangle without it */
    int fullscreen, fullscreen_restore[4];
    int has_size, size[2];
    int geometry[5]; /* x, y, width, height, border width */
    int has_extents, extents[4]; /* left, right, top, bottom */
    uint32_t hwnd;
    char *title, *wm_class;
    xcb_window_t owner;
    /* Rectangles follow_frame and follow_native applied (None or a list in wm.py) */
    int followed_set, followed_count, followed[FOLLOWED][4];
    /* The icons last written for it (previous_icons) */
    int has_icons;
    struct icon_data icons[2];
};

/* Atoms the WM uses, interned at once at startup. */
#define ATOMS(X_) \
    X_(_NET_SUPPORTING_WM_CHECK) X_(_NET_WM_NAME) X_(UTF8_STRING) X_(_NET_SUPPORTED) X_(_NET_CLIENT_LIST) \
    X_(_NET_CLIENT_LIST_STACKING) X_(_NET_ACTIVE_WINDOW) X_(_NET_WORKAREA) X_(_NET_NUMBER_OF_DESKTOPS) \
    X_(_NET_CURRENT_DESKTOP) X_(_NET_WM_STATE) X_(_NET_WM_STATE_HIDDEN) X_(_NET_WM_STATE_MAXIMIZED_VERT) \
    X_(_NET_WM_STATE_MAXIMIZED_HORZ) X_(_NET_WM_STATE_ABOVE) X_(_NET_WM_STATE_FULLSCREEN) X_(_NET_WM_WINDOW_TYPE) \
    X_(_NET_WM_WINDOW_TYPE_DESKTOP) X_(_NET_WM_WINDOW_TYPE_DOCK) X_(_NET_WM_ICON) X_(_NET_FRAME_EXTENTS) \
    X_(_NET_CLOSE_WINDOW) X_(_NET_WM_STRUT) X_(_NET_WM_STRUT_PARTIAL) X_(_WIN4WINE_CLASS) X_(_WIN4WINE_HWND) \
    X_(WM_STATE) X_(WM_PROTOCOLS) X_(WM_TAKE_FOCUS) X_(WM_DELETE_WINDOW) X_(WM_CHANGE_STATE) \
    X_(_XSETTINGS_SETTINGS) X_(MANAGER) X_(_WIN4WINE_LOCK)

static struct
{
#define FIELD(name) xcb_atom_t name;
    ATOMS(FIELD)
#undef FIELD
} A;

static xcb_connection_t *conn;
static const xcb_setup_t *setup;
static xcb_screen_t *screen;
static int screen_number;
static xcb_window_t root, support;
/* Room for the file names after it in a PATH_MAX path */
static char ipc[PATH_MAX - 256];
static int redirect;

static struct client **clients;
static int client_count, client_capacity;
static xcb_window_t active;
static int area[4];
static int area_known;
static char *previous_snapshot, *previous_geometry, *previous_owners;

/* self.frames: frame (proxy) window -> native client, in insertion order */
static struct { xcb_window_t frame, native; } *frames;
static int frame_count, frame_capacity;

static xcb_window_t *unmanaged;
static int unmanaged_count, unmanaged_capacity;
/* The lock screen's override-redirect window (greeter/xp-lock.py), 0 without one */
static xcb_window_t lock_window;
static xcb_window_t lock_top; /* the window keep_lock put on top: it, or a screen saver */

static int cascade_slot;
static struct
{
    int active;
    xcb_window_t window;
    int button, px, py, x, y, width, height;
} drag;

/* Keyboard */
static xcb_keycode_t min_keycode, max_keycode;
static int keysyms_per_keycode;
static xcb_keysym_t *keymap;
static xcb_keycode_t tab_code, escape_code, f4_code, l_code;
static xcb_keycode_t alt_codes[32], super_codes[2];
static int alt_count, super_count;
static struct { xcb_keycode_t code; const char *script; } volume_codes[ARRAY_SIZE(volume_keys)];
static int volume_count;

static int shell_keys;
static int shell_combo_active, shell_combo[64], shell_combo_count, shell_combo_locks;
static int switching, switch_value, switch_session;

static int xerror; /* a request with a reply failed (an XError in wm.py) */
static volatile sig_atomic_t running = 1;
static int wake_write = -1;

/* ---- Small helpers ------------------------------------------------------ */

static void *xmalloc(size_t size)
{
    void *p = malloc(size ? size : 1);
    if (!p)
    {
        perror("wm");
        exit(1);
    }
    return p;
}

static void *xrealloc(void *p, size_t size)
{
    p = realloc(p, size ? size : 1);
    if (!p)
    {
        perror("wm");
        exit(1);
    }
    return p;
}

#define GROW(array, count, capacity) \
    do { \
        if ((count) >= (capacity)) \
        { \
            (capacity) = (capacity) ? (capacity) * 2 : 16; \
            (array) = xrealloc((array), sizeof(*(array)) * (capacity)); \
        } \
    } while (0)

/* A growing text buffer */
struct text
{
    char *data;
    size_t length, capacity;
};

static void text_add(struct text *t, const char *data, size_t length)
{
    if (t->length + length + 1 > t->capacity)
    {
        t->capacity = (t->length + length + 1) * 2;
        t->data = xrealloc(t->data, t->capacity);
    }
    memcpy(t->data + t->length, data, length);
    t->length += length;
    t->data[t->length] = 0;
}

static void text_printf(struct text *t, const char *format, ...)
{
    char buffer[1024];
    va_list args;
    int n;
    va_start(args, format);
    n = vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    if (n < 0) return;
    if ((size_t)n < sizeof(buffer))
    {
        text_add(t, buffer, n);
        return;
    }
    char *large = xmalloc(n + 1);
    va_start(args, format);
    vsnprintf(large, n + 1, format, args);
    va_end(args);
    text_add(t, large, n);
    free(large);
}

static void ipc_path(char *out, size_t size, const char *name)
{
    snprintf(out, size, "%s/%s", ipc, name);
}

/* Writes DATA to TEMPORARY in the IPC directory and renames it to NAME. */
static void write_ipc(const char *temporary, const char *name, const void *data, size_t size)
{
    char from[PATH_MAX], to[PATH_MAX];
    const char *p = data;
    int fd;
    ipc_path(from, sizeof(from), temporary);
    ipc_path(to, sizeof(to), name);
    fd = open(from, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0666);
    if (fd < 0) return;
    while (size)
    {
        ssize_t n = write(fd, p, size);
        if (n < 0)
        {
            if (errno == EINTR) continue;
            break;
        }
        p += n;
        size -= n;
    }
    close(fd);
    rename(from, to);
}

static void unlink_ipc(const char *name)
{
    char path[PATH_MAX];
    ipc_path(path, sizeof(path), name);
    unlink(path);
}

static int same_text(const char *a, const char *b)
{
    return a && b && !strcmp(a, b);
}

static void replace_text(char **previous, const char *text)
{
    free(*previous);
    *previous = strdup(text);
}

/* Appends code point CP to OUT as UTF-8. */
static void put_utf8(struct text *out, uint32_t cp)
{
    char b[4];
    if (cp < 0x80) b[0] = cp, text_add(out, b, 1);
    else if (cp < 0x800) b[0] = 0xc0 | cp >> 6, b[1] = 0x80 | (cp & 0x3f), text_add(out, b, 2);
    else if (cp < 0x10000)
        b[0] = 0xe0 | cp >> 12, b[1] = 0x80 | (cp >> 6 & 0x3f), b[2] = 0x80 | (cp & 0x3f), text_add(out, b, 3);
    else
        b[0] = 0xf0 | cp >> 18, b[1] = 0x80 | (cp >> 12 & 0x3f), b[2] = 0x80 | (cp >> 6 & 0x3f),
        b[3] = 0x80 | (cp & 0x3f), text_add(out, b, 4);
}

/* The next code point of UTF-8 DATA (Python's decode with errors="replace":
 * an invalid sequence, up to where it stops being valid, gives U+FFFD). */
static uint32_t next_utf8(const uint8_t *data, size_t length, size_t *i)
{
    uint8_t lead = data[*i];
    int need;
    uint32_t cp;
    uint8_t low = 0x80, high = 0xbf;
    (*i)++;
    if (lead < 0x80) return lead;
    if (lead >= 0xc2 && lead <= 0xdf) need = 1, cp = lead & 0x1f;
    else if (lead >= 0xe0 && lead <= 0xef)
    {
        need = 2, cp = lead & 0x0f;
        if (lead == 0xe0) low = 0xa0;
        if (lead == 0xed) high = 0x9f;
    }
    else if (lead >= 0xf0 && lead <= 0xf4)
    {
        need = 3, cp = lead & 0x07;
        if (lead == 0xf0) low = 0x90;
        if (lead == 0xf4) high = 0x8f;
    }
    else return 0xfffd;
    for (int k = 0; k < need; k++)
    {
        if (*i >= length || data[*i] < low || data[*i] > high) return 0xfffd;
        cp = cp << 6 | (data[*i] & 0x3f);
        (*i)++;
        low = 0x80, high = 0xbf;
    }
    return cp;
}

/* Text for a .tsv field: DATA decoded (UTF-8 with replacement, or Latin-1),
 * the characters in REPLACE turned into spaces, at most LIMIT characters. */
static char *field_text(const uint8_t *data, size_t length, int utf8, const char *replace, int limit)
{
    struct text out = {0};
    size_t i = 0;
    int count = 0;
    text_add(&out, "", 0);
    while (i < length && count < limit)
    {
        uint32_t cp = utf8 ? next_utf8(data, length, &i) : data[i++];
        if (!cp) continue;
        if (cp < 0x80 && strchr(replace, (int)cp)) cp = ' ';
        put_utf8(&out, cp);
        count++;
    }
    return out.data;
}

/* ---- X helpers ---------------------------------------------------------- */

/* A reply, or NULL after an error (noted in xerror). */
#define REPLY(name, cookie) ({ \
        xcb_generic_error_t *error_ = NULL; \
        xcb_##name##_reply_t *reply_ = xcb_##name##_reply(conn, cookie, &error_); \
        if (error_) { xerror = 1; free(error_); } \
        reply_; })

static xcb_atom_t intern(const char *name)
{
    xcb_intern_atom_reply_t *r = REPLY(intern_atom, xcb_intern_atom(conn, 0, strlen(name), name));
    xcb_atom_t atom = r ? r->atom : XCB_ATOM_NONE;
    free(r);
    return atom;
}

static void intern_atoms(void)
{
    static const char *names[] = {
#define NAME(name) #name,
        ATOMS(NAME)
#undef NAME
    };
    xcb_intern_atom_cookie_t cookies[ARRAY_SIZE(names)];
    xcb_atom_t *out = (xcb_atom_t *)&A;
    for (size_t i = 0; i < ARRAY_SIZE(names); i++)
        cookies[i] = xcb_intern_atom(conn, 0, strlen(names[i]), names[i]);
    for (size_t i = 0; i < ARRAY_SIZE(names); i++)
    {
        xcb_intern_atom_reply_t *r = REPLY(intern_atom, cookies[i]);
        out[i] = r ? r->atom : XCB_ATOM_NONE;
        free(r);
    }
}

static xcb_get_property_reply_t *get_property(xcb_window_t w, xcb_atom_t atom, xcb_atom_t type, uint32_t length)
{
    return REPLY(get_property, xcb_get_property(conn, 0, w, atom, type, 0, length));
}

/* A property's 32-bit values (self.prop for format 32); *COUNT is 0 for
 * none or for another format. Free the returned reply. */
static xcb_get_property_reply_t *prop32(xcb_window_t w, xcb_atom_t atom, uint32_t **values, int *count)
{
    xcb_get_property_reply_t *r = get_property(w, atom, XCB_GET_PROPERTY_TYPE_ANY, 1 << 20);
    *values = NULL;
    *count = 0;
    if (r && r->format == 32 && r->type != XCB_ATOM_NONE)
    {
        *values = xcb_get_property_value(r);
        *count = xcb_get_property_value_length(r) / 4;
    }
    return r;
}

static int has_atom(xcb_window_t w, xcb_atom_t property, xcb_atom_t wanted)
{
    uint32_t *values;
    int count, found = 0;
    xcb_get_property_reply_t *r = prop32(w, property, &values, &count);
    for (int i = 0; i < count; i++) found |= values[i] == wanted;
    free(r);
    return found;
}

static void set_property(xcb_window_t w, xcb_atom_t property, xcb_atom_t type, const uint32_t *data, int count)
{
    xcb_change_property(conn, XCB_PROP_MODE_REPLACE, w, property, type, 32, count, data);
}

static void set_one(xcb_window_t w, xcb_atom_t property, xcb_atom_t type, uint32_t value)
{
    set_property(w, property, type, &value, 1);
}

static void configure_stack(xcb_window_t w, uint32_t mode)
{
    xcb_configure_window(conn, w, XCB_CONFIG_WINDOW_STACK_MODE, &mode);
}

/* Sends event E (its own type, maybe shorter than X's 32 bytes) padded to 32 bytes. */
static void send_event(xcb_window_t destination, uint32_t mask, const void *e, size_t size)
{
    char buffer[32] = {0};
    memcpy(buffer, e, size < sizeof(buffer) ? size : sizeof(buffer));
    xcb_send_event(conn, 0, destination, mask, buffer);
}

static void send_client_message(xcb_window_t w, xcb_atom_t type, uint32_t d0, uint32_t d1, uint32_t d2, uint32_t d3,
                                uint32_t d4)
{
    xcb_client_message_event_t e = {0};
    e.response_type = XCB_CLIENT_MESSAGE;
    e.format = 32;
    e.window = w;
    e.type = type;
    e.data.data32[0] = d0;
    e.data.data32[1] = d1;
    e.data.data32[2] = d2;
    e.data.data32[3] = d3;
    e.data.data32[4] = d4;
    send_event(w, 0, &e, sizeof(e));
}

static void take_focus_message(xcb_window_t w, xcb_timestamp_t time)
{
    send_client_message(w, A.WM_PROTOCOLS, A.WM_TAKE_FOCUS, time, 0, 0, 0);
}

/* Composite's RedirectWindow (1) and UnredirectWindow (3), RedirectAutomatic */
static void composite_request(uint8_t minor, xcb_window_t w)
{
    static xcb_extension_t composite = {"Composite", 0};
    struct
    {
        uint8_t major, minor;
        uint16_t length;
        uint32_t window;
        uint8_t update, pad[3];
    } request = {0, 0, 0, w, 0, {0}};
    xcb_protocol_request_t protocol = {.count = 2, .ext = &composite, .opcode = minor, .isvoid = 1};
    struct iovec parts[4];
    parts[2].iov_base = &request;
    parts[2].iov_len = sizeof(request);
    parts[3].iov_base = NULL;
    parts[3].iov_len = 0;
    xcb_send_request(conn, 0, parts + 2, &protocol);
}

static xcb_get_geometry_reply_t *get_geometry(xcb_window_t w)
{
    return REPLY(get_geometry, xcb_get_geometry(conn, w));
}

/* Children of W, bottom to top; free *CHILDREN_REPLY. */
static int query_tree(xcb_window_t w, xcb_query_tree_reply_t **tree, xcb_window_t **children, xcb_window_t *parent)
{
    *tree = REPLY(query_tree, xcb_query_tree(conn, w));
    if (!*tree) return -1;
    *children = xcb_query_tree_children(*tree);
    if (parent) *parent = (*tree)->parent;
    return xcb_query_tree_children_length(*tree);
}

static xcb_window_t input_focus(void)
{
    xcb_get_input_focus_reply_t *r = REPLY(get_input_focus, xcb_get_input_focus(conn));
    xcb_window_t focus = r ? r->focus : XCB_NONE;
    free(r);
    return focus;
}

static xcb_window_t parent_of(xcb_window_t w)
{
    xcb_query_tree_reply_t *tree;
    xcb_window_t *children, parent = XCB_NONE;
    if (query_tree(w, &tree, &children, &parent) < 0) return XCB_NONE;
    free(tree);
    return parent;
}

/* ---- Keyboard ----------------------------------------------------------- */

static void load_keymap(void)
{
    xcb_get_keyboard_mapping_reply_t *r;
    min_keycode = setup->min_keycode;
    max_keycode = setup->max_keycode;
    r = REPLY(get_keyboard_mapping, xcb_get_keyboard_mapping(conn, min_keycode, max_keycode - min_keycode + 1));
    if (!r) return;
    keysyms_per_keycode = r->keysyms_per_keycode;
    int count = xcb_get_keyboard_mapping_keysyms_length(r);
    keymap = xmalloc(sizeof(*keymap) * count);
    memcpy(keymap, xcb_get_keyboard_mapping_keysyms(r), sizeof(*keymap) * count);
    free(r);
}

static xcb_keysym_t keycode_to_keysym(xcb_keycode_t code, int index)
{
    if (!keymap || code < min_keycode || code > max_keycode || index >= keysyms_per_keycode) return 0;
    return keymap[(code - min_keycode) * keysyms_per_keycode + index];
}

/* python-xlib's keysym_to_keycode: the lowest keycode in the first column that has it. */
static xcb_keycode_t keysym_to_keycode(xcb_keysym_t keysym)
{
    for (int index = 0; index < keysyms_per_keycode; index++)
        for (int code = min_keycode; code <= max_keycode; code++)
            if (keycode_to_keysym(code, index) == keysym) return code;
    return 0;
}

static int is_super(xcb_keycode_t code)
{
    for (int i = 0; i < super_count; i++)
        if (super_codes[i] == code) return 1;
    return 0;
}

static int is_alt(xcb_keycode_t code)
{
    for (int i = 0; i < alt_count; i++)
        if (alt_codes[i] == code) return 1;
    return 0;
}

static const char *volume_script(xcb_keycode_t code)
{
    for (int i = 0; i < volume_count; i++)
        if (volume_codes[i].code == code) return volume_codes[i].script;
    return NULL;
}

static void grab_key(xcb_keycode_t key, uint16_t modifiers, uint8_t keyboard_mode)
{
    xcb_grab_key(conn, 1, root, modifiers, key, XCB_GRAB_MODE_ASYNC, keyboard_mode);
}

/* ---- Clients ------------------------------------------------------------ */

static struct client *find_client(xcb_window_t id)
{
    for (int i = 0; i < client_count; i++)
        if (clients[i]->id == id) return clients[i];
    return NULL;
}

static int frame_index(xcb_window_t frame)
{
    for (int i = 0; i < frame_count; i++)
        if (frames[i].frame == frame) return i;
    return -1;
}

static int is_frame(xcb_window_t frame)
{
    return frame_index(frame) >= 0;
}

/* frames.get(FRAME), or 0 */
static xcb_window_t frame_native(xcb_window_t frame)
{
    int i = frame_index(frame);
    return i >= 0 ? frames[i].native : XCB_NONE;
}

/* The first frame whose native client is NATIVE, or 0 */
static xcb_window_t native_frame(xcb_window_t native)
{
    for (int i = 0; i < frame_count; i++)
        if (frames[i].native == native) return frames[i].frame;
    return XCB_NONE;
}

static int in_list(const xcb_window_t *list, int count, xcb_window_t w)
{
    for (int i = 0; i < count; i++)
        if (list[i] == w) return 1;
    return 0;
}

static void list_remove(xcb_window_t *list, int *count, xcb_window_t w)
{
    for (int i = 0; i < *count; i++)
        if (list[i] == w)
        {
            memmove(list + i, list + i + 1, sizeof(*list) * (*count - i - 1));
            (*count)--;
            return;
        }
}

static void unmanaged_add(xcb_window_t w)
{
    if (in_list(unmanaged, unmanaged_count, w)) return;
    GROW(unmanaged, unmanaged_count, unmanaged_capacity);
    unmanaged[unmanaged_count++] = w;
}

static void free_icons(struct client *c)
{
    for (int i = 0; i < 2; i++)
    {
        free(c->icons[i].data);
        c->icons[i].data = NULL;
        c->icons[i].size = 0;
    }
    c->has_icons = 0;
}

static void free_client(struct client *c)
{
    free(c->title);
    free(c->wm_class);
    free_icons(c);
    free(c);
}

static enum kind window_kind(xcb_window_t w)
{
    xcb_get_property_reply_t *r = get_property(w, A._WIN4WINE_CLASS, XCB_GET_PROPERTY_TYPE_ANY, 1 << 16);
    char cls[256] = "";
    uint32_t *types;
    int type_count, is_desktop = 0, is_dock = 0;
    if (r && r->type != XCB_ATOM_NONE && r->format == 8)
    {
        int length = xcb_get_property_value_length(r);
        const char *value = xcb_get_property_value(r);
        int start = 0, end = length;
        /* .strip('\0').lower() */
        while (start < end && !value[start]) start++;
        while (end > start && !value[end - 1]) end--;
        if (end - start >= (int)sizeof(cls)) end = start + sizeof(cls) - 1;
        for (int i = start; i < end; i++)
            cls[i - start] = value[i] >= 'A' && value[i] <= 'Z' ? value[i] + 32 : value[i];
        cls[end - start] = 0;
    }
    free(r);
    r = prop32(w, A._NET_WM_WINDOW_TYPE, &types, &type_count);
    for (int i = 0; i < type_count; i++)
    {
        is_desktop |= types[i] == A._NET_WM_WINDOW_TYPE_DESKTOP;
        is_dock |= types[i] == A._NET_WM_WINDOW_TYPE_DOCK;
    }
    free(r);
    if (!strcmp(cls, "dv2controlhost")) return KIND_POPUP;
    if (!strcmp(cls, "win4wineproxy")) return KIND_PROXY;
    /* A top-level WorkerW is not the desktop: XP's Show Desktop moves the
     * desktop's icons into one and raises it over the minimized windows,
     * just below the taskbar (measured on the reference XP machine). */
    if (!strcmp(cls, "progman") || is_desktop) return KIND_DESKTOP;
    if (!strcmp(cls, "shell_traywnd") || is_dock) return KIND_DOCK;
    return KIND_NORMAL;
}

static void update_redirect(struct client *c)
{
    /* Frames only while they move (frame_moves) */
    int wanted = redirect && ((c->kind == KIND_PROXY && c->moving) || (c->hwnd && c->kind != KIND_PROXY));
    if (wanted != c->redirected)
    {
        c->redirected = wanted;
        composite_request(wanted ? 1 : 3, c->id);
    }
}

/* Cache the client properties that every bridge pass reads.
 *
 * Right after a native window moves, the X server is busy repainting
 * what it uncovered, and each round trip waits behind that work. The
 * caches follow PropertyNotify; geometry follows ConfigureNotify. ATOM 0
 * refreshes them all. */
static void refresh(struct client *c, xcb_atom_t atom)
{
    xcb_get_property_reply_t *r;
    if (!atom || atom == A._WIN4WINE_HWND)
    {
        r = get_property(c->id, A._WIN4WINE_HWND, XCB_GET_PROPERTY_TYPE_ANY, 1 << 16);
        c->hwnd = 0;
        if (r && r->type != XCB_ATOM_NONE && xcb_get_property_value_length(r) > 0)
        {
            const void *value = xcb_get_property_value(r);
            if (r->format == 32) c->hwnd = *(const uint32_t *)value;
            else if (r->format == 16) c->hwnd = *(const uint16_t *)value;
            else c->hwnd = *(const uint8_t *)value;
        }
        free(r);
    }
    if (!atom || atom == A._NET_WM_NAME || atom == XCB_ATOM_WM_NAME)
    {
        char *title = NULL;
        r = get_property(c->id, A._NET_WM_NAME, XCB_GET_PROPERTY_TYPE_ANY, 1 << 20);
        if (r && r->type != XCB_ATOM_NONE && xcb_get_property_value_length(r) > 0)
            title = field_text(xcb_get_property_value(r), xcb_get_property_value_length(r), 1, "\t\n\r", 500);
        free(r);
        if (!title)
        {
            /* get_wm_name(): WM_NAME of type STRING, as Latin-1 */
            r = get_property(c->id, XCB_ATOM_WM_NAME, XCB_ATOM_STRING, 1 << 20);
            if (r && r->type == XCB_ATOM_STRING && r->format == 8)
                title = field_text(xcb_get_property_value(r), xcb_get_property_value_length(r), 0, "\t\n\r", 500);
            free(r);
        }
        free(c->title);
        c->title = title ? title : strdup("");
    }
    if (!atom || atom == XCB_ATOM_WM_CLASS)
    {
        /* The application's class: a screen saver is known by it. */
        char *cls = NULL;
        r = get_property(c->id, XCB_ATOM_WM_CLASS, XCB_ATOM_STRING, 1 << 16);
        if (r && r->type == XCB_ATOM_STRING && r->format == 8)
        {
            const uint8_t *value = xcb_get_property_value(r);
            int length = xcb_get_property_value_length(r);
            const uint8_t *nul = memchr(value, 0, length);
            if (nul)
            {
                const uint8_t *second = nul + 1, *end = memchr(second, 0, value + length - second);
                cls = field_text(second, (end ? end : value + length) - second, 0, "\t\n", 100);
            }
        }
        free(r);
        free(c->wm_class);
        c->wm_class = cls ? cls : strdup("");
    }
    if (!atom || atom == XCB_ATOM_WM_TRANSIENT_FOR)
    {
        r = get_property(c->id, XCB_ATOM_WM_TRANSIENT_FOR, XCB_ATOM_WINDOW, 1);
        c->owner = r && r->format == 32 && xcb_get_property_value_length(r) >= 4 ?
                   *(uint32_t *)xcb_get_property_value(r) : XCB_NONE;
        free(r);
    }
}

/* Configure values in X's order: x, y, width, height, border width, sibling, stack mode */
struct values
{
    uint16_t mask;
    int32_t v[7];
};

static void set_value(struct values *values, uint16_t bit, int32_t value)
{
    int index = __builtin_ctz(bit);
    values->mask |= bit;
    values->v[index] = value;
}

static int has_value(const struct values *values, uint16_t bit)
{
    return values->mask & bit;
}

static int32_t value_or(const struct values *values, uint16_t bit, int32_t fallback)
{
    return has_value(values, bit) ? values->v[__builtin_ctz(bit)] : fallback;
}

static void configure_window(xcb_window_t w, const struct values *values)
{
    uint32_t list[7];
    int n = 0;
    for (int i = 0; i < 7; i++)
        if (values->mask & 1 << i) list[n++] = values->v[i];
    xcb_configure_window(conn, w, values->mask, list);
}

static void configure_notify(struct client *c)
{
    /* ICCCM: report root coordinates even when a move/stack request did not
     * generate a size change for the client. Native clients stay root children. */
    xcb_configure_notify_event_t e = {0};
    e.response_type = XCB_CONFIGURE_NOTIFY;
    e.event = c->id;
    e.window = c->id;
    e.above_sibling = XCB_NONE;
    e.x = c->geometry[0];
    e.y = c->geometry[1];
    e.width = c->geometry[2];
    e.height = c->geometry[3];
    e.border_width = c->geometry[4];
    e.override_redirect = 0;
    send_event(c->id, XCB_EVENT_MASK_STRUCTURE_NOTIFY, &e, sizeof(e));
}

/* Configure a client and tell it its new root geometry (ICCCM). */
static void configure(struct client *c, const struct values *values)
{
    configure_window(c->id, values);
    for (int i = 0; i < 5; i++)
        if (values->mask & 1 << i) c->geometry[i] = values->v[i];
    configure_notify(c);
}

static void configure_rect(struct client *c, int x, int y, int width, int height)
{
    struct values values = {0};
    set_value(&values, XCB_CONFIG_WINDOW_X, x);
    set_value(&values, XCB_CONFIG_WINDOW_Y, y);
    set_value(&values, XCB_CONFIG_WINDOW_WIDTH, width);
    set_value(&values, XCB_CONFIG_WINDOW_HEIGHT, height);
    configure(c, &values);
}

/* Cover the whole screen with native client C, without a border. */
static void fit_fullscreen(struct client *c)
{
    xcb_get_geometry_reply_t *g = get_geometry(root);
    struct values values = {0};
    if (!g) return;
    set_value(&values, XCB_CONFIG_WINDOW_X, 0);
    set_value(&values, XCB_CONFIG_WINDOW_Y, 0);
    set_value(&values, XCB_CONFIG_WINDOW_WIDTH, g->width);
    set_value(&values, XCB_CONFIG_WINDOW_HEIGHT, g->height);
    set_value(&values, XCB_CONFIG_WINDOW_BORDER_WIDTH, 0);
    free(g);
    if (memcmp(c->geometry, values.v, sizeof(c->geometry))) configure(c, &values);
}

/* Put native client C in full screen or take it out (Wine sizes its own).
 *
 * Its frame stays where it was, under the client: geometry.tsv gives the
 * proxy the rectangle without full screen, so the taskbar button and the
 * frame are kept and the client returns into the frame afterwards. */
static void set_fullscreen(struct client *c, int on)
{
    if (c->kind != KIND_NORMAL || c->hwnd || !on == !c->fullscreen) return;
    if (on)
    {
        memcpy(c->fullscreen_restore, c->geometry, sizeof(c->fullscreen_restore));
        c->fullscreen = 1;
        fit_fullscreen(c);
    }
    else
    {
        c->fullscreen = 0;
        configure_rect(c, c->fullscreen_restore[0], c->fullscreen_restore[1], c->fullscreen_restore[2],
                       c->fullscreen_restore[3]);
    }
}

static void add_followed(struct client *c, const int rect[4])
{
    if (!c->followed_set) c->followed_count = 0;
    c->followed_set = 1;
    if (c->followed_count == FOLLOWED)
    {
        memmove(c->followed, c->followed + 1, sizeof(c->followed[0]) * (FOLLOWED - 1));
        c->followed_count--;
    }
    memcpy(c->followed[c->followed_count++], rect, sizeof(c->followed[0]));
}

static int was_followed(const struct client *c, const int rect[4])
{
    if (!c->followed_set) return 0;
    for (int i = 0; i < c->followed_count; i++)
        if (!memcmp(c->followed[i], rect, sizeof(c->followed[0]))) return 1;
    return 0;
}

/* Wine keeps its own window surfaces. Native clients (notably xterm on
 * the VM's VGA path) can retain stale pixels after resize/activation.
 * Ask exposed descendants to repaint without clearing their contents or
 * injecting input. A terminal's drawing window is a child of its client. */
static void redraw_native(xcb_window_t w)
{
    struct client *c = find_client(w);
    xcb_window_t *pending;
    int count = 0, capacity = 64;
    if (!c || c->hwnd) return;
    pending = xmalloc(sizeof(*pending) * capacity);
    pending[count++] = w;
    for (int step = 0; step < 2048 && count; step++)
    {
        xcb_window_t child = pending[--count];
        xcb_get_window_attributes_reply_t *attributes =
            REPLY(get_window_attributes, xcb_get_window_attributes(conn, child));
        int viewable = attributes && attributes->map_state == XCB_MAP_STATE_VIEWABLE;
        free(attributes);
        if (!viewable) continue;
        xcb_get_geometry_reply_t *g = get_geometry(child);
        if (!g) continue;
        xcb_expose_event_t e = {0};
        e.response_type = XCB_EXPOSE;
        e.window = child;
        e.width = g->width;
        e.height = g->height;
        free(g);
        send_event(child, XCB_EVENT_MASK_EXPOSURE, &e, sizeof(e));
        xcb_query_tree_reply_t *tree;
        xcb_window_t *children;
        int n = query_tree(child, &tree, &children, NULL);
        for (int i = 0; i < n; i++)
        {
            GROW(pending, count, capacity);
            pending[count++] = children[i];
        }
        if (n >= 0) free(tree);
    }
    free(pending);
}

static void set_active(xcb_window_t w)
{
    active = w;
    set_one(root, A._NET_ACTIVE_WINDOW, XCB_ATOM_WINDOW, w);
}

/* Give the XP desktop or taskbar the keyboard, as a click does on XP.
 *
 * It is no application task, so no client stays active. TIME is that of
 * the user's event: Wine ignores WM_TAKE_FOCUS older than the foreground
 * window's own focus, and the keyboard only reaches a window that Wine
 * made active. */
static void focus_shell(xcb_window_t w, xcb_timestamp_t time)
{
    if (has_atom(w, A.WM_PROTOCOLS, A.WM_TAKE_FOCUS)) take_focus_message(w, time);
    xcb_set_input_focus(conn, REVERT_TO_POINTER_ROOT, w, time);
    if (active) set_active(0);
}

/* The client that holds the X keyboard focus, or one of its windows does. */
static xcb_window_t focused_client(void)
{
    xcb_window_t focused = input_focus();
    while (focused > XCB_INPUT_FOCUS_POINTER_ROOT && focused != root)
    {
        if (find_client(focused)) return focused;
        focused = parent_of(focused);
    }
    return XCB_NONE;
}

/* The native client that has the keyboard rather than Wine, if any. */
static xcb_window_t native_focus(void)
{
    xcb_window_t focused = input_focus();
    while (focused > XCB_INPUT_FOCUS_POINTER_ROOT && focused != root)
    {
        struct client *c = find_client(focused);
        if (c) return c->kind == KIND_NORMAL && !c->hwnd ? focused : XCB_NONE;
        focused = parent_of(focused);
    }
    return XCB_NONE;
}

/* The root's children, bottom to top; *COUNT -1 on error. Free the result. */
static xcb_window_t *stacking(int *count)
{
    xcb_query_tree_reply_t *tree;
    xcb_window_t *children, *copy;
    int n = query_tree(root, &tree, &children, NULL);
    if (n < 0)
    {
        *count = 0;
        return xmalloc(1);
    }
    copy = xmalloc(sizeof(*copy) * n);
    memcpy(copy, children, sizeof(*copy) * n);
    free(tree);
    *count = n;
    return copy;
}

/* Raise again, in their order, the override-redirect windows that were
 * above any of the RAISED windows in BEFORE (a stacking): the All
 * Programs menu over the Start menu that a restack had raised over it. */
static void keep_unmanaged_above(const xcb_window_t *before, int before_count, const xcb_window_t *raised,
                                 int raised_count)
{
    int lowest = -1;
    for (int i = 0; i < before_count; i++)
        if (in_list(raised, raised_count, before[i]))
        {
            lowest = i;
            break;
        }
    if (lowest < 0) return;
    for (int i = lowest + 1; i < before_count; i++)
        if (in_list(unmanaged, unmanaged_count, before[i]))
            configure_stack(before[i], XCB_STACK_MODE_ABOVE);
}

/* Whether client C is a screen saver's window: its program is a .scr. */
static int is_screen_saver(const struct client *c)
{
    size_t length = strlen(c->wm_class);
    return c->kind == KIND_NORMAL && length > 4 && !strcasecmp(c->wm_class + length - 4, ".scr");
}

/* Keep the lock screen above every window, whatever is mapped or raised,
 * except a screen saver that covers the screen: that one stays above it. */
static void keep_lock(void)
{
    xcb_get_geometry_reply_t *g;
    xcb_window_t saver = XCB_NONE;
    if (!lock_window || !(g = get_geometry(root))) return;
    for (int i = 0; i < client_count; i++)
    {
        struct client *c = clients[i];
        if (!c->hidden && is_screen_saver(c) && c->geometry[2] >= g->width && c->geometry[3] >= g->height)
            saver = c->id;
    }
    free(g);
    lock_top = saver ? saver : lock_window;
    if (saver)
    {
        uint32_t list[2] = {saver, XCB_STACK_MODE_BELOW};
        configure_stack(saver, XCB_STACK_MODE_ABOVE);
        xcb_configure_window(conn, lock_window, XCB_CONFIG_WINDOW_SIBLING | XCB_CONFIG_WINDOW_STACK_MODE, list);
    }
    else configure_stack(lock_window, XCB_STACK_MODE_ABOVE);
}

static void restack(void);
static void workarea(void);

/* Keep the windows that OWNER owns (WM_TRANSIENT_FOR) above it, as Windows
 * keeps owned windows above their owner: a click on the caption of a window
 * with a modal dialog raised the window over its dialog. */
static void raise_owned(xcb_window_t owner, const xcb_window_t *seen, int seen_count)
{
    xcb_window_t *now_seen = xmalloc(sizeof(*now_seen) * (seen_count + 1)), *order;
    struct client **owned = xmalloc(sizeof(*owned) * (client_count + 1));
    int owned_count = 0, order_count;
    if (seen_count) memcpy(now_seen, seen, sizeof(*seen) * seen_count);
    now_seen[seen_count++] = owner;
    for (int i = 0; i < client_count; i++)
    {
        struct client *c = clients[i];
        if (c->kind == KIND_NORMAL && !c->hidden && c->owner == owner && !in_list(now_seen, seen_count, c->id))
            owned[owned_count++] = c;
    }
    if (!owned_count) goto done;
    {
        xcb_query_tree_reply_t *tree;
        xcb_window_t *children;
        int n = query_tree(root, &tree, &children, NULL);
        if (n < 0) goto done;
        order = xmalloc(sizeof(*order) * (n + 1));
        memcpy(order, children, sizeof(*order) * n);
        order_count = n;
        free(tree);
    }
    /* By stacking position (unknown last), stably */
    {
        int *keys = xmalloc(sizeof(*keys) * owned_count);
        for (int i = 0; i < owned_count; i++)
        {
            keys[i] = order_count;
            for (int k = 0; k < order_count; k++)
                if (order[k] == owned[i]->id)
                {
                    keys[i] = k;
                    break;
                }
        }
        for (int i = 1; i < owned_count; i++)
            for (int k = i; k > 0 && keys[k - 1] > keys[k]; k--)
            {
                int key = keys[k];
                struct client *c = owned[k];
                keys[k] = keys[k - 1], owned[k] = owned[k - 1];
                keys[k - 1] = key, owned[k - 1] = c;
            }
        free(keys);
    }
    free(order);
    {
        /* Recursion may change nothing in clients, but copy the ids to be safe. */
        xcb_window_t *ids = xmalloc(sizeof(*ids) * owned_count);
        for (int i = 0; i < owned_count; i++) ids[i] = owned[i]->id;
        for (int i = 0; i < owned_count; i++)
        {
            configure_stack(ids[i], XCB_STACK_MODE_ABOVE);
            raise_owned(ids[i], now_seen, seen_count);
        }
        free(ids);
    }
done:
    free(owned);
    free(now_seen);
}

static void set_wm_state(xcb_window_t w, uint32_t state)
{
    uint32_t data[2] = {state, 0};
    set_property(w, A.WM_STATE, A.WM_STATE, data, 2);
}

/* _NET_WM_STATE without REMOVE (two atoms, 0 for none) and with ADD */
static void edit_net_wm_state(xcb_window_t w, const xcb_atom_t *remove, int remove_count, const xcb_atom_t *add,
                              int add_count)
{
    uint32_t *values;
    int count, n = 0;
    xcb_get_property_reply_t *r = prop32(w, A._NET_WM_STATE, &values, &count);
    uint32_t *states = xmalloc(sizeof(*states) * (count + add_count + 1));
    for (int i = 0; i < count; i++)
    {
        int drop = 0;
        for (int k = 0; k < remove_count; k++) drop |= values[i] == remove[k];
        for (int k = 0; k < n; k++) drop |= states[k] == values[i]; /* a set */
        if (!drop) states[n++] = values[i];
    }
    free(r);
    for (int k = 0; k < add_count; k++)
    {
        int present = 0;
        for (int i = 0; i < n; i++) present |= states[i] == add[k];
        if (!present) states[n++] = add[k];
    }
    set_property(w, A._NET_WM_STATE, XCB_ATOM_ATOM, states, n);
    free(states);
}

static void focus(xcb_window_t w)
{
    /* Wine activation targets the proxy's XID; the native client owns X
     * keyboard focus and the single EWMH task identity. */
    if (is_frame(w))
    {
        struct client *native = find_client(frame_native(w));
        if (!native) return;
        w = native->id;
    }
    struct client *c = find_client(w);
    if (c && c->kind == KIND_POPUP)
    {
        /* An XP Start popup needs real X keyboard focus for Wine activation,
         * without becoming the active application task or an Alt+Tab entry. */
        int before_count;
        xcb_window_t *before;
        xcb_map_window(conn, w);
        before = stacking(&before_count);
        configure_stack(w, XCB_STACK_MODE_ABOVE);
        keep_unmanaged_above(before, before_count, &w, 1);
        free(before);
        keep_lock();
        if (has_atom(w, A.WM_PROTOCOLS, A.WM_TAKE_FOCUS)) take_focus_message(w, XCB_CURRENT_TIME);
        xcb_set_input_focus(conn, REVERT_TO_POINTER_ROOT, w, XCB_CURRENT_TIME);
        return;
    }
    if (c && (c->kind == KIND_DESKTOP || c->kind == KIND_DOCK))
    {
        focus_shell(w, XCB_CURRENT_TIME);
        return;
    }
    if (!c || c->kind != KIND_NORMAL) return;
    c->hidden = 0;
    xcb_map_window(conn, w);
    set_wm_state(w, 1);
    edit_net_wm_state(w, &A._NET_WM_STATE_HIDDEN, 1, NULL, 0);
    configure_stack(w, XCB_STACK_MODE_ABOVE);
    raise_owned(w, NULL, 0);
    if (has_atom(w, A.WM_PROTOCOLS, A.WM_TAKE_FOCUS)) take_focus_message(w, XCB_CURRENT_TIME);
    xcb_set_input_focus(conn, REVERT_TO_POINTER_ROOT, w, XCB_CURRENT_TIME);
    set_active(w);
    restack();
    redraw_native(w);
}

/* Focus the most recently managed client that is shown, or nothing. */
static void focus_visible(void)
{
    for (int i = client_count - 1; i >= 0; i--)
        if (clients[i]->kind == KIND_NORMAL && !clients[i]->hidden)
        {
            focus(clients[i]->id);
            return;
        }
    xcb_set_input_focus(conn, REVERT_TO_POINTER_ROOT, root, XCB_CURRENT_TIME);
    set_one(root, A._NET_ACTIVE_WINDOW, XCB_ATOM_WINDOW, 0);
}

/* The lock screen went: the keyboard goes back to the window that had it,
 * the active one, or else XP's desktop, as after unlocking XP. */
static void lock_gone(void)
{
    struct client *c = find_client(active);
    lock_window = XCB_NONE;
    if (!c || c->hidden)
        for (int i = 0; i < client_count; i++)
            if (clients[i]->kind == KIND_DESKTOP) c = clients[i];
    if (c && !c->hidden) focus(c->id);
    else focus_visible();
}

static void minimize(xcb_window_t w)
{
    struct client *c = find_client(w);
    if (!c) return;
    c->hidden = 1;
    xcb_unmap_window(conn, w);
    for (int i = 0; i < frame_count; i++)
        if (frames[i].native == w)
        {
            struct client *frame = find_client(frames[i].frame);
            if (frame) xcb_unmap_window(conn, frame->id);
        }
    set_wm_state(w, 3);
    edit_net_wm_state(w, NULL, 0, &A._NET_WM_STATE_HIDDEN, 1);
    if (active == w)
    {
        active = 0;
        focus_visible();
    }
}

static void close_window(xcb_window_t w)
{
    if (has_atom(w, A.WM_PROTOCOLS, A.WM_DELETE_WINDOW))
        send_client_message(w, A.WM_PROTOCOLS, A.WM_DELETE_WINDOW, XCB_CURRENT_TIME, 0, 0, 0);
    else xcb_kill_client(conn, w);
}

static void icon_name(char *out, size_t size, xcb_window_t w, int small, const char *suffix)
{
    snprintf(out, size, "icon-%x%s.%s", w, small ? "-small" : "", suffix);
}

static void unmanage(xcb_window_t window_id)
{
    struct client *c = find_client(window_id);
    char name[64];
    list_remove(unmanaged, &unmanaged_count, window_id);
    if (c)
    {
        for (int i = 0; i < client_count; i++)
            if (clients[i] == c)
            {
                memmove(clients + i, clients + i + 1, sizeof(*clients) * (client_count - i - 1));
                client_count--;
                break;
            }
        free_client(c);
    }
    else
    {
        int known = 0;
        for (int i = 0; i < frame_count; i++) known |= frames[i].frame == window_id || frames[i].native == window_id;
        /* Not a client: Wine destroys short-lived top-level windows (a
         * submenu's toolbar) while the Start menu's cascades change. */
        if (!known) return;
    }
    icon_name(name, sizeof(name), window_id, 0, "argb");
    unlink_ipc(name);
    icon_name(name, sizeof(name), window_id, 1, "argb");
    unlink_ipc(name);
    /* Hide the orphan frame before the proxy destroys its HWND, and reject
     * any activation the proxy has already queued for it. */
    for (int i = 0; i < frame_count;)
    {
        if (frames[i].frame == window_id || frames[i].native == window_id)
        {
            struct client *frame = find_client(frames[i].frame);
            if (frame) xcb_unmap_window(conn, frame->id);
            memmove(frames + i, frames + i + 1, sizeof(*frames) * (frame_count - i - 1));
            frame_count--;
        }
        else i++;
    }
    if (active == window_id)
    {
        /* Focus the next window still shown, not a minimized one (Wine
         * withdraws a minimized window that it restores, and maps it again). */
        active = 0;
        focus_visible();
    }
    restack();
    workarea();
}

static void restack(void)
{
    /* Native clients remain root children. The genuine Wine nonclient frame
     * sits immediately below its native client, so Wine draws the XP caption
     * and resize borders while the native pixels cover its unused client area. */
    int before_count, raised_count = 0, topmost_count = 0, n = 0;
    xcb_window_t *before = stacking(&before_count), fullscreen = XCB_NONE;
    xcb_window_t *raised = xmalloc(sizeof(*raised) * (2 * client_count + 1));
    xcb_window_t *topmost = xmalloc(sizeof(*topmost) * (client_count + 1));
    uint32_t *ids;
    for (int i = 0; i < frame_count; i++)
    {
        if (find_client(frames[i].frame) && find_client(frames[i].native))
        {
            uint32_t list[2] = {frames[i].native, XCB_STACK_MODE_BELOW};
            xcb_configure_window(conn, frames[i].frame, XCB_CONFIG_WINDOW_SIBLING | XCB_CONFIG_WINDOW_STACK_MODE, list);
        }
    }
    for (int i = 0; i < client_count; i++)
        if (clients[i]->kind == KIND_DESKTOP) configure_stack(clients[i]->id, XCB_STACK_MODE_BELOW);
    for (int i = 0; i < client_count; i++)
        if (clients[i]->kind == KIND_DOCK && !clients[i]->hidden)
        {
            configure_stack(clients[i]->id, XCB_STACK_MODE_ABOVE);
            raised[raised_count++] = clients[i]->id;
        }
    /* The active full screen window covers the taskbar, as on XP, with the
     * windows it owns (a dialog of it may be the active one); topmost
     * windows stay above it. */
    {
        struct client *c = find_client(active);
        for (int depth = 0; c && c->kind == KIND_NORMAL && !c->hidden && depth <= client_count; depth++)
        {
            if (has_atom(c->id, A._NET_WM_STATE, A._NET_WM_STATE_FULLSCREEN)) fullscreen = c->id;
            c = c->owner ? find_client(c->owner) : NULL;
        }
    }
    if (fullscreen)
    {
        configure_stack(fullscreen, XCB_STACK_MODE_ABOVE);
        raised[raised_count++] = fullscreen;
        for (int i = 0; i < client_count; i++)
        {
            struct client *c = clients[i];
            if (c->kind == KIND_NORMAL && !c->hidden && c->owner == fullscreen && c->id != fullscreen)
            {
                configure_stack(c->id, XCB_STACK_MODE_ABOVE);
                raised[raised_count++] = c->id;
            }
        }
    }
    /* Win32 topmost windows (EWMH above), such as XP's dimmed shut down
     * screen, cover the topmost taskbar; keep their transients above them. */
    for (int i = 0; i < client_count; i++)
    {
        struct client *c = clients[i];
        if (c->kind == KIND_NORMAL && !c->hidden && has_atom(c->id, A._NET_WM_STATE, A._NET_WM_STATE_ABOVE))
        {
            configure_stack(c->id, XCB_STACK_MODE_ABOVE);
            topmost[topmost_count++] = c->id;
        }
    }
    for (int i = 0; i < client_count; i++)
    {
        struct client *c = clients[i];
        if (c->kind == KIND_NORMAL && !c->hidden && !in_list(topmost, topmost_count, c->id) &&
            in_list(topmost, topmost_count, c->owner))
        {
            configure_stack(c->id, XCB_STACK_MODE_ABOVE);
            raised[raised_count++] = c->id;
        }
    }
    for (int i = 0; i < topmost_count; i++)
        if (!in_list(raised, raised_count, topmost[i])) raised[raised_count++] = topmost[i];
    for (int i = 0; i < client_count; i++)
        if (clients[i]->kind == KIND_POPUP && !clients[i]->hidden)
        {
            configure_stack(clients[i]->id, XCB_STACK_MODE_ABOVE);
            if (!in_list(raised, raised_count, clients[i]->id)) raised[raised_count++] = clients[i]->id;
        }
    keep_unmanaged_above(before, before_count, raised, raised_count);
    keep_lock();
    free(before);
    free(raised);
    free(topmost);
    ids = xmalloc(sizeof(*ids) * (client_count + 1));
    for (int i = 0; i < client_count; i++)
        if (clients[i]->kind != KIND_PROXY) ids[n++] = clients[i]->id;
    set_property(root, A._NET_CLIENT_LIST, XCB_ATOM_WINDOW, ids, n);
    {
        int children_count, stack_count = 0;
        xcb_window_t *children = stacking(&children_count);
        uint32_t *stack = xmalloc(sizeof(*stack) * (children_count + 1));
        for (int i = 0; i < children_count; i++)
        {
            xcb_window_t w = children[i], entry;
            if (!in_list(ids, n, w) && !is_frame(w)) continue;
            entry = is_frame(w) ? frame_native(w) : w;
            if (!in_list(stack, stack_count, entry)) stack[stack_count++] = entry;
        }
        set_property(root, A._NET_CLIENT_LIST_STACKING, XCB_ATOM_WINDOW, stack, stack_count);
        free(stack);
        free(children);
    }
    free(ids);
}

static void workarea(void)
{
    xcb_get_geometry_reply_t *g = get_geometry(root);
    int left = 0, top = 0, right = 0, bottom = 0, gw, gh;
    if (!g) return;
    gw = g->width;
    gh = g->height;
    free(g);
    for (int i = 0; i < client_count; i++)
    {
        struct client *c = clients[i];
        uint32_t *strut;
        int count;
        if (c->kind != KIND_DOCK || c->hidden) continue;
        xcb_get_property_reply_t *r = prop32(c->id, A._NET_WM_STRUT_PARTIAL, &strut, &count);
        if (!count)
        {
            free(r);
            r = prop32(c->id, A._NET_WM_STRUT, &strut, &count);
        }
        if (count >= 4)
        {
            left = left > (int)strut[0] ? left : (int)strut[0];
            right = right > (int)strut[1] ? right : (int)strut[1];
            top = top > (int)strut[2] ? top : (int)strut[2];
            bottom = bottom > (int)strut[3] ? bottom : (int)strut[3];
        }
        else
        {
            int x = c->geometry[0], y = c->geometry[1], width = c->geometry[2], height = c->geometry[3];
            if (width >= gw / 2)
            {
                if (y >= gh / 2) bottom = bottom > gh - y ? bottom : gh - y;
                else top = top > y + height ? top : y + height;
            }
            else if (x >= gw / 2) right = right > gw - x ? right : gw - x;
            else left = left > x + width ? left : x + width;
        }
        free(r);
    }
    area[0] = left;
    area[1] = top;
    area[2] = gw - left - right > 1 ? gw - left - right : 1;
    area[3] = gh - top - bottom > 1 ? gh - top - bottom : 1;
    area_known = 1;
    {
        uint32_t *values;
        int count, same;
        xcb_get_property_reply_t *r = prop32(root, A._NET_WORKAREA, &values, &count);
        same = count == 4;
        for (int i = 0; same && i < 4; i++) same = (int)values[i] == area[i];
        free(r);
        if (!same)
        {
            uint32_t data[4] = {area[0], area[1], area[2], area[3]};
            set_property(root, A._NET_WORKAREA, XCB_ATOM_CARDINAL, data, 4);
        }
    }
}

/* WM_NORMAL_HINTS as python-xlib reads them (exactly 18 values), or NULL */
static xcb_get_property_reply_t *normal_hints(xcb_window_t w, const uint32_t **hints)
{
    xcb_get_property_reply_t *r = get_property(w, XCB_ATOM_WM_NORMAL_HINTS, XCB_ATOM_WM_SIZE_HINTS, 100);
    if (r && r->format == 32 && xcb_get_property_value_length(r) == 18 * 4)
    {
        *hints = xcb_get_property_value(r);
        return r;
    }
    free(r);
    *hints = NULL;
    return NULL;
}

static int positioned(struct client *c)
{
    const uint32_t *hints;
    xcb_get_property_reply_t *r = normal_hints(c->id, &hints);
    int result;
    if (!hints || !(hints[0] & (US_POSITION | P_POSITION))) result = 0;
    else result = (hints[0] & US_POSITION) || c->geometry[0] || c->geometry[1];
    free(r);
    return result;
}

static int max3(int a, int b, int c)
{
    int m = a > b ? a : b;
    return m > c ? m : c;
}

/* The client's size, shrunk to fit the work area with its frame (not below its minimum size). */
static void fit(struct client *c, int frame_width, int frame_height, int *width, int *height)
{
    const uint32_t *hints;
    xcb_get_property_reply_t *r = normal_hints(c->id, &hints);
    int min_width = 1, min_height = 1, w, h;
    if (hints && hints[0] & P_MIN_SIZE) min_width = (int32_t)hints[5], min_height = (int32_t)hints[6];
    free(r);
    w = max3(area[2] - frame_width, min_width, 1);
    h = max3(area[3] - frame_height, min_height, 1);
    *width = c->geometry[2] < w ? c->geometry[2] : w;
    *height = c->geometry[3] < h ? c->geometry[3] : h;
}

/* Next XP default-window slot, as native client coordinates.
 *
 * XP steps 22px right (96 DPI, scaled here by the frame width) and
 * SM_CYSIZE + SM_CYFRAME down, which is the caption extent minus one,
 * and restarts after a quarter of the work area. */
static void cascade(int left, int top, int *out_x, int *out_y)
{
    int x = area[0], y = area[1], width = area[2], height = area[3];
    /* Python's round(22 * max(left, 1) / 4): halves to even */
    int numerator = 22 * (left > 1 ? left : 1), step_x = numerator / 4, rest = numerator % 4;
    if (rest * 2 > 4 || (rest * 2 == 4 && step_x % 2)) step_x++;
    if (step_x < 1) step_x = 1;
    int step_y = top - 1 > 1 ? top - 1 : 1;
    int slot = cascade_slot + 1;
    if (slot * step_x > width / 4 || slot * step_y > height / 4) slot = 1;
    cascade_slot = slot;
    *out_x = x + slot * step_x + left;
    *out_y = y + slot * step_y + top;
}

static void manage(xcb_window_t w)
{
    xcb_get_window_attributes_reply_t *attributes = REPLY(get_window_attributes, xcb_get_window_attributes(conn, w));
    struct client *client;
    if (!attributes) return;
    if (attributes->override_redirect || w == support)
    {
        if (attributes->override_redirect) unmanaged_add(w);
        free(attributes);
        xcb_map_window(conn, w);
        return;
    }
    free(attributes);
    if (!(client = find_client(w)))
    {
        xcb_get_geometry_reply_t *g = get_geometry(w);
        uint32_t mask = XCB_EVENT_MASK_PROPERTY_CHANGE | XCB_EVENT_MASK_FOCUS_CHANGE;
        if (!g) return;
        client = xmalloc(sizeof(*client));
        memset(client, 0, sizeof(*client));
        client->id = w;
        client->kind = window_kind(w);
        client->icon_dirty = 1;
        client->geometry[0] = g->x;
        client->geometry[1] = g->y;
        client->geometry[2] = g->width;
        client->geometry[3] = g->height;
        client->geometry[4] = g->border_width;
        free(g);
        GROW(clients, client_count, client_capacity);
        clients[client_count++] = client;
        xcb_change_window_attributes(conn, w, XCB_CW_EVENT_MASK, &mask);
        /* Left and right clicks activate a window, as on XP (a right click
         * on the desktop opens its menu, which then needs the keyboard). */
        for (uint8_t button = 1; button <= 3; button += 2)
            xcb_grab_button(conn, 1, w, XCB_EVENT_MASK_BUTTON_PRESS, XCB_GRAB_MODE_SYNC, XCB_GRAB_MODE_ASYNC,
                            XCB_NONE, XCB_NONE, button, 0);
        refresh(client, 0);
    }
    client->kind = window_kind(w);
    update_redirect(client);
    if (client->kind == KIND_PROXY)
    {
        if (!is_frame(w)) xcb_unmap_window(conn, w);
        return;
    }
    client->hidden = 0;
    if (client->kind == KIND_NORMAL && client->owner && !client->hwnd && !client->placed)
    {
        /* An owned Linux window has no frame; keep it inside the work area
         * (GTK's file chooser asks for a fixed size, larger than small screens),
         * centred on its owner unless it placed itself (GTK 4 does not). */
        int x = client->geometry[0], y = client->geometry[1], width, height;
        struct client *owner = find_client(client->owner);
        client->placed = 1;
        fit(client, 0, 0, &width, &height);
        if (owner && !positioned(client))
        {
            x = owner->geometry[0] + (owner->geometry[2] - width) / 2;
            y = owner->geometry[1] + (owner->geometry[3] - height) / 2;
            /* Python's // floors */
            if ((owner->geometry[2] - width) % 2 && owner->geometry[2] - width < 0) x--;
            if ((owner->geometry[3] - height) % 2 && owner->geometry[3] - height < 0) y--;
        }
        int high_x = area[0] + area[2] - width, high_y = area[1] + area[3] - height;
        x = x < high_x ? x : high_x;
        y = y < high_y ? y : high_y;
        x = x > area[0] ? x : area[0];
        y = y > area[1] ? y : area[1];
        if (x != client->geometry[0] || y != client->geometry[1] || width != client->geometry[2] ||
            height != client->geometry[3])
            configure_rect(client, x, y, width, height);
    }
    /* Mapped in full screen (mpv --fs, SDL games); kept again when it is mapped after a withdrawal */
    if (has_atom(w, A._NET_WM_STATE, A._NET_WM_STATE_FULLSCREEN)) set_fullscreen(client, 1);
    set_wm_state(w, 1);
    xcb_map_window(conn, w);
    if (client->kind == KIND_NORMAL || client->kind == KIND_POPUP) focus(w);
    restack();
    workarea();
}

static void maximize(xcb_window_t w, int restore)
{
    struct client *c = find_client(w);
    xcb_atom_t atoms[2] = {A._NET_WM_STATE_MAXIMIZED_VERT, A._NET_WM_STATE_MAXIMIZED_HORZ};
    int x, y, width, height;
    if (!c) return;
    if (c->hwnd)
    {
        /* Wine's owner thread supplies its computed outer rectangle. Only
         * acknowledge EWMH state here; resizing it races restore placement. */
        edit_net_wm_state(w, atoms, 2, atoms, restore ? 0 : 2);
        focus(w);
        return;
    }
    if (c->fullscreen)
    {
        /* Maximized or restored under full screen: the rectangle it leaves
         * full screen for changes (sync_frames fits it to the maximized
         * frame), and the client keeps covering the screen. */
        if (!restore && !c->has_restore)
        {
            memcpy(c->restore, c->fullscreen_restore, sizeof(c->restore));
            c->has_restore = 1;
        }
        else if (restore && c->has_restore)
        {
            memcpy(c->fullscreen_restore, c->restore, sizeof(c->restore));
            c->has_restore = 0;
        }
        edit_net_wm_state(w, atoms, 2, atoms, restore ? 0 : 2);
        focus(w);
        return;
    }
    if (restore && c->has_restore)
    {
        x = c->restore[0], y = c->restore[1], width = c->restore[2], height = c->restore[3];
        c->has_restore = 0;
    }
    else if (!restore)
    {
        xcb_window_t frame_id = native_frame(w);
        struct client *frame = frame_id ? find_client(frame_id) : NULL;
        if (!c->has_restore)
        {
            memcpy(c->restore, c->geometry, sizeof(c->restore));
            c->has_restore = 1;
        }
        x = area[0], y = area[1], width = area[2], height = area[3];
        if (frame && c->has_extents)
        {
            /* The proxy maximizes the frame (geometry.tsv says so) and
             * sync_frames fits the client into it once frames.tsv has its
             * maximized borders. Moving the client here first, to a guess
             * from the restored frame, resized the frame twice more. */
            edit_net_wm_state(w, NULL, 0, atoms, 2);
            focus(w);
            return;
        }
        /* Fit the complete Wine border inside the work area, using its
         * measured extents rather than assuming classic or Luna metrics. */
        if (frame)
        {
            xcb_get_geometry_reply_t *outer = get_geometry(frame->id);
            if (!outer) return;
            int left = c->geometry[0] - outer->x, top = c->geometry[1] - outer->y;
            width -= outer->width - c->geometry[2];
            height -= outer->height - c->geometry[3];
            x += left;
            y += top;
            free(outer);
        }
    }
    else
    {
        focus(w);
        return;
    }
    configure_rect(c, x, y, width > 1 ? width : 1, height > 1 ? height : 1);
    edit_net_wm_state(w, atoms, 2, atoms, restore ? 0 : 2);
    focus(w);
}

/* ---- The bridge's files ------------------------------------------------- */

static int bridge_alive(void)
{
    /* The proxy holds a byte-range lock on proxy.lock while it runs;
     * wineserver backs it with a POSIX lock and drops it at process exit. */
    char path[PATH_MAX];
    struct flock lock = {.l_type = F_WRLCK, .l_whence = SEEK_SET, .l_start = 0, .l_len = 1};
    int fd, alive;
    ipc_path(path, sizeof(path), "proxy.lock");
    fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) return 0;
    alive = fcntl(fd, F_GETLK, &lock) == 0 && lock.l_type != F_UNLCK;
    close(fd);
    return alive;
}

static void write_switch(char state)
{
    char text[64];
    int n = snprintf(text, sizeof(text), "%d %d %c\n", switch_session, switch_value, state);
    write_ipc("switch.tmp", "switch.tsv", text, n);
}

static void write_shell_keys(const char *kind, const int *keys, int count)
{
    struct text t = {0};
    text_printf(&t, "%d %s", ++shell_keys, kind);
    for (int i = 0; i < count; i++) text_printf(&t, " %x", keys[i]);
    text_add(&t, "\n", 1);
    write_ipc("shellkeys.tmp", "shellkeys.tsv", t.data, t.length);
    free(t.data);
}

static void cycle(int reverse)
{
    int count = 0, index = -1;
    xcb_window_t *ids = xmalloc(sizeof(*ids) * (client_count + 1));
    for (int i = 0; i < client_count; i++)
        if (clients[i]->kind == KIND_NORMAL)
        {
            if (clients[i]->id == active) index = count;
            ids[count++] = clients[i]->id;
        }
    if (!count) set_active(0);
    else focus(ids[(((index + (reverse ? -1 : 1)) % count) + count) % count]);
    free(ids);
}

static void end_switch(char state)
{
    write_switch(state);
    xcb_ungrab_keyboard(conn, XCB_CURRENT_TIME);
    switching = 0;
}

/* Alt+Tab: XP's switch window, drawn by the Win32 bridge.
 *
 * The keyboard stays grabbed while Alt is held; Tab and Shift+Tab move
 * the selection, Escape cancels and releasing Alt switches. Without the
 * bridge, cycle through the clients directly. */
static void switch_key(int reverse)
{
    xcb_grab_keyboard_reply_t *grab;
    xcb_query_keymap_reply_t *keys;
    int alt_down = 0;
    if (!bridge_alive())
    {
        cycle(reverse);
        return;
    }
    grab = REPLY(grab_keyboard, xcb_grab_keyboard(conn, 1, root, XCB_CURRENT_TIME, XCB_GRAB_MODE_ASYNC,
                                                  XCB_GRAB_MODE_ASYNC));
    if (!grab || grab->status != XCB_GRAB_STATUS_SUCCESS)
    {
        free(grab);
        cycle(reverse);
        return;
    }
    free(grab);
    switch_session++;
    switching = 1;
    switch_value = reverse ? -1 : 1;
    write_switch('o');
    /* Alt may already be up when the grab starts; its release is then lost. */
    keys = REPLY(query_keymap, xcb_query_keymap(conn));
    if (keys)
    {
        for (int i = 0; i < alt_count; i++)
            alt_down |= keys->keys[alt_codes[i] / 8] >> (alt_codes[i] % 8) & 1;
        free(keys);
    }
    if (!alt_down) end_switch('c');
}

static void switch_event(uint8_t type, xcb_keycode_t detail, uint16_t state)
{
    if (type == XCB_KEY_PRESS && detail == tab_code)
    {
        switch_value += state & XCB_MOD_MASK_SHIFT ? -1 : 1;
        write_switch('o');
    }
    else if (type == XCB_KEY_PRESS && detail == escape_code) end_switch('x');
    else if (type == XCB_KEY_RELEASE && is_alt(detail)) end_switch('c');
}

/* Win+L, which XP's Explorer leaves to Windows itself: ask the session to
 * lock the screen (session/screen.py). */
static int is_lock_key(xcb_keycode_t detail, uint16_t state)
{
    return detail == l_code && l_code &&
           !(state & (XCB_MOD_MASK_SHIFT | XCB_MOD_MASK_CONTROL | XCB_MOD_MASK_1));
}

static void request_lock(void)
{
    write_ipc("lock-request.tmp", "lock-request", "lock\n", 5);
}

/* A Windows key or Ctrl+Esc press, frozen by the WM's synchronous grab.
 *
 * Under a native client, Wine has none of these keys, and a window that
 * Wine has yet to activate would lose them. The WM keeps the keyboard
 * until the Windows key is up (the grab of a key lasts until its release),
 * then the proxy enters the same keys into Wine with SendInput: XP's
 * hotkeys (Win+R, Win+E, ...) and Wine's Start menu keys work as for a
 * Win32 window, with the foreground's right to open the Start menu.
 * Otherwise the key goes on as is. */
static void shell_key(xcb_key_press_event_t *e)
{
    uint8_t mode = XCB_ALLOW_REPLAY_KEYBOARD;
    if (switching)
    {
        /* The switcher's own grab delivers it; Esc there closes the switcher. */
        xcb_allow_events(conn, XCB_ALLOW_ASYNC_KEYBOARD, e->time);
        switch_event(XCB_KEY_PRESS, e->detail, e->state);
        return;
    }
    if (native_focus())
    {
        mode = XCB_ALLOW_ASYNC_KEYBOARD;
        if (e->detail == escape_code) write_shell_keys("ctrl-escape", NULL, 0);
        else
        {
            shell_combo_active = 1;
            shell_combo_count = 0;
            shell_combo_locks = 0;
        }
    }
    xcb_allow_events(conn, mode, e->time);
}

/* A key while the WM holds the keyboard for the Windows key. */
static void shell_combo_event(uint8_t type, xcb_keycode_t detail, uint16_t state)
{
    if (type == XCB_KEY_PRESS && is_lock_key(detail, state))
    {
        shell_combo_locks = 1;
        request_lock();
    }
    else if (type == XCB_KEY_PRESS && !is_super(detail))
    {
        xcb_keysym_t keysym = keycode_to_keysym(detail, 0);
        int vk = -1;
        for (size_t i = 0; i < ARRAY_SIZE(virtual_keys); i++)
            if (virtual_keys[i].keysym == keysym) vk = virtual_keys[i].vk;
        if (vk < 0 && keysym >= XK_a && keysym <= XK_z) vk = keysym - 32;
        if (vk < 0 && keysym >= XK_0 && keysym <= XK_9) vk = keysym;
        if (vk < 0 && keysym >= XK_F1 && keysym <= XK_F24) vk = 0x70 + keysym - XK_F1;
        if (vk >= 0 && shell_combo_count < (int)ARRAY_SIZE(shell_combo)) shell_combo[shell_combo_count++] = vk;
    }
    else if (type == XCB_KEY_RELEASE && is_super(detail))
    {
        /* After Win+L the Windows key alone would open the Start menu. */
        if (!shell_combo_locks) write_shell_keys("windows", shell_combo, shell_combo_count);
        shell_combo_active = 0;
    }
}

/* Run a volume key's command; held keys repeat it. */
static void volume_key(const char *script)
{
    pid_t pid = fork();
    if (pid < 0)
    {
        printf("volume key: %s\n", strerror(errno));
        fflush(stdout);
        return;
    }
    if (!pid)
    {
        int null = open("/dev/null", O_RDWR);
        signal(SIGCHLD, SIG_DFL); /* the WM ignores it; the script's shell must not */
        setsid();
        if (null >= 0)
        {
            dup2(null, 0);
            dup2(null, 1);
            dup2(null, 2);
        }
        execl("/bin/sh", "sh", "-c", script, (char *)NULL);
        _exit(127);
    }
}

/* Publish native geometry from the cache, without X round trips. */
static void write_geometry(void)
{
    struct text t = {0};
    text_add(&t, "", 0);
    for (int i = 0; i < client_count; i++)
    {
        struct client *c = clients[i];
        /* In full screen, where its frame stays: the rectangle without it. */
        const int *g = c->fullscreen ? c->fullscreen_restore : c->geometry;
        if (c->kind == KIND_NORMAL && !c->hwnd)
            text_printf(&t, "%x %d %d %d %d %d %d %d %d %d\n", c->id, g[0], g[1], g[2], g[3], c->has_restore, area[0],
                        area[1], area[2], area[3]);
    }
    if (!same_text(t.data, previous_geometry))
    {
        write_ipc("geometry.tmp", "geometry.tsv", t.data, t.length);
        replace_text(&previous_geometry, t.data);
    }
    free(t.data);
}

/* ---- Icons (icons.py) --------------------------------------------------- */

#define MAX_SIDE 256
#define MAX_WORDS 262144

struct image
{
    int width, height, depth;
    uint32_t *pixels;
};

static void free_image(struct image *image)
{
    free(image->pixels);
    image->pixels = NULL;
}

static int net_icon(xcb_window_t w, int side, struct image *out)
{
    xcb_get_property_reply_t *r = get_property(w, A._NET_WM_ICON, XCB_ATOM_CARDINAL, MAX_WORDS);
    const uint32_t *values;
    long count, offset = 0;
    long best_distance = 0, best_area = 0, best_offset = -1, best_width = 0, best_height = 0;
    if (!r || r->format != 32 || r->bytes_after || r->type == XCB_ATOM_NONE)
    {
        free(r);
        return 0;
    }
    values = xcb_get_property_value(r);
    count = xcb_get_property_value_length(r) / 4;
    while (offset < count)
    {
        uint32_t width, height;
        if (count - offset < 2) goto fail;
        width = values[offset];
        height = values[offset + 1];
        offset += 2;
        if (!width || !height || (uint64_t)width * height > (uint64_t)(count - offset)) goto fail;
        if (width <= MAX_SIDE && height <= MAX_SIDE)
        {
            long distance = labs((long)(width > height ? width : height) - side), size = (long)width * height;
            if (best_offset < 0 || distance < best_distance ||
                (distance == best_distance && (size > best_area || (size == best_area && offset < best_offset))))
            {
                best_distance = distance, best_area = size, best_offset = offset;
                best_width = width, best_height = height;
            }
        }
        offset += (long)width * height;
    }
    if (best_offset < 0) goto fail;
    out->width = best_width;
    out->height = best_height;
    out->depth = 32;
    out->pixels = xmalloc(sizeof(uint32_t) * best_width * best_height);
    memcpy(out->pixels, values + best_offset, sizeof(uint32_t) * best_width * best_height);
    free(r);
    return 1;
fail:
    free(r);
    return 0;
}

static int pixmap_pixels(xcb_pixmap_t pixmap, struct image *out)
{
    xcb_get_geometry_reply_t *g = get_geometry(pixmap);
    const xcb_format_t *format = NULL;
    xcb_get_image_reply_t *image;
    int width, height, depth, bpp, pad, stride;
    const uint8_t *data;
    if (!g) return 0;
    width = g->width, height = g->height, depth = g->depth;
    free(g);
    if (width <= 0 || width > MAX_SIDE || height <= 0 || height > MAX_SIDE) return 0;
    for (xcb_format_iterator_t i = xcb_setup_pixmap_formats_iterator(setup); i.rem; xcb_format_next(&i))
        if (i.data->depth == depth)
        {
            format = i.data;
            break;
        }
    if (!format) return 0;
    bpp = format->bits_per_pixel;
    pad = format->scanline_pad;
    if (bpp != 1 && bpp != 8 && bpp != 16 && bpp != 24 && bpp != 32) return 0;
    stride = ((width * bpp + pad - 1) / pad) * (pad / 8);
    image = REPLY(get_image, xcb_get_image(conn, XCB_IMAGE_FORMAT_Z_PIXMAP, pixmap, 0, 0, width, height, 0xffffffff));
    if (!image) return 0;
    if (xcb_get_image_data_length(image) < stride * height)
    {
        free(image);
        return 0;
    }
    data = xcb_get_image_data(image);
    out->width = width;
    out->height = height;
    out->depth = depth;
    out->pixels = xmalloc(sizeof(uint32_t) * width * height);
    for (int y = 0; y < height; y++)
        for (int x = 0; x < width; x++)
        {
            int offset = y * stride + (x * bpp / 8);
            uint32_t pixel = 0;
            if (bpp == 1)
            {
                int shift = setup->bitmap_format_bit_order == XCB_IMAGE_ORDER_LSB_FIRST ? x % 8 : 7 - x % 8;
                pixel = data[offset] >> shift & 1;
            }
            else
            {
                int bytes = bpp / 8;
                for (int k = 0; k < bytes; k++)
                {
                    int index = setup->image_byte_order == XCB_IMAGE_ORDER_LSB_FIRST ? bytes - 1 - k : k;
                    pixel = pixel << 8 | data[offset + index];
                }
            }
            out->pixels[y * width + x] = pixel;
        }
    free(image);
    return 1;
}

/* WM_HINTS as python-xlib reads them (exactly 9 values), or NULL */
static xcb_get_property_reply_t *wm_hints(xcb_window_t w, const uint32_t **hints)
{
    xcb_get_property_reply_t *r = get_property(w, XCB_ATOM_WM_HINTS, XCB_ATOM_WM_HINTS, 100);
    if (r && r->format == 32 && xcb_get_property_value_length(r) == 9 * 4)
    {
        *hints = xcb_get_property_value(r);
        return r;
    }
    free(r);
    *hints = NULL;
    return NULL;
}

static int hints_icon(xcb_window_t w, struct image *out)
{
    const uint32_t *hints;
    xcb_get_property_reply_t *r = wm_hints(w, &hints);
    struct image image = {0}, mask = {0};
    uint32_t icon_mask, flags;
    int ok = 0;
    if (!hints || !(hints[0] & ICON_PIXMAP_HINT))
    {
        free(r);
        return 0;
    }
    flags = hints[0];
    icon_mask = hints[7];
    ok = pixmap_pixels(hints[3], &image);
    free(r);
    if (!ok) return 0;
    if (flags & ICON_MASK_HINT)
    {
        if (!pixmap_pixels(icon_mask, &mask) || mask.width != image.width || mask.height != image.height ||
            mask.depth != 1)
            goto fail;
    }
    int count = image.width * image.height;
    if (image.depth == 1)
    {
        for (int i = 0; i < count; i++) image.pixels[i] = image.pixels[i] ? 0xffffffff : 0xff000000;
    }
    else
    {
        const xcb_visualtype_t *visual = NULL;
        for (xcb_depth_iterator_t d = xcb_screen_allowed_depths_iterator(screen); d.rem; xcb_depth_next(&d))
        {
            if (d.data->depth != image.depth) continue;
            for (xcb_visualtype_iterator_t v = xcb_depth_visuals_iterator(d.data); v.rem; xcb_visualtype_next(&v))
                if (v.data->_class == XCB_VISUAL_CLASS_TRUE_COLOR &&
                    (!visual || (v.data->visual_id == screen->root_visual && visual->visual_id != screen->root_visual)))
                    visual = v.data;
        }
        if (visual)
        {
            const uint32_t masks[3] = {visual->red_mask, visual->green_mask, visual->blue_mask};
            for (int k = 0; k < 3; k++)
                if (!(masks[k] & -masks[k])) goto fail;
            for (int i = 0; i < count; i++)
            {
                uint32_t pixel = image.pixels[i], color = 0xff000000;
                static const int shifts[3] = {16, 8, 0};
                for (int k = 0; k < 3; k++)
                {
                    uint64_t low = masks[k] & -masks[k];
                    color |= (uint32_t)((pixel & masks[k]) / low * 255 / (masks[k] / low)) << shifts[k];
                }
                image.pixels[i] = color;
            }
        }
        else
        {
            /* Indexed pixmaps use the client's colormap (the default when unset). */
            uint32_t unique[256];
            int unique_count = 0;
            for (int i = 0; i < count; i++)
            {
                int found = 0;
                for (int k = 0; k < unique_count && !found; k++) found = unique[k] == image.pixels[i];
                if (found) continue;
                if (unique_count == 256) goto fail;
                unique[unique_count++] = image.pixels[i];
            }
            xcb_get_window_attributes_reply_t *attributes =
                REPLY(get_window_attributes, xcb_get_window_attributes(conn, w));
            if (!attributes) goto fail;
            xcb_colormap_t colormap = attributes->colormap ? attributes->colormap : screen->default_colormap;
            free(attributes);
            xcb_query_colors_reply_t *colors =
                REPLY(query_colors, xcb_query_colors(conn, colormap, unique_count, unique));
            if (!colors || xcb_query_colors_colors_length(colors) < unique_count)
            {
                free(colors);
                goto fail;
            }
            xcb_rgb_t *rgb = xcb_query_colors_colors(colors);
            for (int i = 0; i < count; i++)
                for (int k = 0; k < unique_count; k++)
                    if (unique[k] == image.pixels[i])
                    {
                        image.pixels[i] = 0xff000000 | (uint32_t)(rgb[k].red >> 8) << 16 |
                                          (uint32_t)(rgb[k].green >> 8) << 8 | (rgb[k].blue >> 8);
                        break;
                    }
            free(colors);
        }
    }
    if (mask.pixels)
        for (int i = 0; i < count; i++)
            if (!mask.pixels[i]) image.pixels[i] = 0;
    free_image(&mask);
    *out = image;
    out->depth = 32;
    return 1;
fail:
    free_image(&image);
    free_image(&mask);
    return 0;
}

/* The image fitted in SIDE x SIDE by averaging its pixels (alpha
 * weighted), centred: a caption draws its small icon at that size, and
 * stretching it there would only keep some of the pixels. */
static void shrink(struct image *image, int side)
{
    int width = image->width, height = image->height;
    double scale = (double)side / (width > height ? width : height);
    int w = (int)nearbyint(width * scale), h = (int)nearbyint(height * scale);
    uint32_t *out = calloc(side * side, sizeof(uint32_t));
    if (!out) return;
    if (w < 1) w = 1;
    if (h < 1) h = 1;
    int left = (side - w) / 2, top = (side - h) / 2;
    for (int y = 0; y < h; y++)
    {
        int y0 = y * height / h, y1 = (y + 1) * height / h;
        if (y1 < y0 + 1) y1 = y0 + 1;
        for (int x = 0; x < w; x++)
        {
            int x0 = x * width / w, x1 = (x + 1) * width / w;
            uint64_t a = 0, r = 0, g = 0, b = 0;
            if (x1 < x0 + 1) x1 = x0 + 1;
            for (int sy = y0; sy < y1; sy++)
                for (int sx = x0; sx < x1 && sx < width; sx++)
                {
                    uint32_t p = image->pixels[sy * width + sx], alpha = p >> 24;
                    a += alpha;
                    r += (p >> 16 & 0xff) * alpha;
                    g += (p >> 8 & 0xff) * alpha;
                    b += (p & 0xff) * alpha;
                }
            uint64_t count = (uint64_t)(y1 - y0) * (x1 - x0);
            if (a)
                out[(top + y) * side + left + x] = (uint32_t)(a / count) << 24 | (uint32_t)(r / a) << 16 |
                                                   (uint32_t)(g / a) << 8 | (uint32_t)(b / a);
        }
    }
    free(image->pixels);
    image->pixels = out;
    image->width = image->height = side;
}

/* The icon nearest SIDE pixels; a small one (side 16) is shrunk to 16x16
 * when the client has none that small. HINTS caches the WM_HINTS icon
 * between calls for one window (*HINTS_STATE: 0 not read, 1 none, 2 read). */
static struct icon_data payload(xcb_window_t w, int side, struct image *hints, int *hints_state)
{
    struct image image = {0};
    struct icon_data result = {0};
    int have = net_icon(w, side, &image);
    if (!have)
    {
        if (!*hints_state) *hints_state = hints_icon(w, hints) ? 2 : 1;
        if (*hints_state == 2)
        {
            image = *hints;
            image.pixels = xmalloc(sizeof(uint32_t) * hints->width * hints->height);
            memcpy(image.pixels, hints->pixels, sizeof(uint32_t) * hints->width * hints->height);
            have = 1;
        }
    }
    if (!have) return result;
    if (side < 32 && (image.width > side || image.height > side)) shrink(&image, side);
    result.size = 12 + 4 * (size_t)image.width * image.height;
    result.data = xmalloc(result.size);
    memcpy(result.data, "W4I1", 4);
    for (int k = 0; k < 4; k++)
    {
        result.data[4 + k] = (uint32_t)image.width >> 8 * k;
        result.data[8 + k] = (uint32_t)image.height >> 8 * k;
    }
    for (long i = 0; i < (long)image.width * image.height; i++)
        for (int k = 0; k < 4; k++) result.data[12 + 4 * i + k] = image.pixels[i] >> 8 * k;
    free_image(&image);
    return result;
}

static int same_icon(const struct icon_data *a, const struct icon_data *b)
{
    if (!a->data || !b->data) return !a->data && !b->data;
    return a->size == b->size && !memcmp(a->data, b->data, a->size);
}

static void update_icons(struct client *c)
{
    /* The caption's 16x16 icon beside the big one; the proxy reads both when
     * the big file changes, so it goes last. */
    struct image hints = {0};
    int hints_state = 0;
    struct icon_data data[2];
    data[0] = payload(c->id, 16, &hints, &hints_state);
    data[1] = payload(c->id, 32, &hints, &hints_state);
    free_image(&hints);
    if (!c->has_icons || !same_icon(&data[0], &c->icons[0]) || !same_icon(&data[1], &c->icons[1]))
    {
        for (int small = 1; small >= 0; small--)
        {
            char name[64], temporary[64];
            struct icon_data *icon = &data[small ? 0 : 1];
            icon_name(name, sizeof(name), c->id, small, "argb");
            icon_name(temporary, sizeof(temporary), c->id, small, "tmp");
            if (icon->data && data[1].data) write_ipc(temporary, name, icon->data, icon->size);
            else unlink_ipc(name);
        }
        free_icons(c);
        c->icons[0] = data[0];
        c->icons[1] = data[1];
        c->has_icons = 1;
    }
    else
    {
        free(data[0].data);
        free(data[1].data);
    }
}

static void snapshot(void)
{
    struct text lines = {0}, owners = {0};
    xcb_window_t focused = input_focus(), focused_id;
    /* An XP menu can take focus without changing the EWMH active client.
     * Do not keep stealing Win32 foreground from that menu back to a proxy. */
    while (focused > XCB_INPUT_FOCUS_POINTER_ROOT && focused != root && !find_client(focused))
        focused = parent_of(focused);
    focused_id = focused > XCB_INPUT_FOCUS_POINTER_ROOT ? focused : 0;
    text_add(&lines, "", 0);
    text_add(&owners, "", 0);
    for (int i = 0; i < client_count; i++)
    {
        struct client *c = clients[i];
        if (c->kind != KIND_NORMAL || c->hwnd) continue;
        /* Owned windows (dialogs) get a frame too, owned by their owner's
         * frame as in XP, so without a taskbar button. */
        if (c->owner) text_printf(&owners, "%x\t%x\n", c->id, c->owner);
        if (c->icon_dirty)
        {
            update_icons(c);
            c->icon_dirty = 0;
        }
        text_printf(&lines, "%x\t%d\t%d\t%s\n", c->id, c->hidden, c->id == active && c->id == focused_id, c->title);
    }
    /* Before windows.tsv, which the proxy reads it with. */
    if (!same_text(owners.data, previous_owners))
    {
        write_ipc("owners.tmp", "owners.tsv", owners.data, owners.length);
        replace_text(&previous_owners, owners.data);
    }
    if (!same_text(lines.data, previous_snapshot))
    {
        write_ipc("windows.tmp", "windows.tsv", lines.data, lines.length);
        replace_text(&previous_snapshot, lines.data);
    }
    free(lines.data);
    free(owners.data);
}

/* Python's int(TEXT, BASE) for one whitespace-free field */
static int parse_int(const char *text, int base, long long *out)
{
    char *end;
    if (!*text) return 0;
    errno = 0;
    *out = strtoll(text, &end, base);
    return !errno && !*end;
}

static char *read_ipc(const char *name)
{
    char path[PATH_MAX];
    struct text t = {0};
    char buffer[4096];
    int fd;
    ssize_t n;
    ipc_path(path, sizeof(path), name);
    if ((fd = open(path, O_RDONLY | O_CLOEXEC)) < 0) return NULL;
    text_add(&t, "", 0);
    while ((n = read(fd, buffer, sizeof(buffer))) > 0) text_add(&t, buffer, n);
    close(fd);
    if (n < 0)
    {
        free(t.data);
        return NULL;
    }
    return t.data;
}

/* Splits TEXT into whitespace-separated fields (in place); returns their number. */
static int split(char *text, char **fields, int max)
{
    int n = 0;
    char *save, *field;
    for (field = strtok_r(text, " \t\r\v\f", &save); field; field = strtok_r(NULL, " \t\r\v\f", &save))
        if (n < max) fields[n++] = field;
        else n++;
    return n;
}

struct frame_line
{
    long long handle, native;
    int has_extents, extents[5];
    int has_ready, ready;
    int has_clip, clip[2];
};

/* frames.tsv: "HWND NATIVE [LEFT RIGHT TOP BOTTOM MAXIMIZED [HIDDEN [CLIP_X CLIP_Y]]]";
 * any bad line voids them all, as in wm.py. */
static int read_frames(struct frame_line **out)
{
    char *text = read_ipc("frames.tsv"), *line, *save;
    struct frame_line *lines = NULL;
    int count = 0, capacity = 0;
    *out = NULL;
    if (!text) return 0;
    /* str.splitlines(): \n, \r\n and \r end lines; the last may lack one */
    for (char *p = text; *p; p++)
        if (*p == '\r') *p = p[1] == '\n' ? ' ' : '\n';
    size_t length = strlen(text);
    if (length && text[length - 1] == '\n') text[length - 1] = 0;
    if (!*text) goto done;
    for (line = text;; line = save + 1)
    {
        char *fields[16];
        int n;
        struct frame_line f = {0};
        long long value;
        save = strchr(line, '\n');
        if (save) *save = 0;
        n = split(line, fields, 16);
        if (n < 2 || !parse_int(fields[0], 16, &f.handle) || !parse_int(fields[1], 16, &f.native)) goto bad;
        if (n >= 7)
        {
            for (int k = 0; k < 5; k++)
            {
                if (!parse_int(fields[2 + k], 10, &value)) goto bad;
                f.extents[k] = (int)value;
            }
            f.has_extents = 1;
        }
        if (n >= 8)
        {
            if (!parse_int(fields[7], 10, &value)) goto bad;
            f.has_ready = 1;
            f.ready = !value;
        }
        if (n == 10)
        {
            for (int k = 0; k < 2; k++)
            {
                if (!parse_int(fields[8 + k], 10, &value)) goto bad;
                f.clip[k] = (int)value;
            }
            f.has_clip = 1;
        }
        GROW(lines, count, capacity);
        lines[count++] = f;
        if (!save) break;
    }
done:
    free(text);
    *out = lines;
    return count;
bad:
    free(text);
    free(lines);
    return 0;
}

/* The last line in LINES matching KEY in the given field, as a dict keeps the last */
static const struct frame_line *frame_by_handle(const struct frame_line *lines, int count, uint32_t handle)
{
    const struct frame_line *found = NULL;
    for (int i = 0; i < count; i++)
        if ((uint64_t)lines[i].handle == handle) found = &lines[i];
    return found;
}

static const struct frame_line *frame_by_native(const struct frame_line *lines, int count, xcb_window_t native,
                                                int what)
{
    const struct frame_line *found = NULL;
    for (int i = 0; i < count; i++)
        if ((uint64_t)lines[i].native == native &&
            (what == 0 ? lines[i].has_extents : what == 1 ? lines[i].has_ready : lines[i].has_clip))
            found = &lines[i];
    return found;
}

static void sync_frames(void)
{
    struct frame_line *lines;
    int line_count = read_frames(&lines);
    int new_count = 0, changed;
    struct { xcb_window_t frame, native; } *found = xmalloc(sizeof(*found) * (client_count + 1));
    for (int i = 0; i < client_count; i++)
    {
        struct client *c = clients[i], *native = NULL;
        const struct frame_line *line;
        if (c->kind != KIND_PROXY) continue;
        if (c->hwnd && (line = frame_by_handle(lines, line_count, c->hwnd)))
            native = find_client((xcb_window_t)line->native);
        if (native && native->kind == KIND_NORMAL && !native->hwnd)
        {
            const struct frame_line *metrics = frame_by_native(lines, line_count, native->id, 0);
            const struct frame_line *ready = frame_by_native(lines, line_count, native->id, 1);
            found[new_count].frame = c->id;
            found[new_count++].native = native->id;
            if (metrics)
            {
                int left = metrics->extents[0], right = metrics->extents[1], top = metrics->extents[2];
                int bottom = metrics->extents[3], maximized = metrics->extents[4];
                if (!native->framed)
                {
                    int nx = native->geometry[0], ny = native->geometry[1];
                    int nwidth = native->geometry[2], nheight = native->geometry[3], width, height, x, y;
                    fit(native, left + right, top + bottom, &width, &height);
                    if (width != nwidth || height != nheight)
                    {
                        /* Too large for the work area with its frame:
                         * shrink it, centred, as other window managers do. */
                        int spare_x = area[2] - left - right - width, spare_y = area[3] - top - bottom - height;
                        x = area[0] + left + (spare_x > 0 ? spare_x / 2 : 0);
                        y = area[1] + top + (spare_y > 0 ? spare_y / 2 : 0);
                    }
                    else if (native->owner || positioned(native))
                    {
                        /* Keep an explicit position (a dialog's: centred on
                         * its owner), but keep the first caption visible
                         * above content placed at 0,0. */
                        x = nx > area[0] + left ? nx : area[0] + left;
                        y = ny > area[1] + top ? ny : area[1] + top;
                    }
                    else cascade(left, top, &x, &y);
                    if (native->fullscreen)
                    {
                        /* Mapped in full screen: placed so for when it leaves it. */
                        int placed[4] = {x, y, width, height};
                        memcpy(native->fullscreen_restore, placed, sizeof(placed));
                    }
                    else if (x != nx || y != ny || width != nwidth || height != nheight)
                        configure_rect(native, x, y, width, height);
                    native->framed = 1;
                }
                int extents[4] = {left, right, top, bottom};
                if (!native->has_extents || memcmp(native->extents, extents, sizeof(extents)))
                {
                    memcpy(native->extents, extents, sizeof(extents));
                    native->has_extents = 1;
                    set_property(native->id, A._NET_FRAME_EXTENTS, XCB_ATOM_CARDINAL, (const uint32_t *)extents, 4);
                }
                if (maximized && native->has_restore)
                {
                    const struct frame_line *clip = frame_by_native(lines, line_count, native->id, 2);
                    int bx = clip ? clip->clip[0] : 0, by = clip ? clip->clip[1] : 0;
                    int wanted[4] = {area[0] + left - bx, area[1] + top - by,
                                     area[2] - left - right + 2 * bx, area[3] - top - bottom + 2 * by};
                    if (wanted[2] < 1) wanted[2] = 1;
                    if (wanted[3] < 1) wanted[3] = 1;
                    if (native->fullscreen) memcpy(native->fullscreen_restore, wanted, sizeof(wanted));
                    else if (memcmp(native->geometry, wanted, sizeof(wanted)))
                        configure_rect(native, wanted[0], wanted[1], wanted[2], wanted[3]);
                }
            }
            if (native->geometry[4])
            {
                struct values values = {0};
                set_value(&values, XCB_CONFIG_WINDOW_BORDER_WIDTH, 0);
                configure(native, &values);
            }
            if (native->hidden || (ready && !ready->ready)) xcb_unmap_window(conn, c->id);
            else xcb_map_window(conn, c->id);
        }
        else xcb_unmap_window(conn, c->id);
    }
    free(lines);
    /* A dict comparison: the same pairs, in any order */
    changed = new_count != frame_count;
    for (int i = 0; !changed && i < new_count; i++)
        changed = frame_native(found[i].frame) != found[i].native;
    if (changed)
    {
        frame_count = 0;
        for (int i = 0; i < new_count; i++)
        {
            GROW(frames, frame_count, frame_capacity);
            frames[frame_count].frame = found[i].frame;
            frames[frame_count++].native = found[i].native;
        }
        restack();
    }
    free(found);
    write_geometry();
}

/* Keep a frame's pixels off screen while it moves, not while it changes size.
 *
 * Its client moves first (follow_frame) and covers the frame's leading
 * edge for a moment; the frame's move then cannot copy that edge, and
 * it stayed black until Wine painted it again, about 5 ms in each step
 * of a drag (wide frames such as Windows 7's showed it). A redirected
 * frame moves from its off-screen image. Redirected, each size change
 * would cost a new image, so a size change ends it; the switch costs
 * one repaint, without black. */
static void frame_moves(struct client *frame, const struct values *values)
{
    int x = frame->geometry[0], y = frame->geometry[1], width = frame->geometry[2], height = frame->geometry[3];
    int resized = value_or(values, XCB_CONFIG_WINDOW_WIDTH, width) != width ||
                  value_or(values, XCB_CONFIG_WINDOW_HEIGHT, height) != height;
    if (resized || value_or(values, XCB_CONFIG_WINDOW_X, x) != x || value_or(values, XCB_CONFIG_WINDOW_Y, y) != y)
    {
        frame->moving = !resized;
        update_redirect(frame);
    }
}

/* Move a native client with its frame, in the same pass.
 *
 * Dragging or sizing a frame moves the Wine window; the proxy then asks
 * for the client's new geometry, one round trip through Wine and the
 * IPC directory later. Meanwhile the frame's empty client area showed
 * beside the client. The client's rectangle follows from the frame's by
 * the measured border extents, so both move in the same flush; the
 * proxy's later commands for these rectangles are then stale. The
 * client moves first: where it already covers the frame's client
 * area, the frame's move exposes nothing that Wine must paint. */
static void follow_frame(xcb_window_t frame_id, const struct values *values)
{
    struct client *native = find_client(frame_native(frame_id)), *frame = find_client(frame_id);
    if (!native || !frame || native->hidden || native->has_restore || native->fullscreen || !native->has_extents)
        return;
    int left = native->extents[0], right = native->extents[1], top = native->extents[2], bottom = native->extents[3];
    int x = value_or(values, XCB_CONFIG_WINDOW_X, frame->geometry[0]);
    int y = value_or(values, XCB_CONFIG_WINDOW_Y, frame->geometry[1]);
    int width = value_or(values, XCB_CONFIG_WINDOW_WIDTH, frame->geometry[2]);
    int height = value_or(values, XCB_CONFIG_WINDOW_HEIGHT, frame->geometry[3]);
    if (width <= left + right || height <= top + bottom) return;
    int wanted[4] = {x + left, y + top, width - left - right, height - top - bottom};
    if (memcmp(native->geometry, wanted, sizeof(wanted)))
    {
        configure_rect(native, wanted[0], wanted[1], wanted[2], wanted[3]);
        add_followed(native, wanted);
    }
}

/* Move a frame with its native client, the other way round.
 *
 * A client that moves itself (GTK drags its header bar so without
 * _NET_WM_MOVERESIZE) waited for the proxy to move the frame. Wine
 * takes the frame's new position from the X server as from any WM. */
static void follow_native(xcb_window_t native_id)
{
    struct client *native = find_client(native_id), *frame;
    xcb_window_t frame_id = native_frame(native_id);
    if (!native || !frame_id || !(frame = find_client(frame_id))) return;
    if (native->hidden || native->has_restore || native->fullscreen || !native->has_extents) return;
    int left = native->extents[0], right = native->extents[1], top = native->extents[2], bottom = native->extents[3];
    int wanted[4] = {native->geometry[0] - left, native->geometry[1] - top, native->geometry[2] + left + right,
                     native->geometry[3] + top + bottom};
    if (memcmp(frame->geometry, wanted, sizeof(wanted)))
    {
        configure_rect(frame, wanted[0], wanted[1], wanted[2], wanted[3]);
        add_followed(native, native->geometry);
    }
}

/* PID-SEQUENCE.cmd: in the order each proxy wrote them (9 before 10). */
struct command_file
{
    char *path;
    long long pid, sequence;
    int index;
};

static int command_order(const void *a, const void *b)
{
    const struct command_file *x = a, *y = b;
    if (x->pid != y->pid) return x->pid < y->pid ? -1 : 1;
    if (x->sequence != y->sequence) return x->sequence < y->sequence ? -1 : 1;
    return x->index - y->index;
}

static int all_digits(const char *s, size_t n)
{
    if (!n) return 0;
    for (size_t i = 0; i < n; i++)
        if (s[i] < '0' || s[i] > '9') return 0;
    return 1;
}

static void commands(void)
{
    char pattern[PATH_MAX];
    glob_t found;
    struct command_file *files;
    ipc_path(pattern, sizeof(pattern), "*.cmd");
    if (glob(pattern, GLOB_NOSORT, NULL, &found)) return;
    files = xmalloc(sizeof(*files) * (found.gl_pathc + 1));
    for (size_t i = 0; i < found.gl_pathc; i++)
    {
        const char *name = strrchr(found.gl_pathv[i], '/') ? strrchr(found.gl_pathv[i], '/') + 1 : found.gl_pathv[i];
        size_t stem = strlen(name) - 4;
        const char *dash = memchr(name, '-', stem);
        files[i].path = found.gl_pathv[i];
        files[i].index = i;
        files[i].pid = files[i].sequence = 0;
        if (dash && all_digits(name, dash - name) && all_digits(dash + 1, name + stem - dash - 1))
        {
            files[i].pid = strtoll(name, NULL, 10);
            files[i].sequence = strtoll(dash + 1, NULL, 10);
        }
    }
    qsort(files, found.gl_pathc, sizeof(*files), command_order);
    for (size_t i = 0; i < found.gl_pathc; i++)
    {
        char *text = NULL, *fields[8];
        int fd = open(files[i].path, O_RDONLY | O_CLOEXEC);
        if (fd >= 0)
        {
            struct text t = {0};
            char buffer[512];
            ssize_t n;
            text_add(&t, "", 0);
            while ((n = read(fd, buffer, sizeof(buffer))) > 0) text_add(&t, buffer, n);
            close(fd);
            text = t.data;
        }
        unlink(files[i].path);
        if (!text) continue;
        for (char *p = text; *p; p++)
            if (*p == '\n') *p = ' ';
        int n = split(text, fields, 8);
        long long value;
        if (n < 2 || !parse_int(fields[1], 16, &value))
        {
            free(text);
            continue;
        }
        const char *op = fields[0];
        struct client *c = find_client((xcb_window_t)value);
        if (value < 0 || value > UINT32_MAX) c = NULL;
        if (!strcmp(op, "switch") && c && c->kind == KIND_NORMAL)
        {
            /* Alt+Tab chose this window, which may also be a Wine one. */
            focus(c->id);
        }
        else if (c && c->kind == KIND_NORMAL && !c->hwnd)
        {
            if (!strcmp(op, "geometry") && n == 6 && !c->fullscreen)
            {
                long long g[4];
                int ok = 1;
                for (int k = 0; k < 4; k++) ok &= parse_int(fields[2 + k], 10, &g[k]);
                if (ok)
                {
                    int wanted[4] = {(int)g[0], (int)g[1], (int)g[2], (int)g[3]};
                    /* Rectangles that follow_frame has already applied
                     * arrive late; a newer one may be in place by then. */
                    if (wanted[2] > 0 && wanted[3] > 0 && memcmp(c->geometry, wanted, sizeof(wanted)) &&
                        !was_followed(c, wanted))
                    {
                        configure_rect(c, wanted[0], wanted[1], wanted[2], wanted[3]);
                        c->followed_set = 0;
                        c->followed_count = 0;
                    }
                }
            }
            else if (!strcmp(op, "restore")) maximize(c->id, 1);
            else if (!strcmp(op, "activate")) focus(c->id);
            else if (!strcmp(op, "minimize")) minimize(c->id);
            else if (!strcmp(op, "close")) close_window(c->id);
            else if (!strcmp(op, "maximize")) maximize(c->id, 0);
        }
        free(text);
    }
    free(files);
    globfree(&found);
}

/* ---- Events ------------------------------------------------------------- */

/* What the proxy's files may need after event E: 0 nothing, 1 the native
 * geometry only, 2 a whole pass.
 *
 * Each step of a window drag brings a ConfigureRequest and a
 * ConfigureNotify. Only those of native clients, the taskbar and the
 * root change what the proxy reads, and mostly just geometry.tsv; a
 * whole pass for each of them made the WM a large part of a drag. */
static int changes_bridge(xcb_generic_event_t *e)
{
    uint8_t type = e->response_type & 0x7f;
    xcb_window_t window;
    struct client *c;
    if (type == XCB_MOTION_NOTIFY || type == XCB_BUTTON_RELEASE) return 0;
    /* A native client's request is published at once, from the cache,
     * without waiting for its notify. */
    if (type == XCB_CONFIGURE_REQUEST || type == XCB_CONFIGURE_NOTIFY)
    {
        window = type == XCB_CONFIGURE_REQUEST ? ((xcb_configure_request_event_t *)e)->window
                                               : ((xcb_configure_notify_event_t *)e)->window;
        if (window != root)
        {
            c = find_client(window);
            if (!c || c->kind == KIND_PROXY || (c->hwnd && c->kind != KIND_DOCK)) return 0;
            /* A maximized client is kept fitted to the work area by sync_frames. */
            return c->kind == KIND_NORMAL && !c->has_restore ? 1 : 2;
        }
    }
    return 2;
}

static void handle_event(xcb_generic_event_t *ev)
{
    uint8_t type = ev->response_type & 0x7f;
    int send_event = ev->response_type & 0x80;
    switch (type)
    {
    case XCB_MAP_REQUEST:
        manage(((xcb_map_request_event_t *)ev)->window);
        break;
    case XCB_MAP_NOTIFY:
    {
        xcb_map_notify_event_t *e = (void *)ev;
        struct client *c;
        if (e->override_redirect) unmanaged_add(e->window);
        if (e->override_redirect)
        {
            xcb_get_property_reply_t *r =
                get_property(e->window, A._WIN4WINE_LOCK, XCB_GET_PROPERTY_TYPE_ANY, 1);
            if (r && r->type != XCB_ATOM_NONE) lock_window = e->window;
            free(r);
        }
        keep_lock();
        c = find_client(e->window);
        if (c && (c->kind == KIND_POPUP || (c->kind == KIND_NORMAL && active == e->window)))
        {
            /* Wine defers WM_TAKE_FOCUS while waiting for the initial map acknowledgement. */
            if (has_atom(e->window, A.WM_PROTOCOLS, A.WM_TAKE_FOCUS)) take_focus_message(e->window, XCB_CURRENT_TIME);
        }
        break;
    }
    case XCB_CONFIGURE_REQUEST:
    {
        xcb_configure_request_event_t *e = (void *)ev;
        struct values values = {0};
        struct client *c = find_client(e->window);
        if (e->value_mask & XCB_CONFIG_WINDOW_X) set_value(&values, XCB_CONFIG_WINDOW_X, e->x);
        if (e->value_mask & XCB_CONFIG_WINDOW_Y) set_value(&values, XCB_CONFIG_WINDOW_Y, e->y);
        if (e->value_mask & XCB_CONFIG_WINDOW_WIDTH) set_value(&values, XCB_CONFIG_WINDOW_WIDTH, e->width);
        if (e->value_mask & XCB_CONFIG_WINDOW_HEIGHT) set_value(&values, XCB_CONFIG_WINDOW_HEIGHT, e->height);
        if (e->value_mask & XCB_CONFIG_WINDOW_BORDER_WIDTH)
            set_value(&values, XCB_CONFIG_WINDOW_BORDER_WIDTH, e->border_width);
        if (e->value_mask & XCB_CONFIG_WINDOW_SIBLING) set_value(&values, XCB_CONFIG_WINDOW_SIBLING, e->sibling);
        if (e->value_mask & XCB_CONFIG_WINDOW_STACK_MODE)
            set_value(&values, XCB_CONFIG_WINDOW_STACK_MODE, e->stack_mode);
        if (c && c->fullscreen)
        {
            /* A client in full screen keeps covering the screen; it is told so. */
            values.mask &= XCB_CONFIG_WINDOW_SIBLING | XCB_CONFIG_WINDOW_STACK_MODE;
            configure(c, &values);
        }
        else if (c)
        {
            int moves = e->value_mask & (XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y | XCB_CONFIG_WINDOW_WIDTH |
                                         XCB_CONFIG_WINDOW_HEIGHT);
            if (moves && is_frame(e->window))
            {
                frame_moves(c, &values);
                follow_frame(e->window, &values);
            }
            configure(c, &values);
            if (moves && c->kind == KIND_NORMAL && !c->hwnd) follow_native(e->window);
        }
        else configure_window(e->window, &values);
        if (e->value_mask & (XCB_CONFIG_WINDOW_SIBLING | XCB_CONFIG_WINDOW_STACK_MODE)) restack();
        if (c && (c = find_client(e->window)) && c->kind == KIND_DOCK) workarea();
        break;
    }
    case XCB_DESTROY_NOTIFY:
    {
        xcb_destroy_notify_event_t *e = (void *)ev;
        if (lock_window && e->window == lock_window) lock_gone();
        unmanage(e->window);
        break;
    }
    case XCB_UNMAP_NOTIFY:
    {
        xcb_unmap_notify_event_t *e = (void *)ev;
        struct client *c;
        list_remove(unmanaged, &unmanaged_count, e->window);
        if (lock_window && e->window == lock_window) lock_gone();
        c = find_client(e->window);
        /* A minimized client that withdraws itself sends only the synthetic
         * UnmapNotify (XWithdrawWindow); Wine 11 restores a minimized window
         * this way and maps it again once WM_STATE is gone. */
        int withdrawn = c && c->hidden && send_event && c->kind == KIND_NORMAL;
        if (c && (!c->hidden || withdrawn) && c->kind != KIND_PROXY)
        {
            /* ICCCM withdrawal acknowledgement. Wine waits for PropertyDelete
             * (WithdrawnState) before issuing a subsequent XMapWindow. Keep
             * iconic clients and deliberately hidden proxy frames registered. */
            xcb_delete_property(conn, e->window, A.WM_STATE);
            unmanage(e->window);
        }
        break;
    }
    case XCB_PROPERTY_NOTIFY:
    {
        xcb_property_notify_event_t *e = (void *)ev;
        struct client *c = find_client(e->window);
        if (!c) break;
        if (e->atom == A._NET_WM_ICON || e->atom == XCB_ATOM_WM_HINTS) c->icon_dirty = 1;
        refresh(c, e->atom ? e->atom : (xcb_atom_t)-1);
        if (e->atom == A._WIN4WINE_CLASS || e->atom == A._NET_WM_WINDOW_TYPE) c->kind = window_kind(e->window);
        if (e->atom == A._WIN4WINE_CLASS || e->atom == A._WIN4WINE_HWND) update_redirect(c);
        if (c->kind == KIND_PROXY && !is_frame(e->window)) xcb_unmap_window(conn, e->window);
        if (c->kind == KIND_DOCK || e->atom == A._WIN4WINE_CLASS || e->atom == A._NET_WM_WINDOW_TYPE) workarea();
        break;
    }
    case XCB_BUTTON_PRESS:
    {
        xcb_button_press_event_t *e = (void *)ev;
        if (e->state & XCB_MOD_MASK_1 && e->event == root && e->child)
        {
            struct client *child = find_client(e->child);
            if (child && child->kind == KIND_NORMAL)
            {
                focus(e->child);
                xcb_get_geometry_reply_t *g = get_geometry(e->child);
                if (g)
                {
                    drag.active = 1;
                    drag.window = e->child;
                    drag.button = e->detail;
                    drag.px = e->root_x;
                    drag.py = e->root_y;
                    drag.x = g->x;
                    drag.y = g->y;
                    drag.width = g->width;
                    drag.height = g->height;
                    free(g);
                }
            }
        }
        else
        {
            struct client *c = find_client(e->event);
            if (c && (c->kind == KIND_DESKTOP || c->kind == KIND_DOCK)) focus_shell(e->event, e->time);
            else focus(e->event);
            xcb_allow_events(conn, XCB_ALLOW_REPLAY_POINTER, XCB_CURRENT_TIME);
        }
        break;
    }
    case XCB_MOTION_NOTIFY:
    {
        xcb_motion_notify_event_t *e = (void *)ev;
        if (!drag.active) break;
        int dx = e->root_x - drag.px, dy = e->root_y - drag.py;
        struct values values = {0};
        if (drag.button == 1)
        {
            set_value(&values, XCB_CONFIG_WINDOW_X, drag.x + dx);
            set_value(&values, XCB_CONFIG_WINDOW_Y, drag.y + dy);
        }
        else
        {
            set_value(&values, XCB_CONFIG_WINDOW_WIDTH, drag.width + dx > 64 ? drag.width + dx : 64);
            set_value(&values, XCB_CONFIG_WINDOW_HEIGHT, drag.height + dy > 32 ? drag.height + dy : 32);
        }
        configure_window(drag.window, &values);
        break;
    }
    case XCB_BUTTON_RELEASE:
        drag.active = 0;
        break;
    case XCB_KEY_PRESS:
    case XCB_KEY_RELEASE:
    {
        xcb_key_press_event_t *e = (void *)ev;
        const char *script = type == XCB_KEY_PRESS ? volume_script(e->detail) : NULL;
        if (script) volume_key(script);
        else if (shell_combo_active)
        {
            /* The WM holds the keyboard until the Windows key is released;
             * repeated presses of that key are ignored there. */
            shell_combo_event(type, e->detail, e->state);
        }
        else if (type == XCB_KEY_PRESS &&
                 (is_super(e->detail) || (e->detail == escape_code && e->state & XCB_MOD_MASK_CONTROL)))
            shell_key(e);
        else if (type == XCB_KEY_PRESS && e->state & XCB_MOD_MASK_4 && is_lock_key(e->detail, e->state))
        {
            /* Wine gets the key too (and drops it, patch 0159): it saw the
             * Windows key go down, and would open the Start menu for it alone. */
            request_lock();
            xcb_allow_events(conn, XCB_ALLOW_REPLAY_KEYBOARD, e->time);
        }
        else if (switching) switch_event(type, e->detail, e->state);
        else if (type == XCB_KEY_PRESS && e->detail == f4_code)
        {
            if (find_client(active)) close_window(active);
        }
        else if (type == XCB_KEY_PRESS && e->detail == tab_code) switch_key(e->state & XCB_MOD_MASK_SHIFT);
        break;
    }
    case XCB_FOCUS_IN:
    {
        xcb_focus_in_event_t *e = (void *)ev;
        struct client *c = find_client(e->event);
        if (e->mode == NOTIFY_NORMAL && e->detail != NOTIFY_POINTER && e->detail != NOTIFY_POINTER_ROOT &&
            e->detail != NOTIFY_DETAIL_NONE && c)
        {
            /* A FocusIn can arrive after the focus has moved on (an Alt+Tab
             * right after a window was focused); it must not take back the
             * active window. */
            if (c->kind == KIND_NORMAL && focused_client() == e->event) set_active(e->event);
        }
        break;
    }
    case XCB_CONFIGURE_NOTIFY:
    {
        xcb_configure_notify_event_t *e = (void *)ev;
        struct client *c;
        if (e->window == root)
        {
            workarea();
            for (int i = 0; i < client_count; i++)
                if (clients[i]->fullscreen) fit_fullscreen(clients[i]);
        }
        else if (!send_event && (c = find_client(e->window)))
        {
            c->geometry[0] = e->x;
            c->geometry[1] = e->y;
            c->geometry[2] = e->width;
            c->geometry[3] = e->height;
            c->geometry[4] = e->border_width;
            if (!c->has_size || c->size[0] != e->width || c->size[1] != e->height)
            {
                c->has_size = 1;
                c->size[0] = e->width;
                c->size[1] = e->height;
                if (c->kind == KIND_NORMAL && !c->hidden) redraw_native(e->window);
            }
        }
        /* A window put above the lock screen or the screen saver over it, or
         * a screen saver that now covers the screen. */
        if (lock_window && e->window != lock_window && e->window != root &&
            (e->above_sibling == lock_window || e->above_sibling == lock_top ||
             ((c = find_client(e->window)) && is_screen_saver(c))))
            keep_lock();
        break;
    }
    case XCB_CLIENT_MESSAGE:
    {
        xcb_client_message_event_t *e = (void *)ev;
        const uint32_t *values = e->data.data32;
        struct client *c = find_client(e->window);
        if (e->type == A._NET_ACTIVE_WINDOW) focus(e->window);
        else if (e->type == A._NET_CLOSE_WINDOW) close_window(e->window);
        else if (e->type == A.WM_CHANGE_STATE && values[0] == 3)
        {
            if (c) minimize(e->window);
        }
        else if (e->type == A._NET_WM_STATE && c)
        {
            uint32_t *current;
            int count, n = 0, maximize_change = 0;
            xcb_get_property_reply_t *r = prop32(e->window, A._NET_WM_STATE, &current, &count);
            uint32_t *states = xmalloc(sizeof(*states) * (count + 3));
            for (int i = 0; i < count; i++)
            {
                int dup = 0;
                for (int k = 0; k < n; k++) dup |= states[k] == current[i];
                if (!dup) states[n++] = current[i];
            }
            free(r);
            for (int k = 1; k < 3; k++)
            {
                uint32_t atom = values[k];
                int present = 0, at = -1;
                if (!atom) continue;
                for (int i = 0; i < n; i++)
                    if (states[i] == atom) present = 1, at = i;
                if (values[0] == 1 || (values[0] == 2 && !present))
                {
                    if (!present) states[n++] = atom;
                }
                else if (present)
                {
                    memmove(states + at, states + at + 1, sizeof(*states) * (n - at - 1));
                    n--;
                }
            }
            set_property(e->window, A._NET_WM_STATE, XCB_ATOM_ATOM, states, n);
            if (values[1] == A._NET_WM_STATE_FULLSCREEN || values[2] == A._NET_WM_STATE_FULLSCREEN)
            {
                int fullscreen = 0;
                for (int i = 0; i < n; i++) fullscreen |= states[i] == A._NET_WM_STATE_FULLSCREEN;
                set_fullscreen(c, fullscreen);
                restack();
            }
            if (values[1] == A._NET_WM_STATE_ABOVE || values[2] == A._NET_WM_STATE_ABOVE) restack();
            for (int k = 1; k < 3; k++)
                maximize_change |= values[k] == A._NET_WM_STATE_MAXIMIZED_VERT ||
                                   values[k] == A._NET_WM_STATE_MAXIMIZED_HORZ;
            if (maximize_change && (c = find_client(e->window)))
            {
                int maximized = 0;
                for (int i = 0; i < n; i++)
                    maximized |= states[i] == A._NET_WM_STATE_MAXIMIZED_VERT ||
                                 states[i] == A._NET_WM_STATE_MAXIMIZED_HORZ;
                if (c->kind == KIND_PROXY)
                {
                    struct client *native = find_client(frame_native(e->window));
                    if (native && native->has_restore != maximized) maximize(native->id, !maximized);
                }
                else maximize(e->window, !maximized);
            }
            free(states);
        }
        break;
    }
    }
}

/* ---- Startup and the main loop ------------------------------------------ */

/* Hide GTK header-bar window buttons; the Win32 frame already has them.
 *
 * Only claims an unowned XSETTINGS selection, so an existing settings
 * daemon keeps control. GTK falls back to its defaults for other keys. */
static void own_xsettings(void)
{
    char name[32];
    xcb_atom_t selection;
    xcb_get_selection_owner_reply_t *owner;
    static const uint8_t settings[] = {
        0, 0, 0, 0,                   /* byte order (LSB first), padding */
        0, 0, 0, 0,                   /* serial */
        1, 0, 0, 0,                   /* one setting */
        1, 0, 20, 0,                  /* a string named ... (20 bytes) */
        'G', 't', 'k', '/', 'D', 'e', 'c', 'o', 'r', 'a', 't', 'i', 'o', 'n', 'L', 'a', 'y', 'o', 'u', 't',
        0, 0, 0, 0,                   /* its serial */
        1, 0, 0, 0,                   /* value length */
        ':', 0, 0, 0,
    };
    snprintf(name, sizeof(name), "_XSETTINGS_S%d", screen_number);
    selection = intern(name);
    owner = REPLY(get_selection_owner, xcb_get_selection_owner(conn, selection));
    if (!owner || owner->owner != XCB_NONE)
    {
        free(owner);
        return;
    }
    free(owner);
    xcb_change_property(conn, XCB_PROP_MODE_REPLACE, support, A._XSETTINGS_SETTINGS, A._XSETTINGS_SETTINGS, 8,
                        sizeof(settings), settings);
    xcb_set_selection_owner(conn, support, selection, XCB_CURRENT_TIME);
    owner = REPLY(get_selection_owner, xcb_get_selection_owner(conn, selection));
    if (!owner || owner->owner != support)
    {
        free(owner);
        return;
    }
    free(owner);
    xcb_client_message_event_t e = {0};
    e.response_type = XCB_CLIENT_MESSAGE;
    e.format = 32;
    e.window = root;
    e.type = A.MANAGER;
    e.data.data32[0] = XCB_CURRENT_TIME;
    e.data.data32[1] = selection;
    e.data.data32[2] = support;
    send_event(root, XCB_EVENT_MASK_STRUCTURE_NOTIFY, &e, sizeof(e));
    xcb_flush(conn);
}

static void fail(const char *message)
{
    fprintf(stderr, "%s\n", message);
    exit(1);
}

static void mkdirs(const char *path)
{
    char buffer[PATH_MAX];
    snprintf(buffer, sizeof(buffer), "%s", path);
    for (char *p = buffer + 1; *p; p++)
        if (*p == '/')
        {
            *p = 0;
            mkdir(buffer, 0777);
            *p = '/';
        }
    if (mkdir(buffer, 0700) && errno != EEXIST)
    {
        perror(path);
        exit(1);
    }
}

static void setup_wm(void)
{
    uint32_t mask = XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT | XCB_EVENT_MASK_SUBSTRUCTURE_NOTIFY |
                    XCB_EVENT_MASK_PROPERTY_CHANGE | XCB_EVENT_MASK_STRUCTURE_NOTIFY;
    xcb_generic_error_t *error;
    xcb_font_t font;
    xcb_cursor_t cursor;
    intern_atoms();
    error = xcb_request_check(conn, xcb_change_window_attributes_checked(conn, root, XCB_CW_EVENT_MASK, &mask));
    if (error)
    {
        int access = error->error_code == XCB_ACCESS;
        free(error);
        if (access) fail("Another WM already owns this X display");
    }
    if (redirect)
    {
        const xcb_query_extension_reply_t *composite;
        static xcb_extension_t composite_id = {"Composite", 0};
        composite = xcb_get_extension_data(conn, &composite_id);
        redirect = composite && composite->present;
    }
    /* Windows without a cursor of their own, such as the menus of Linux
     * applications while they hold the pointer, show the root's: an
     * arrow, as window managers set, instead of the X server's cross. */
    font = xcb_generate_id(conn);
    xcb_open_font(conn, font, 6, "cursor");
    cursor = xcb_generate_id(conn);
    xcb_create_glyph_cursor(conn, cursor, font, font, 68, 69, 0, 0, 0, 0xffff, 0xffff, 0xffff);
    xcb_change_window_attributes(conn, root, XCB_CW_CURSOR, &cursor);
    xcb_close_font(conn, font);
    support = xcb_generate_id(conn);
    xcb_create_window(conn, XCB_COPY_FROM_PARENT, support, root, -1, -1, 1, 1, 0, XCB_WINDOW_CLASS_COPY_FROM_PARENT,
                      XCB_COPY_FROM_PARENT, 0, NULL);
    set_one(root, A._NET_SUPPORTING_WM_CHECK, XCB_ATOM_WINDOW, support);
    set_one(support, A._NET_SUPPORTING_WM_CHECK, XCB_ATOM_WINDOW, support);
    xcb_change_property(conn, XCB_PROP_MODE_REPLACE, support, A._NET_WM_NAME, A.UTF8_STRING, 8, 8, "win4wine");
    {
        uint32_t supported[] = {
            A._NET_SUPPORTED, A._NET_SUPPORTING_WM_CHECK, A._NET_CLIENT_LIST, A._NET_CLIENT_LIST_STACKING,
            A._NET_ACTIVE_WINDOW, A._NET_WORKAREA, A._NET_NUMBER_OF_DESKTOPS, A._NET_CURRENT_DESKTOP,
            A._NET_WM_STATE, A._NET_WM_STATE_HIDDEN, A._NET_WM_STATE_MAXIMIZED_VERT,
            A._NET_WM_STATE_MAXIMIZED_HORZ, A._NET_WM_STATE_ABOVE, A._NET_WM_STATE_FULLSCREEN, A._NET_WM_WINDOW_TYPE,
            A._NET_WM_WINDOW_TYPE_DESKTOP, A._NET_WM_WINDOW_TYPE_DOCK, A._NET_WM_ICON, A._NET_FRAME_EXTENTS,
            A._NET_CLOSE_WINDOW, A._NET_WM_STRUT, A._NET_WM_STRUT_PARTIAL,
        };
        set_property(root, A._NET_SUPPORTED, XCB_ATOM_ATOM, supported, ARRAY_SIZE(supported));
    }
    set_one(root, A._NET_NUMBER_OF_DESKTOPS, XCB_ATOM_CARDINAL, 1);
    set_one(root, A._NET_CURRENT_DESKTOP, XCB_ATOM_CARDINAL, 0);
    own_xsettings();
    load_keymap();
    tab_code = keysym_to_keycode(XK_Tab);
    escape_code = keysym_to_keycode(XK_Escape);
    f4_code = keysym_to_keycode(XK_F4);
    l_code = keysym_to_keycode(XK_l);
    {
        xcb_get_modifier_mapping_reply_t *r = REPLY(get_modifier_mapping, xcb_get_modifier_mapping(conn));
        if (r)
        {
            xcb_keycode_t *codes = xcb_get_modifier_mapping_keycodes(r);
            int per = r->keycodes_per_modifier;
            for (int i = 0; i < per && alt_count < (int)ARRAY_SIZE(alt_codes); i++)
                if (codes[3 * per + i] && !is_alt(codes[3 * per + i])) alt_codes[alt_count++] = codes[3 * per + i];
            free(r);
        }
    }
    static const uint16_t locks[] = {0, XCB_MOD_MASK_LOCK, XCB_MOD_MASK_2, XCB_MOD_MASK_LOCK | XCB_MOD_MASK_2};
    for (size_t i = 0; i < ARRAY_SIZE(locks); i++)
    {
        grab_key(tab_code, XCB_MOD_MASK_1 | locks[i], XCB_GRAB_MODE_ASYNC);
        grab_key(tab_code, XCB_MOD_MASK_1 | XCB_MOD_MASK_SHIFT | locks[i], XCB_GRAB_MODE_ASYNC);
        for (uint8_t button = 1; button <= 3; button += 2)
            xcb_grab_button(conn, 0, root,
                            XCB_EVENT_MASK_BUTTON_PRESS | XCB_EVENT_MASK_BUTTON_RELEASE |
                                XCB_EVENT_MASK_POINTER_MOTION,
                            XCB_GRAB_MODE_ASYNC, XCB_GRAB_MODE_ASYNC, XCB_NONE, XCB_NONE, button,
                            XCB_MOD_MASK_1 | locks[i]);
        grab_key(f4_code, XCB_MOD_MASK_1 | locks[i], XCB_GRAB_MODE_ASYNC);
        if (l_code) grab_key(l_code, XCB_MOD_MASK_4 | locks[i], XCB_GRAB_MODE_SYNC);
    }
    /* The Windows key with XP's hotkeys (Win+R, Win+E, ...) and Ctrl+Esc
     * belong to the shell even while a native client has the keyboard; see shell_key. */
    {
        xcb_keycode_t l = keysym_to_keycode(XK_Super_L), r = keysym_to_keycode(XK_Super_R);
        if (l) super_codes[super_count++] = l;
        if (r && r != l) super_codes[super_count++] = r;
    }
    for (int i = 0; i < super_count; i++) grab_key(super_codes[i], XCB_MOD_MASK_ANY, XCB_GRAB_MODE_SYNC);
    for (size_t i = 0; i < ARRAY_SIZE(locks); i++)
        grab_key(escape_code, XCB_MOD_MASK_CONTROL | locks[i], XCB_GRAB_MODE_SYNC);
    for (size_t i = 0; i < ARRAY_SIZE(volume_keys); i++)
    {
        xcb_keycode_t code = keysym_to_keycode(volume_keys[i].keysym);
        if (!code) continue;
        volume_codes[volume_count].code = code;
        volume_codes[volume_count++].script = volume_keys[i].script;
        grab_key(code, XCB_MOD_MASK_ANY, XCB_GRAB_MODE_ASYNC);
    }
    /* The work area before any client asks for it (wm.py had none yet) */
    {
        xcb_get_geometry_reply_t *g = get_geometry(root);
        if (g)
        {
            area[2] = g->width;
            area[3] = g->height;
            free(g);
        }
    }
    {
        xcb_query_tree_reply_t *tree;
        xcb_window_t *children, *copy;
        int n = query_tree(root, &tree, &children, NULL);
        if (n > 0)
        {
            copy = xmalloc(sizeof(*copy) * n);
            memcpy(copy, children, sizeof(*copy) * n);
            free(tree);
            for (int i = 0; i < n; i++)
            {
                xcb_get_window_attributes_reply_t *a =
                    REPLY(get_window_attributes, xcb_get_window_attributes(conn, copy[i]));
                int viewable = a && a->map_state == XCB_MAP_STATE_VIEWABLE;
                free(a);
                if (viewable) manage(copy[i]);
            }
            free(copy);
        }
        else if (n == 0) free(tree);
    }
    workarea();
}

static void on_signal(int number)
{
    int saved = errno;
    (void)number;
    running = 0;
    if (wake_write >= 0)
    {
        ssize_t ignored = write(wake_write, "", 1);
        (void)ignored;
    }
    errno = saved;
}

/* Sleep until an X event, a proxy file or a signal arrives.
 *
 * Every change the proxy must see comes from an X event (map, configure,
 * property, focus) or from the proxy's own files, so the bridge state is
 * recomputed only then. Writes happen only when their content changes;
 * the events they cause settle after one more pass. */
static void run(void)
{
    int changes = inotify_init1(IN_NONBLOCK | IN_CLOEXEC), pipe_fds[2], dirty = 2;
    xcb_generic_event_t *next = NULL;
    if (changes < 0 || inotify_add_watch(changes, ipc, IN_CLOSE_WRITE | IN_MOVED_TO) < 0)
    {
        perror("inotify");
        exit(1);
    }
    if (pipe2(pipe_fds, O_NONBLOCK | O_CLOEXEC))
    {
        perror("pipe");
        exit(1);
    }
    wake_write = pipe_fds[1];
    struct sigaction action = {.sa_handler = on_signal};
    sigaction(SIGTERM, &action, NULL);
    sigaction(SIGINT, &action, NULL);
    while (running)
    {
        xcb_generic_event_t *e;
        while ((e = next ? next : xcb_poll_for_event(conn)))
        {
            next = NULL;
            if (e->response_type)
            {
                xerror = 0;
                handle_event(e);
                int level = changes_bridge(e);
                if (xerror) level = 2; /* A client may disappear between any two requests. */
                if (level > dirty) dirty = level;
            }
            free(e);
        }
        if (xcb_connection_has_error(conn)) fail("X connection lost");
        {
            char buffer[4096] __attribute__((aligned(__alignof__(struct inotify_event))));
            ssize_t n;
            while ((n = read(changes, buffer, sizeof(buffer))) > 0)
                for (char *p = buffer; p < buffer + n;)
                {
                    struct inotify_event *event = (struct inotify_event *)p;
                    if (event->len)
                    {
                        size_t length = strlen(event->name);
                        if ((length >= 4 && !strcmp(event->name + length - 4, ".cmd")) ||
                            !strcmp(event->name, "frames.tsv"))
                            dirty = 2;
                    }
                    p += sizeof(*event) + event->len;
                }
        }
        if (dirty)
        {
            write_geometry(); /* First: frames follow their clients. */
            if (dirty == 2)
            {
                commands();
                snapshot();
                sync_frames();
            }
            dirty = 0;
        }
        xcb_flush(conn);
        if ((next = xcb_poll_for_event(conn))) continue; /* Replies to this pass queued more events. */
        if (xcb_connection_has_error(conn)) fail("X connection lost");
        struct pollfd fds[3] = {
            {xcb_get_file_descriptor(conn), POLLIN, 0}, {changes, POLLIN, 0}, {pipe_fds[0], POLLIN, 0}};
        if (poll(fds, 3, -1) < 0 && errno != EINTR)
        {
            perror("poll");
            exit(1);
        }
        if (fds[2].revents)
        {
            char buffer[64];
            while (read(pipe_fds[0], buffer, sizeof(buffer)) > 0) {}
        }
    }
    xcb_disconnect(conn);
}

int main(int argc, char **argv)
{
    const char *ipc_argument = NULL;
    struct stat st;
    char resolved[PATH_MAX];
    redirect = 1;
    for (int i = 1; i < argc; i++)
    {
        if (!strcmp(argv[i], "--ipc") && i + 1 < argc) ipc_argument = argv[++i];
        else if (!strncmp(argv[i], "--ipc=", 6)) ipc_argument = argv[i] + 6;
        else if (!strcmp(argv[i], "--no-composite")) redirect = 0;
        else if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help"))
        {
            printf("usage: wm --ipc IPC [--no-composite]\n\n"
                   "Small single-screen X11 WM and file IPC endpoint for the Win32 shell bridge.\n\n"
                   "  --no-composite  leave Wine's windows unredirected\n");
            return 0;
        }
        else
        {
            fprintf(stderr, "usage: wm --ipc IPC [--no-composite]\nwm: unrecognized argument: %s\n", argv[i]);
            return 2;
        }
    }
    if (!ipc_argument)
    {
        fprintf(stderr, "usage: wm --ipc IPC [--no-composite]\nwm: the following arguments are required: --ipc\n");
        return 2;
    }
    mkdirs(ipc_argument);
    if (!realpath(ipc_argument, resolved))
    {
        perror(ipc_argument);
        return 1;
    }
    if (strlen(resolved) >= sizeof(ipc)) fail("IPC directory path too long");
    snprintf(ipc, sizeof(ipc), "%s", resolved);
    if (stat(ipc, &st) || st.st_uid != getuid() || st.st_mode & 077)
        fail("IPC directory must be owned by this user, mode 0700");
    /* Volume key commands are not waited for. */
    struct sigaction ignore = {.sa_handler = SIG_IGN, .sa_flags = SA_NOCLDWAIT};
    sigaction(SIGCHLD, &ignore, NULL);
    conn = xcb_connect(NULL, &screen_number);
    if (xcb_connection_has_error(conn)) fail("Cannot open the X display");
    setup = xcb_get_setup(conn);
    {
        xcb_screen_iterator_t it = xcb_setup_roots_iterator(setup);
        for (int i = 0; i < screen_number && it.rem; i++) xcb_screen_next(&it);
        screen = it.data;
    }
    root = screen->root;
    setup_wm();
    run();
    return 0;
}
