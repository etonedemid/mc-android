// touch.h - on-screen controls, included by glue.c (uses its key/mouse state and the game's GLFW callbacks), so the game
// gets them as plain keyboard and mouse input. Shown only in game (cursor captured); in menus the finger is the mouse.
//
//   left:   D-pad (W A S D; double-tap forward to sprint), sneak toggle in its middle
//   right:  jump, use (right button held: place, eat, draw a bow) beside it, drop above; inventory beside the hotbar;
//           pause and camera view at the top right; chat (opens with the keyboard up) at the top left
//   menus:  keyboard button at the top left (soft keyboard for chat, signs, names); over a screen opened from the
//           game, a close button at the top right
//   hotbar: tap a slot to select it
//   elsewhere: drag to look, short tap to attack (left click), hold still to break (left button held)
//
// Drawn with GLES 2 in a context of its own on the game's surface, so gl4es's view of the GL state is never disturbed.
#include <GLES2/gl2.h>

// GLES entry points looked up at run time rather than linked: libmc.so then imports no gl* symbol at all, so nothing of
// the system GLES library can end up in front of gl4es's functions of the same names.
#define GLFN(name) static __typeof__(name) *p_##name;
GLFN(glCreateShader)
GLFN(glShaderSource)
GLFN(glCompileShader)
GLFN(glCreateProgram)
GLFN(glAttachShader)
GLFN(glBindAttribLocation)
GLFN(glLinkProgram)
GLFN(glGetUniformLocation)
GLFN(glGenTextures)
GLFN(glBindTexture)
GLFN(glPixelStorei)
GLFN(glTexImage2D)
GLFN(glTexParameteri)
GLFN(glVertexAttribPointer)
GLFN(glUniform4f)
GLFN(glDrawArrays)
GLFN(glViewport)
GLFN(glDisable)
GLFN(glEnable)
GLFN(glBlendFunc)
GLFN(glUseProgram)
GLFN(glUniform2f)
GLFN(glActiveTexture)
GLFN(glEnableVertexAttribArray)
GLFN(glBindBuffer)
GLFN(glFlush)
static int gles_load(void) {
    void *h = dlopen("libGLESv2.so", RTLD_NOW | RTLD_LOCAL);
    if (!h) return 0;
#define GLLOAD(name) p_##name = (__typeof__(p_##name))dlsym(h, #name);
    GLLOAD(glCreateShader)
    GLLOAD(glShaderSource)
    GLLOAD(glCompileShader)
    GLLOAD(glCreateProgram)
    GLLOAD(glAttachShader)
    GLLOAD(glBindAttribLocation)
    GLLOAD(glLinkProgram)
    GLLOAD(glGetUniformLocation)
    GLLOAD(glGenTextures)
    GLLOAD(glBindTexture)
    GLLOAD(glPixelStorei)
    GLLOAD(glTexImage2D)
    GLLOAD(glTexParameteri)
    GLLOAD(glVertexAttribPointer)
    GLLOAD(glUniform4f)
    GLLOAD(glDrawArrays)
    GLLOAD(glViewport)
    GLLOAD(glDisable)
    GLLOAD(glEnable)
    GLLOAD(glBlendFunc)
    GLLOAD(glUseProgram)
    GLLOAD(glUniform2f)
    GLLOAD(glActiveTexture)
    GLLOAD(glEnableVertexAttribArray)
    GLLOAD(glBindBuffer)
    GLLOAD(glFlush)
    return 1;
}
#define glCreateShader p_glCreateShader
#define glShaderSource p_glShaderSource
#define glCompileShader p_glCompileShader
#define glCreateProgram p_glCreateProgram
#define glAttachShader p_glAttachShader
#define glBindAttribLocation p_glBindAttribLocation
#define glLinkProgram p_glLinkProgram
#define glGetUniformLocation p_glGetUniformLocation
#define glGenTextures p_glGenTextures
#define glBindTexture p_glBindTexture
#define glPixelStorei p_glPixelStorei
#define glTexImage2D p_glTexImage2D
#define glTexParameteri p_glTexParameteri
#define glVertexAttribPointer p_glVertexAttribPointer
#define glUniform4f p_glUniform4f
#define glDrawArrays p_glDrawArrays
#define glViewport p_glViewport
#define glDisable p_glDisable
#define glEnable p_glEnable
#define glBlendFunc p_glBlendFunc
#define glUseProgram p_glUseProgram
#define glUniform2f p_glUniform2f
#define glActiveTexture p_glActiveTexture
#define glEnableVertexAttribArray p_glEnableVertexAttribArray
#define glBindBuffer p_glBindBuffer
#define glFlush p_glFlush

// --- layout (surface pixels) ---
enum { ACT_KEY, ACT_TOGGLE, ACT_MOUSE };   // ACT_MOUSE: key is the mouse button
typedef struct { const char *glyph; int action, key; float x, y, w, h; int pressed; } Button;
enum { B_SNEAK, B_JUMP, B_USE, B_DROP, B_INV, B_PAUSE, B_CAM, B_CHAT, NBUTTONS };
static Button close_button, keyboard_button;
// A screen the game opened over the world (the cursor was freed after being captured): shows the close button. It
// stays set after quitting to the title screen, where Escape does nothing.
static int screen_from_game;
static void track_screen(void) {
    static int prev = GLFW_CURSOR_NORMAL;
    if (cursor_mode != prev) screen_from_game = prev == GLFW_CURSOR_DISABLED;
    if (cursor_mode != prev && cursor_mode == GLFW_CURSOR_DISABLED) keyboard(0);   // back in the game
    prev = cursor_mode;
}
static Button buttons[NBUTTONS];
static float pad_cx, pad_cy, pad_b;          // D-pad centre and cell size
static int gui_scale = 0;                    // guiScale from options.txt, 0 = auto
static float hb_x, hb_y, hb_slot;            // hotbar (Minecraft's own, for slot taps)

static const char *G_UP = "...##..." "..####.." ".######." "########" "...##..." "...##..." "...##..." "...##...";
static const char *G_DOWN = "...##..." "...##..." "...##..." "...##..." "########" ".######." "..####.." "...##...";
static const char *G_LEFT = "...#...." "..##...." ".###...." "########" "########" ".###...." "..##...." "...#....";
static const char *G_RIGHT = "....#..." "....##.." "....###." "########" "########" "....###." "....##.." "....#...";
static const char *G_JUMP = "...##..." "..####.." ".##..##." "##....##" "...##..." "..####.." ".##..##." "##....##";
// Steve sneaking, side on: head ahead of the shoulders, body leaning forward, arm hanging, legs upright (16x16)
static const char *G_SNEAK =
    "..........#####."
    "..........#####."
    "..........#####."
    "..........#####."
    "..........#####."
    "........#####..."
    ".......#######.."
    "......####.###.."
    ".....####..###.."
    "....####...###.."
    "....###....##..."
    "....###........."
    "....###........."
    "....###........."
    "....###........."
    "....###.........";
static const char *G_DROP = "...##..." "...##..." "...##..." ".######." "..####.." "...##..." "........" "########";
static const char *G_INV = "########" "#......#" "#.####.#" "########" "#......#" "#......#" "#......#" "########";
static const char *G_PAUSE = "........" ".##..##." ".##..##." ".##..##." ".##..##." ".##..##." ".##..##." "........";
static const char *G_CAM = "........" "..####.." ".#....#." "#..##..#" "#..##..#" ".#....#." "..####.." "........";
static const char *G_USE = "########" "#......#" "#..##..#" "#.####.#" "#.####.#" "#..##..#" "#......#" "########";
static const char *G_KEYB = "........" "########" "#.#.#.##" "########" "##.#.#.#" "########" "#.####.#" "########";
static const char *G_CHAT = "########" "#......#" "#.####.#" "#......#" "#.###..#" "#......#" "########" ".##.....";
static const char *G_CLOSE = "##....##" "###..###" ".######." "..####.." "..####.." ".######." "###..###" "##....##";
#define NGLYPHS 15   // atlas cells (16x16): 0 is solid, then these in order
static const char **all_glyphs(void) {
    static const char *all[NGLYPHS];
    const char *g[NGLYPHS] = { NULL, G_UP, G_DOWN, G_LEFT, G_RIGHT, G_JUMP, G_SNEAK, G_DROP, G_INV, G_PAUSE, G_CAM, G_CLOSE, G_USE, G_KEYB, G_CHAT };
    memcpy(all, g, sizeof g);
    return all;
}

static void layout(void) {
    float u = H / 720.f, b = 92 * u, m = 48 * u;
    pad_b = b; pad_cx = m + 1.5f * b + 48 * u; pad_cy = H - m - 1.5f * b - 40 * u;   // in from the corner, under the thumb
    buttons[B_SNEAK] = (Button){ G_SNEAK, ACT_TOGGLE, K_LSHIFT, pad_cx - b / 2, pad_cy - b / 2, b, b, buttons[B_SNEAK].pressed };
    buttons[B_JUMP] = (Button){ G_JUMP, ACT_KEY, K_SPACE, W - m - 1.2f * b, pad_cy - 0.6f * b, 1.2f * b, 1.2f * b };
    // use: up and to the left of jump, on the arc the right thumb sweeps from the corner
    buttons[B_USE] = (Button){ G_USE, ACT_MOUSE, 1, W - m - 2.2f * b, pad_cy - 1.6f * b, b, b };
    buttons[B_DROP] = (Button){ G_DROP, ACT_KEY, K_Q, W - m - 0.95f * b, pad_cy - 1.9f * b, 0.7f * b, 0.7f * b };
    // Minecraft's GUI scale: the largest that keeps 320x240 GUI pixels, capped by the option
    int s = 1;
    while ((gui_scale == 0 || s < gui_scale) && W / (s + 1) >= 320 && H / (s + 1) >= 240) s++;
    hb_slot = 20 * s; hb_x = W / 2.f - 91 * s; hb_y = H - 22 * s;
    float ib = 22 * s;
    buttons[B_INV] = (Button){ G_INV, ACT_KEY, K_E, hb_x + 182 * s + 6 * u, H - ib, ib, ib };
    float tb = 0.7f * b;
    buttons[B_PAUSE] = (Button){ G_PAUSE, ACT_KEY, K_ESC, W - m / 2 - tb, m / 2, tb, tb };
    buttons[B_CAM] = (Button){ G_CAM, ACT_KEY, K_F5, W - m / 2 - 2 * tb - 12 * u, m / 2, tb, tb };
    close_button = (Button){ G_CLOSE, ACT_KEY, K_ESC, W - m / 2 - tb, m / 2, tb, tb, close_button.pressed };
    keyboard_button = (Button){ G_KEYB, ACT_KEY, 0, m / 2, m / 2, tb, tb, keyboard_button.pressed };
    buttons[B_CHAT] = (Button){ G_CHAT, ACT_KEY, 'T', m / 2, m / 2, tb, tb };
}
static void read_gui_scale(void) {
    char p[700]; snprintf(p, sizeof p, "%s/minecraft/options.txt", data_dir);
    FILE *f = fopen(p, "r"); char line[256];
    if (!f) return;
    while (fgets(line, sizeof line, f)) if (!strncmp(line, "guiScale:", 9)) gui_scale = atoi(line + 9);
    fclose(f);
}

// --- input ---
static void touch_key(int k, int down) { if (touch_keys[k] == down) return; touch_keys[k] = down; key_update(k, 0, 0); }
static void touch_button(int b, int down) {
    if (touch_buttons[b] == down) return;
    touch_buttons[b] = down;
    if (cb.button) cb.button(&win, b, down, 0);
}
enum { R_NONE, R_MOUSE, R_LOOK, R_PAD, R_BUTTON };
typedef struct { int id, role, button, moved, breaking; double x, y, sx, sy, t; } Finger;
static Finger fingers[10];
static int pad_dirs;                         // 1 up 2 down 4 left 8 right
static double fwd_last; static int sprint;

static void pad_update(double x, double y) {
    double dx = x - pad_cx, dy = y - pad_cy, dz = pad_b * 0.35;
    int d = 0;
    if (fabs(dx) > dz || fabs(dy) > dz) {    // 8 directions: diagonals when the finger is between two arrows
        double a = atan2(-dy, dx);
        int sector = (int)lround(a / (M_PI / 4)) & 7;   // 0 right, 2 up, 4 left, 6 down
        static const int dirs[8] = { 8, 8 | 1, 1, 1 | 4, 4, 4 | 2, 2, 2 | 8 };
        d = dirs[sector];
    }
    if ((d & 1) && !(pad_dirs & 1)) {        // forward pressed: twice within 0.3 s sprints
        double t = now_s();
        if (t - fwd_last < 0.3) sprint = 1;
        fwd_last = t;
    }
    if (!(d & 1)) sprint = 0;
    pad_dirs = d;
    touch_key(K_W, d & 1); touch_key(K_S, !!(d & 2)); touch_key(K_A, !!(d & 4)); touch_key(K_D, !!(d & 8));
    touch_key(K_LCTRL, sprint);
}
static int in(Button *b, double x, double y, float pad) {
    return x >= b->x - pad && x < b->x + b->w + pad && y >= b->y - pad && y < b->y + b->h + pad;
}
static void release_all(void) {
    for (int i = 0; i < 10; i++) if (fingers[i].role) fingers[i].role = R_NONE;
    for (int k = 0; k < 512; k++) if (touch_keys[k] && !(k == K_LSHIFT && buttons[B_SNEAK].pressed)) touch_key(k, 0);
    for (int b = 0; b < 3; b++) touch_button(b, 0);
    for (int i = 0; i < NBUTTONS; i++) if (buttons[i].action != ACT_TOGGLE) buttons[i].pressed = 0;
    pad_dirs = 0; sprint = 0;
    if (touch_down) { touch_down = 0; if (cb.button) cb.button(&win, 0, 0, 0); }
}

static void touch_event(int a, int id, double x, double y) {
    if (a == 0) pad_active = 0;   // touching the screen brings the controls back after a controller was used
    static int was_ingame = -1;
    int ingame = cursor_mode == GLFW_CURSOR_DISABLED;
    if (ingame != was_ingame) { release_all(); was_ingame = ingame; }
    Finger *f = NULL;
    for (int i = 0; i < 10; i++) if (fingers[i].role && fingers[i].id == id) f = &fingers[i];
    if (a == 0) {
        if (f) f->role = R_NONE;   // a stale finger with the same id
        for (int i = 0; i < 10 && !f; i++) if (!fingers[i].role) f = &fingers[i];
        if (!f) return;
        *f = (Finger){ id, R_NONE, -1, 0, 0, x, y, x, y, now_s() };
        if (!ingame) {             // the keyboard button, in every menu
            layout();
            if (in(&keyboard_button, x, y, 8)) {
                f->role = R_BUTTON; f->button = -2;
                keyboard(!keyboard_shown);
                return;
            }
        }
        if (!ingame && screen_from_game) {   // the close button over a screen opened from the game
            layout();
            if (in(&close_button, x, y, 8)) {
                f->role = R_BUTTON; f->button = -1;
                close_button.pressed = 1; touch_key(K_ESC, 1);
                return;
            }
        }
        if (!ingame) {             // menus: the first finger is the mouse pointer, the left button held while it touches
            for (int i = 0; i < 10; i++) if (&fingers[i] != f && fingers[i].role == R_MOUSE) return;
            f->role = R_MOUSE;
            mouse_x = x; mouse_y = y;
            if (cb.cursor) cb.cursor(&win, x, y);
            touch_down = 1; if (cb.button) cb.button(&win, 0, 1, 0);
            return;
        }
        layout();
        for (int i = 0; i < NBUTTONS; i++) {
            Button *b = &buttons[i];
            if (i == B_SNEAK || !in(b, x, y, 8)) continue;
            f->role = R_BUTTON; f->button = i;
            b->pressed = 1;
            if (b->action == ACT_MOUSE) touch_button(b->key, 1); else touch_key(b->key, 1);
            if (i == B_CHAT) keyboard(1);
            return;
        }
        if (in(&buttons[B_SNEAK], x, y, 0)) {   // the D-pad's middle: sneak on/off
            f->role = R_BUTTON; f->button = B_SNEAK;
            buttons[B_SNEAK].pressed ^= 1; touch_key(K_LSHIFT, buttons[B_SNEAK].pressed);
            return;
        }
        if (fabs(x - pad_cx) < 1.7 * pad_b && fabs(y - pad_cy) < 1.7 * pad_b) { f->role = R_PAD; pad_update(x, y); return; }
        if (x >= hb_x && x < hb_x + 9 * hb_slot && y >= hb_y) {   // hotbar slot
            int slot = (int)((x - hb_x) / hb_slot);
            touch_key('1' + slot, 1); touch_key('1' + slot, 0);
            return;
        }
        f->role = R_LOOK;
        return;
    }
    if (!f) return;
    if (a == 1) {
        if (f->role == R_MOUSE) {
            mouse_x = x; mouse_y = y;
            if (cb.cursor) cb.cursor(&win, x, y);
        } else if (f->role == R_PAD) pad_update(x, y);
        else if (f->role == R_LOOK) {
            double dx = x - f->x, dy = y - f->y, sens = 2.2 * 720.0 / H;
            if (fabs(x - f->sx) + fabs(y - f->sy) > 12 * H / 720.0) f->moved = 1;
            mouse_x += dx * sens; mouse_y += dy * sens;
            if (cb.cursor) cb.cursor(&win, mouse_x, mouse_y);
        }
        f->x = x; f->y = y;
        return;
    }
    // up
    if (f->role == R_MOUSE) { touch_down = 0; if (cb.button) cb.button(&win, 0, 0, 0); }
    else if (f->role == R_PAD) pad_update(pad_cx, pad_cy);
    else if (f->role == R_BUTTON && f->button == -2) {}   // keyboard button: toggled on press
    else if (f->role == R_BUTTON && f->button < 0) { close_button.pressed = 0; touch_key(K_ESC, 0); }
    else if (f->role == R_BUTTON && buttons[f->button].action == ACT_MOUSE) { buttons[f->button].pressed = 0; touch_button(buttons[f->button].key, 0); }
    else if (f->role == R_BUTTON && buttons[f->button].action == ACT_KEY) { buttons[f->button].pressed = 0; touch_key(buttons[f->button].key, 0); }
    else if (f->role == R_LOOK) {
        if (f->breaking) touch_button(0, 0);
        else if (!f->moved && now_s() - f->t < 0.3) { touch_button(0, 1); touch_button(0, 0); }   // tap: attack
    }
    f->role = R_NONE;
}
// every frame: a look finger held still for 0.3 s starts breaking (left button held until it lifts)
static void touch_frame(void) {
    if (cursor_mode != GLFW_CURSOR_DISABLED) return;
    double t = now_s();
    for (int i = 0; i < 10; i++) {
        Finger *f = &fingers[i];
        if (f->role == R_LOOK && !f->breaking && !f->moved && t - f->t >= 0.3) { f->breaking = 1; touch_button(0, 1); }
    }
}

// --- drawing ---
static EGLContext overlay_ctx;
static GLuint prog, atlas;
static GLint u_scr, u_color;
static const char *glyphs[] = { NULL /* solid */, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL };
static int glyph_index(const char *g) {
    const char **all = all_glyphs();
    for (int i = 1; i < NGLYPHS; i++) if (all[i] == g) return i;
    return 0;
}
static GLuint shader(GLenum type, const char *src) {
    GLuint s = glCreateShader(type); glShaderSource(s, 1, &src, NULL); glCompileShader(s); return s;
}
static void overlay_init(void) {
    gles_load();
    static const EGLint ca[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
    overlay_ctx = eglCreateContext(dpy, cfg, EGL_NO_CONTEXT, ca);
    eglMakeCurrent(dpy, surf, surf, overlay_ctx);
    prog = glCreateProgram();
    glAttachShader(prog, shader(GL_VERTEX_SHADER,
        "attribute vec2 p; attribute vec2 t; uniform vec2 scr; varying vec2 v;"
        "void main() { v = t; gl_Position = vec4(p.x / scr.x * 2.0 - 1.0, 1.0 - p.y / scr.y * 2.0, 0.0, 1.0); }"));
    glAttachShader(prog, shader(GL_FRAGMENT_SHADER,
        "precision mediump float; uniform sampler2D tex; uniform vec4 color; varying vec2 v;"
        "void main() { gl_FragColor = color * texture2D(tex, v).a; }"));
    glBindAttribLocation(prog, 0, "p"); glBindAttribLocation(prog, 1, "t");
    glLinkProgram(prog);
    u_scr = glGetUniformLocation(prog, "scr"); u_color = glGetUniformLocation(prog, "color");
    // the glyph atlas: one row of 16x16 cells (cell 0 solid), alpha only, sampled nearest for crisp pixels
    const char **all = all_glyphs();
    // 16x16 cells: 16x16 glyphs as they are, 8x8 ones doubled
    static unsigned char px[16][NGLYPHS * 16];
    for (int g = 0; g < NGLYPHS; g++) {
        int n = all[g] && strlen(all[g]) == 256 ? 16 : 8;
        for (int y = 0; y < 16; y++)
            for (int x = 0; x < 16; x++) {
                int sx = x * n / 16, sy = y * n / 16;
                px[y][g * 16 + x] = !all[g] || all[g][sy * n + sx] == '#' ? 255 : 0;
            }
    }
    glGenTextures(1, &atlas); glBindTexture(GL_TEXTURE_2D, atlas);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_ALPHA, NGLYPHS * 16, 16, 0, GL_ALPHA, GL_UNSIGNED_BYTE, px);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    read_gui_scale();
    (void)glyphs;
}
static void quad(float x, float y, float w, float h, int g, float r, float gr, float b, float a) {
    float u0 = (g * 16 + 0.5f) / (NGLYPHS * 16.f), u1 = (g * 16 + 15.5f) / (NGLYPHS * 16.f);   // solid: away from the edges
    if (g) { u0 = g * 16 / (NGLYPHS * 16.f); u1 = (g * 16 + 16) / (NGLYPHS * 16.f); }
    float v[] = { x, y, u0, 0, x + w, y, u1, 0, x, y + h, u0, 1, x + w, y + h, u1, 1 };
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 16, v); glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 16, v + 2);
    glUniform4f(u_color, r * a, gr * a, b * a, a);   // premultiplied
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
}
static void draw_button(float x, float y, float w, float h, int glyph, int pressed) {
    float e = 2 * H / 720.f;
    quad(x, y, w, h, 0, 1, 1, 1, pressed ? 0.55f : 0.28f);                     // border
    quad(x + e, y + e, w - 2 * e, h - 2 * e, 0, 0, 0, 0, pressed ? 0.25f : 0.4f); // face
    float gs = fminf(w, h) * 0.5f;
    quad(x + (w - gs) / 2, y + (h - gs) / 2, gs, gs, glyph, 1, 1, 1, pressed ? 1.f : 0.8f);
}
static void draw_overlay(void) {
    track_screen();
    int ingame = cursor_mode == GLFW_CURSOR_DISABLED;
    int show = !pad_active;   // nothing while a controller is in use
    static int shown = -1;
    if (show != shown) { LOG("overlay %s (in game %d, screen from game %d, controller %d)", show ? "on" : "off", ingame, screen_from_game, pad_active); shown = show; }
    if (!show) return;
    // gl4es may still hold some of the frame's drawing; it must reach the surface before the overlay does, or the two
    // land in either order and the controls flicker
    static void (*gl4es_flush)(void);
    if (!gl4es_flush) gl4es_flush = (void (*)(void))glfwGetProcAddress("glFlush");
    if (gl4es_flush) gl4es_flush();
    if (!overlay_ctx) overlay_init();
    else eglMakeCurrent(dpy, surf, surf, overlay_ctx);
    layout();
    glViewport(0, 0, W, H);
    glDisable(GL_DEPTH_TEST); glDisable(GL_CULL_FACE); glDisable(GL_SCISSOR_TEST);
    glEnable(GL_BLEND); glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    glUseProgram(prog); glUniform2f(u_scr, W, H);
    glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, atlas);
    glEnableVertexAttribArray(0); glEnableVertexAttribArray(1);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    if (!ingame) {
        draw_button(keyboard_button.x, keyboard_button.y, keyboard_button.w, keyboard_button.h, glyph_index(G_KEYB), keyboard_shown);
        if (screen_from_game)
            draw_button(close_button.x, close_button.y, close_button.w, close_button.h, glyph_index(G_CLOSE), close_button.pressed);
        glFlush();
        eglMakeCurrent(dpy, surf, surf, ctx);
        return;
    }
    float b = pad_b;
    draw_button(pad_cx - b / 2, pad_cy - 1.5f * b, b, b, glyph_index(G_UP), pad_dirs & 1);
    draw_button(pad_cx - b / 2, pad_cy + 0.5f * b, b, b, glyph_index(G_DOWN), !!(pad_dirs & 2));
    draw_button(pad_cx - 1.5f * b, pad_cy - b / 2, b, b, glyph_index(G_LEFT), !!(pad_dirs & 4));
    draw_button(pad_cx + 0.5f * b, pad_cy - b / 2, b, b, glyph_index(G_RIGHT), !!(pad_dirs & 8));
    for (int i = 0; i < NBUTTONS; i++)
        draw_button(buttons[i].x, buttons[i].y, buttons[i].w, buttons[i].h, glyph_index(buttons[i].glyph), buttons[i].pressed);
    glFlush();
    eglMakeCurrent(dpy, surf, surf, ctx);
}
