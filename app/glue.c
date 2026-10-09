// glue.c - Android side of the AOT Minecraft: NativeActivity entry, the game's main on its own thread, stdout/stderr to
// logcat, and GLFW 3.4 (LWJGL's flavour) over ANativeWindow + EGL (GLES 2) with gl4es as the desktop GL.
// LWJGL loads this library as libglfw (-Dorg.lwjgl.glfw.libname) and gl4es as libGL (-Dorg.lwjgl.opengl.libname).
// Input: touch -> mouse (drag looks around in game), keyboard -> keys, game controller -> keys + mouse (pad->kbm, as on
// the Switch build), Back -> Escape.
#include <android/log.h>
#include <android/native_activity.h>
#include <android/native_window.h>
#include <android/input.h>
#include <android/looper.h>
#include <EGL/egl.h>
#include <jni.h>
#include <dlfcn.h>
#include <errno.h>
#include <math.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define LOG(...) __android_log_print(ANDROID_LOG_INFO, "mc", __VA_ARGS__)
#define API __attribute__((visibility("default")))

int mc_main(int argc, char **argv);   // the native image's main (renamed from main in link-apk.sh)

// --- activity state, shared between the UI thread (callbacks) and the game thread ---
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t cond = PTHREAD_COND_INITIALIZER;
static ANativeWindow *window;          // set by the UI thread, used by the game thread
static AInputQueue *input_queue;
static int H = 720, W = 1280;          // render size: 720 lines, width from the screen's aspect
static int screen_w = 1, screen_h = 1; // the window's own size, for touch coordinates
static int app_closing;
static volatile int app_focused = 1;    // the activity has the window focus; lost in the background, so the game pauses
static volatile int pad_active;         // a controller is in use: on-screen controls hidden until the screen is touched
static volatile int size_changed = 1;   // the game hears about the render size on the next poll (start, rotation, resume)

// --- stdout/stderr -> logcat ---
static void *log_pump(void *fdp) {
    int fd = (int)(long)fdp; char buf[1024]; size_t len = 0;
    for (;;) {
        ssize_t n = read(fd, buf + len, sizeof buf - 1 - len);
        if (n <= 0) return NULL;
        len += n;
        char *s = buf, *nl;
        while ((nl = memchr(s, '\n', buf + len - s))) { *nl = 0; __android_log_write(ANDROID_LOG_INFO, "mc-java", s); s = nl + 1; }
        len -= s - buf; memmove(buf, s, len);
        if (len == sizeof buf - 1) { buf[len] = 0; __android_log_write(ANDROID_LOG_INFO, "mc-java", buf); len = 0; }
    }
}
static void redirect_stdio(void) {
    int p[2]; pipe(p);
    dup2(p[1], 1); dup2(p[1], 2);
    setvbuf(stdout, NULL, _IOLBF, 0); setvbuf(stderr, NULL, _IONBF, 0);
    pthread_t t; pthread_create(&t, NULL, log_pump, (void *)(long)p[0]); pthread_detach(t);
}

// --- the game thread ---
static char data_dir[512], lib_dir[512], tmp_dir[512];
static void *game_thread(void *arg) {
    (void)arg;
    // args.txt in the data dir (one per line) replaces the defaults below, for trying settings without a rebuild
    static char *argv[128]; int argc = 0;
    static char line[128][512];
    char path[600]; snprintf(path, sizeof path, "%s/args.txt", data_dir);
    FILE *f = fopen(path, "r");
    argv[argc++] = "mc";
    char p1[600], p2[600], p3[600], p4[600], p5[600], p6[600], p7[600], p8[600], p9[600];
    snprintf(p1, sizeof p1, "-Dorg.lwjgl.librarypath=%s", lib_dir);
    snprintf(p2, sizeof p2, "-Dorg.lwjgl.glfw.libname=%s/libmc.so", lib_dir);
    snprintf(p3, sizeof p3, "-Dorg.lwjgl.opengl.libname=%s/libGL.so", lib_dir);
    snprintf(p4, sizeof p4, "-Dorg.lwjgl.openal.libname=%s/libopenal.so", lib_dir);
    snprintf(p5, sizeof p5, "-Djava.io.tmpdir=%s", tmp_dir);
    snprintf(p6, sizeof p6, "-Duser.home=%s", data_dir);
    snprintf(p7, sizeof p7, "%s/minecraft", data_dir);
    snprintf(p8, sizeof p8, "%s/assets", data_dir);
    snprintf(p9, sizeof p9, "-Duser.dir=%s", p7);
    char *fixed[] = { "-Xmx1g", "-Djava.awt.headless=true", p1, p2, p3, p4, p5, p6, p9 };
    for (int i = 0; i < 9; i++) argv[argc++] = fixed[i];
    if (f) {
        int n = 0;
        while (n < 100 && fgets(line[n], sizeof line[n], f)) {
            line[n][strcspn(line[n], "\r\n")] = 0;
            if (line[n][0]) argv[argc++] = line[n];
            n++;
        }
        fclose(f);
    } else {
        char *game[] = { "--username", "etonedemid", "--version", "1.12.2", "--gameDir", p7, "--assetsDir", p8,
                         "--assetIndex", "1.12", "--uuid", "00000000000000000000000000000000", "--accessToken", "0",
                         "--userType", "legacy", "--versionType", "release" };
        for (int i = 0; i < 18; i++) argv[argc++] = game[i];
    }
    // the render size, so the game doesn't start at its 854x480 default (it is also told again on the first poll)
    pthread_mutex_lock(&lock);
    if (window) { screen_w = ANativeWindow_getWidth(window); screen_h = ANativeWindow_getHeight(window); }
    pthread_mutex_unlock(&lock);
    if (screen_h > 1) W = (int)((long)H * screen_w / screen_h) & ~1;
    static char ws[16], hs[16];
    snprintf(ws, sizeof ws, "%d", W); snprintf(hs, sizeof hs, "%d", H);
    argv[argc++] = "--width"; argv[argc++] = ws; argv[argc++] = "--height"; argv[argc++] = hs;
    argv[argc] = NULL;
    mkdir(p7, 0777); mkdir(tmp_dir, 0777);
    chdir(p7);
    LOG("starting the game, %d args, data in %s", argc, data_dir);
    int rc = mc_main(argc, argv);
    LOG("game main returned %d", rc);
    exit(rc);
}
static void start_game(void) {
    static int started;
    if (started) return;
    started = 1;
    pthread_attr_t a; pthread_attr_init(&a);
    pthread_attr_setstacksize(&a, 16 << 20);   // the Java main thread: world generation recurses deeply
    pthread_t t; pthread_create(&t, &a, game_thread, NULL);
}

// =====================================================================================================================
// GLFW
typedef struct { int width, height, redBits, greenBits, blueBits, refreshRate; } GLFWvidmode;
typedef struct { unsigned char buttons[15]; float axes[6]; } GLFWgamepadstate;
enum { GLFW_PRESS = 1, GLFW_RELEASE = 0, GLFW_FOCUSED = 0x20001, GLFW_VISIBLE = 0x20004, GLFW_HOVERED = 0x2000B,
       GLFW_CURSOR = 0x33001, GLFW_CURSOR_NORMAL = 0x34001, GLFW_CURSOR_DISABLED = 0x34003, GLFW_PLATFORM_NULL = 0x60005 };
typedef struct { int should_close; void *user; } Win;
static Win win;
static EGLDisplay dpy;
static EGLSurface surf;
static EGLContext ctx;
static EGLConfig cfg;

static struct {
    void (*key)(Win *, int, int, int, int);
    void (*chr)(Win *, unsigned);
    void (*button)(Win *, int, int, int);
    void (*cursor)(Win *, double, double);
    void (*scroll)(Win *, double, double);
    void (*size)(Win *, int, int);
    void (*fbsize)(Win *, int, int);
    void (*focus)(Win *, int);
    void (*close)(Win *);
    void (*joystick)(int, int);
    void (*error)(int, const char *);
} cb;

API int glfwInit(void) { return 1; }
API void glfwTerminate(void) {}

// The window the current surface belongs to. Left unset by glfwCreateWindow on purpose: the first swap then makes the
// surface again and makes the context current once more, and gl4es needs that (without it the first draw dies inside
// gl4es on a null state pointer).
static ANativeWindow *surf_window;
static int make_surface(void) {
    pthread_mutex_lock(&lock);
    while (!window && !app_closing) pthread_cond_wait(&cond, &lock);
    ANativeWindow *w = window;
    pthread_mutex_unlock(&lock);
    if (!w) return 0;
    screen_w = ANativeWindow_getWidth(w); screen_h = ANativeWindow_getHeight(w);
    int nw = screen_h ? (int)((long)H * screen_w / screen_h) & ~1 : 1280;
    if (nw != W) { W = nw; size_changed = 1; }
    EGLint fmt; eglGetConfigAttrib(dpy, cfg, EGL_NATIVE_VISUAL_ID, &fmt);
    ANativeWindow_setBuffersGeometry(w, W, H, fmt);   // the compositor scales it up to the screen
    surf = eglCreateWindowSurface(dpy, cfg, w, NULL);
    LOG("glfw: surface %dx%d on a %dx%d screen: %p", W, H, screen_w, screen_h, surf);
    return surf != EGL_NO_SURFACE;
}
API Win *glfwCreateWindow(int w, int h, const char *title, void *monitor, void *share) {
    (void)w; (void)h; (void)monitor; (void)share;
    dpy = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    eglInitialize(dpy, NULL, NULL);
    static const EGLint attrs[] = { EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT, EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
        EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8, EGL_DEPTH_SIZE, 24, EGL_STENCIL_SIZE, 8, EGL_NONE };
    EGLint n;
    if (!eglChooseConfig(dpy, attrs, &cfg, 1, &n) || !n) { LOG("glfw: no EGL config %#x", eglGetError()); return NULL; }
    static const EGLint ca[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
    ctx = eglCreateContext(dpy, cfg, EGL_NO_CONTEXT, ca);
    if (!ctx || !make_surface()) { LOG("glfw: context/surface failed %#x", eglGetError()); return NULL; }
    LOG("glfw: window '%s'", title ? title : "");
    return &win;
}
API void glfwDestroyWindow(Win *w) { (void)w; }
static __thread Win *current;
API void glfwMakeContextCurrent(Win *w) {
    current = w;
    if (!eglMakeCurrent(dpy, w ? surf : EGL_NO_SURFACE, w ? surf : EGL_NO_SURFACE, w ? ctx : EGL_NO_CONTEXT))
        LOG("glfw: eglMakeCurrent %#x", eglGetError());
}
API Win *glfwGetCurrentContext(void) { return current; }

// The app went to the background: Android destroys the window, so the surface goes with it, and a new one is made when
// the window comes back. Frames in between are skipped.
static double now_s(void);
static void draw_overlay(void);
API void glfwSwapBuffers(Win *w) {
    (void)w;
    pthread_mutex_lock(&lock);
    ANativeWindow *now = window;
    pthread_mutex_unlock(&lock);
    if (now != surf_window || surf == EGL_NO_SURFACE) {
        if (surf != EGL_NO_SURFACE) { eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT); eglDestroySurface(dpy, surf); surf = EGL_NO_SURFACE; }
        if (!now) { usleep(50000); return; }
        if (!make_surface()) return;
        surf_window = now;
        eglMakeCurrent(dpy, surf, surf, ctx);
        return;
    }
    draw_overlay();
    eglSwapBuffers(dpy, surf);
    // frame rate and the longest frame, every 5 s, in logcat
    static double since, last; static int frames; static double worst;
    double t = now_s();
    if (last && t - last > worst) worst = t - last;
    last = t; frames++;
    if (!since) since = t;
    if (t - since >= 5) { LOG("fps %.1f, longest frame %.0f ms", frames / (t - since), worst * 1000); since = t; frames = 0; worst = 0; }
}
API void glfwSwapInterval(int i) { eglSwapInterval(dpy, i); }

static void *(*gl4es_proc)(const char *);
API void *glfwGetProcAddress(const char *n) {
    if (!gl4es_proc) {
        char p[600]; snprintf(p, sizeof p, "%s/libGL.so", lib_dir);
        void *h = dlopen(p, RTLD_NOW);
        gl4es_proc = h ? dlsym(h, "gl4es_GetProcAddress") : NULL;
        if (!gl4es_proc) { LOG("glfw: no gl4es (%s)", dlerror()); return NULL; }
    }
    return gl4es_proc(n);
}
API int glfwExtensionSupported(const char *n) { (void)n; return 0; }

API int glfwWindowShouldClose(Win *w) { return w ? w->should_close : 0; }
API void glfwSetWindowShouldClose(Win *w, int v) { if (w) w->should_close = v; }
API void glfwGetWindowSize(Win *w, int *x, int *y) { (void)w; if (x) *x = W; if (y) *y = H; }
API void glfwGetFramebufferSize(Win *w, int *x, int *y) { (void)w; if (x) *x = W; if (y) *y = H; }
API void glfwGetWindowPos(Win *w, int *x, int *y) { (void)w; if (x) *x = 0; if (y) *y = 0; }
API void glfwGetWindowFrameSize(Win *w, int *l, int *t, int *r, int *b) { (void)w; if (l) *l = 0; if (t) *t = 0; if (r) *r = 0; if (b) *b = 0; }
API void glfwGetWindowContentScale(Win *w, float *x, float *y) { (void)w; if (x) *x = 1; if (y) *y = 1; }
// Minecraft's "pause on lost focus" reads the focus attribute: in the background the game opens its pause menu
API int glfwGetWindowAttrib(Win *w, int a) { (void)w; return a == GLFW_FOCUSED ? app_focused : a == GLFW_VISIBLE || a == GLFW_HOVERED; }
API void glfwSetWindowUserPointer(Win *w, void *p) { if (w) w->user = p; }
API void *glfwGetWindowUserPointer(Win *w) { return w ? w->user : NULL; }
API float glfwGetWindowOpacity(Win *w) { (void)w; return 1; }
API const char *glfwGetWindowTitle(Win *w) { (void)w; return "Minecraft"; }
API void *glfwGetWindowMonitor(Win *w) { (void)w; return NULL; }

static int the_monitor;
static void *monitors[1] = { &the_monitor };
static GLFWvidmode mode = { 1280, 720, 8, 8, 8, 60 };
API void **glfwGetMonitors(int *n) { *n = 1; return monitors; }
API void *glfwGetPrimaryMonitor(void) { return &the_monitor; }
API const GLFWvidmode *glfwGetVideoMode(void *m) { (void)m; mode.width = W; mode.height = H; return &mode; }
API const GLFWvidmode *glfwGetVideoModes(void *m, int *n) { *n = 1; return glfwGetVideoMode(m); }
API void glfwGetMonitorPos(void *m, int *x, int *y) { (void)m; if (x) *x = 0; if (y) *y = 0; }
API void glfwGetMonitorWorkarea(void *m, int *x, int *y, int *w, int *h) { (void)m; if (x) *x = 0; if (y) *y = 0; if (w) *w = W; if (h) *h = H; }
API void glfwGetMonitorPhysicalSize(void *m, int *w, int *h) { (void)m; if (w) *w = 150; if (h) *h = 70; }
API void glfwGetMonitorContentScale(void *m, float *x, float *y) { (void)m; if (x) *x = 1; if (y) *y = 1; }
API const char *glfwGetMonitorName(void *m) { (void)m; return "Android"; }

static double now_s(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec + t.tv_nsec / 1e9; }
static double t0;
API double glfwGetTime(void) { return now_s() - t0; }
API void glfwSetTime(double t) { t0 = now_s() - t; }
API unsigned long glfwGetTimerValue(void) { return (unsigned long)(now_s() * 1e9); }
API unsigned long glfwGetTimerFrequency(void) { return 1000000000ul; }

// --- input: the UI thread queues events, glfwPollEvents (game thread) replays them through the game's callbacks ---
enum { EV_KEY, EV_CHAR, EV_TOUCH, EV_PAD };
typedef struct { int type, a, b; float x, y; } Ev;
static Ev evq[1024];
static int evq_n;
static void push(Ev e) { pthread_mutex_lock(&lock); if (evq_n < 1024) evq[evq_n++] = e; pthread_mutex_unlock(&lock); }

// controller state, written by the UI thread: buttons as bits, sticks/triggers -1..1
enum { PB_A = 1, PB_B = 2, PB_X = 4, PB_Y = 8, PB_L = 16, PB_R = 32, PB_ZL = 64, PB_ZR = 128, PB_MINUS = 256, PB_PLUS = 512,
       PB_LS = 1024, PB_RS = 2048, PB_UP = 4096, PB_DOWN = 8192, PB_LEFT = 16384, PB_RIGHT = 32768 };
static volatile unsigned pad_bits, pad_key_bits;   // pad_key_bits: from key events; the D-pad/triggers can also be axes
static volatile float pad_ax[6];                    // LX LY RX RY LT RT, Y down positive (Android's convention)
static volatile int pad_seen;

static unsigned char keys[512], kb_keys[512], pad_keys[512], touch_keys[512];
static double mouse_x, mouse_y;
static int touch_down, pad_buttons[3], touch_buttons[3];
static int cursor_mode = GLFW_CURSOR_NORMAL;
static void key_update(int k, int sc, int mods) {
    int down = kb_keys[k] | pad_keys[k] | touch_keys[k];
    if (keys[k] == down) return;
    keys[k] = down;
    if (cb.key) cb.key(&win, k, sc, down ? GLFW_PRESS : GLFW_RELEASE, mods);
}
enum { K_SPACE = 32, K_A = 65, K_D = 68, K_E = 69, K_Q = 81, K_S = 83, K_W = 87, K_ESC = 256, K_F3 = 292, K_F5 = 294,
       K_LSHIFT = 340, K_LCTRL = 341 };
static void key_event(int k, int down) { if (pad_keys[k] == down) return; pad_keys[k] = down; key_update(k, 0, 0); }
static void button_event(int b, int down) {
    if (pad_buttons[b] == down) return;
    pad_buttons[b] = down;
    if (cb.button) cb.button(&win, b, down, 0);
}

// pad->kbm, the Switch build's layout. In game: left stick WASD, right stick looks, A jump, B sneak, ZR/ZL (RT/LT)
// attack/use, X inventory, Y drop, L/R and D-pad left/right hotbar, Start pause, Select debug screen, LS sprint, RS pick
// block, D-pad up camera view. In menus: sticks move the pointer, A/RT click, Y right-click, LT held is Shift, B Escape.
static void pad_kbm(void) {
    static double last; static unsigned prev;
    double t = now_s(), dt = last ? t - last : 0; if (dt > 0.1) dt = 0.1; last = t;
    unsigned b = pad_bits | pad_key_bits, down = b & ~prev; prev = b;
    double lx = pad_ax[0], ly = -pad_ax[1], rx = pad_ax[2], ry = -pad_ax[3];
    if (cursor_mode == GLFW_CURSOR_DISABLED) {
        key_event(K_W, ly > 0.4); key_event(K_S, ly < -0.4); key_event(K_A, lx < -0.4); key_event(K_D, lx > 0.4);
        key_event(K_SPACE, !!(b & PB_A)); key_event(K_LSHIFT, !!(b & PB_B)); key_event(K_Q, !!(b & PB_Y));
        key_event(K_LCTRL, !!(b & PB_LS)); key_event(K_F5, !!(b & PB_UP)); key_event(K_F3, !!(b & PB_MINUS));
        button_event(0, !!(b & PB_ZR)); button_event(1, !!(b & PB_ZL)); button_event(2, !!(b & PB_RS));
        if (cb.scroll && (down & (PB_L | PB_LEFT))) cb.scroll(&win, 0, 1);
        if (cb.scroll && (down & (PB_R | PB_RIGHT))) cb.scroll(&win, 0, -1);
        double dz = 0.12, speed = 900 * dt;
        double mx = fabs(rx) > dz ? rx * fabs(rx) : 0, my = fabs(ry) > dz ? ry * fabs(ry) : 0;
        if (mx || my) { mouse_x += mx * speed; mouse_y -= my * speed; if (cb.cursor) cb.cursor(&win, mouse_x, mouse_y); }
        key_event(K_ESC, !!(b & PB_PLUS));
    } else {
        for (int k = 0; k < 512; k++) if (pad_keys[k] && k != K_ESC && k != K_LSHIFT && k != K_E) key_event(k, 0);
        button_event(2, 0);
        double dz = 0.15, speed = 700 * dt;
        double mx = fabs(lx) > dz ? lx : fabs(rx) > dz ? rx : 0, my = fabs(ly) > dz ? ly : fabs(ry) > dz ? ry : 0;
        if (mx || my) {
            mouse_x = fmin(fmax(mouse_x + mx * speed, 0), W - 1); mouse_y = fmin(fmax(mouse_y - my * speed, 0), H - 1);
            if (cb.cursor) cb.cursor(&win, mouse_x, mouse_y);
        }
        button_event(0, !!(b & (PB_A | PB_ZR))); button_event(1, !!(b & PB_Y));
        key_event(K_LSHIFT, !!(b & PB_ZL)); key_event(K_ESC, !!(b & PB_B));
    }
    key_event(K_E, !!(b & PB_X));
}

static int glfw_to_char(int k, int shift) {
    if (k >= 65 && k <= 90) return shift ? k : k + 32;
    if (k >= 48 && k <= 57) return shift ? ")!@#$%^&*("[k - 48] : k;
    if (k == 32) return 32;
    static const char *lo = "-=[]\\;'`,./", *hi = "_+{}|:\"~<>?";
    static const int ks[] = { 45, 61, 91, 93, 92, 59, 39, 96, 44, 46, 47 };
    for (int i = 0; i < 11; i++) if (ks[i] == k) return shift ? hi[i] : lo[i];
    return 0;
}

#include "touch.h"

API void glfwPollEvents(void) {
    static Ev local[1024];
    pthread_mutex_lock(&lock);
    int n = evq_n; memcpy(local, evq, n * sizeof *local); evq_n = 0;
    int closing = app_closing;
    pthread_mutex_unlock(&lock);
    if (closing && !win.should_close) { win.should_close = 1; if (cb.close) cb.close(&win); }
    if (size_changed && (cb.fbsize || cb.size)) {
        size_changed = 0;
        if (cb.size) cb.size(&win, W, H);
        if (cb.fbsize) cb.fbsize(&win, W, H);
    }
    for (int i = 0; i < n; i++) {
        Ev *e = &local[i];
        if (e->type == EV_KEY) {
            if (kb_keys[e->a] == e->b) continue;
            kb_keys[e->a] = e->b; key_update(e->a, e->a, 0);
        } else if (e->type == EV_CHAR) {
            if (cb.chr) cb.chr(&win, e->a);
        } else if (e->type == EV_TOUCH) {
            touch_event(e->a, e->b, e->x * W / screen_w, e->y * H / screen_h);
        }
    }
    touch_frame();
    static int was_focused = 1;
    if (app_focused != was_focused) {   // tell the game; nothing on screen stays held while away
        was_focused = app_focused;
        if (!was_focused) release_all();
        if (cb.focus) cb.focus(&win, was_focused);
    }
    if (pad_seen) pad_kbm();
}
API void glfwWaitEvents(void) { glfwPollEvents(); }
API void glfwWaitEventsTimeout(double t) { (void)t; glfwPollEvents(); }
API void glfwPostEmptyEvent(void) {}
API int glfwGetKey(Win *w, int k) { (void)w; return k >= 0 && k < 512 ? keys[k] : 0; }

API int glfwGetMouseButton(Win *w, int b) { (void)w; return b >= 0 && b < 3 ? (b == 0 && touch_down) || pad_buttons[b] || touch_buttons[b] : 0; }
API void glfwGetCursorPos(Win *w, double *x, double *y) { (void)w; if (x) *x = mouse_x; if (y) *y = mouse_y; }
API void glfwSetCursorPos(Win *w, double x, double y) { (void)w; mouse_x = x; mouse_y = y; }
API int glfwGetInputMode(Win *w, int m) { (void)w; return m == GLFW_CURSOR ? cursor_mode : 0; }
API void glfwSetInputMode(Win *w, int m, int v) { (void)w; if (m == GLFW_CURSOR) cursor_mode = v; }
API const char *glfwGetKeyName(int k, int s) { (void)k; (void)s; return NULL; }
API int glfwGetKeyScancode(int k) { return k; }
API int glfwGetPlatform(void) { return GLFW_PLATFORM_NULL; }
API int glfwPlatformSupported(int p) { return p == GLFW_PLATFORM_NULL; }
API void glfwGetVersion(int *a, int *b, int *c) { if (a) *a = 3; if (b) *b = 4; if (c) *c = 0; }
API const char *glfwGetVersionString(void) { return "3.4.0 android (EGL, gl4es)"; }
API int glfwGetError(const char **d) { if (d) *d = NULL; return 0; }
API const char *glfwGetClipboardString(Win *w) { (void)w; return ""; }
API int glfwRawMouseMotionSupported(void) { return 0; }

API int glfwGetGamepadState(int jid, GLFWgamepadstate *s) {
    if (jid != 0 || !pad_seen) return 0;
    unsigned b = pad_bits | pad_key_bits;
    static const unsigned order[15] = { PB_A, PB_B, PB_X, PB_Y, PB_L, PB_R, PB_MINUS, PB_PLUS, 0, PB_LS, PB_RS,
                                        PB_UP, PB_RIGHT, PB_DOWN, PB_LEFT };
    for (int i = 0; i < 15; i++) s->buttons[i] = order[i] && (b & order[i]);
    for (int i = 0; i < 4; i++) s->axes[i] = pad_ax[i];
    s->axes[4] = pad_ax[4] * 2 - 1; s->axes[5] = pad_ax[5] * 2 - 1;
    return 1;
}
static float axes_out[6]; static unsigned char buttons_out[15];
API int glfwJoystickPresent(int jid) { return jid == 0 && pad_seen; }
API int glfwJoystickIsGamepad(int jid) { return jid == 0 && pad_seen; }
API const float *glfwGetJoystickAxes(int jid, int *n) {
    GLFWgamepadstate s; *n = glfwGetGamepadState(jid, &s) ? 6 : 0; memcpy(axes_out, s.axes, sizeof axes_out); return axes_out;
}
API const unsigned char *glfwGetJoystickButtons(int jid, int *n) {
    GLFWgamepadstate s; *n = glfwGetGamepadState(jid, &s) ? 15 : 0; memcpy(buttons_out, s.buttons, 15); return buttons_out;
}
API const unsigned char *glfwGetJoystickHats(int jid, int *n) { (void)jid; *n = 0; return NULL; }
API const char *glfwGetJoystickName(int jid) { return jid == 0 ? "Gamepad" : NULL; }
API const char *glfwGetGamepadName(int jid) { return jid == 0 ? "Gamepad" : NULL; }
API const char *glfwGetJoystickGUID(int jid) { return jid == 0 ? "03000000de2800000112000000000000" : NULL; }
API int glfwUpdateGamepadMappings(const char *s) { (void)s; return 1; }

#define SETCB(name, field) API void *name(void *w, void *f) { (void)w; void *o = (void *)cb.field; cb.field = f; return o; }
SETCB(glfwSetKeyCallback, key)
SETCB(glfwSetCharCallback, chr)
SETCB(glfwSetMouseButtonCallback, button)
SETCB(glfwSetCursorPosCallback, cursor)
SETCB(glfwSetScrollCallback, scroll)
SETCB(glfwSetWindowSizeCallback, size)
SETCB(glfwSetFramebufferSizeCallback, fbsize)
SETCB(glfwSetWindowFocusCallback, focus)
SETCB(glfwSetWindowCloseCallback, close)
API void *glfwSetErrorCallback(void *f) { void *o = (void *)cb.error; cb.error = f; return o; }
API void *glfwSetJoystickCallback(void *f) { void *o = (void *)cb.joystick; cb.joystick = f; return o; }
static int dummy_cursor;
API void *glfwCreateCursor(void) { return &dummy_cursor; }
API void *glfwCreateStandardCursor(int s) { (void)s; return &dummy_cursor; }

// accepted and ignored
#define NOOP(n) API long n(void) { return 0; }
NOOP(glfwInitHint) NOOP(glfwInitAllocator) NOOP(glfwSetMonitorUserPointer) NOOP(glfwGetMonitorUserPointer)
NOOP(glfwSetMonitorCallback) NOOP(glfwSetGamma) NOOP(glfwGetGammaRamp) NOOP(glfwSetGammaRamp) NOOP(glfwDefaultWindowHints)
NOOP(glfwWindowHint) NOOP(glfwWindowHintString) NOOP(glfwSetWindowTitle) NOOP(glfwSetWindowIcon) NOOP(glfwSetWindowPos)
NOOP(glfwSetWindowSizeLimits) NOOP(glfwSetWindowAspectRatio) NOOP(glfwSetWindowSize) NOOP(glfwSetWindowOpacity)
NOOP(glfwIconifyWindow) NOOP(glfwRestoreWindow) NOOP(glfwMaximizeWindow) NOOP(glfwShowWindow) NOOP(glfwHideWindow)
NOOP(glfwFocusWindow) NOOP(glfwRequestWindowAttention) NOOP(glfwSetWindowMonitor) NOOP(glfwSetWindowAttrib)
NOOP(glfwSetWindowPosCallback) NOOP(glfwSetWindowRefreshCallback) NOOP(glfwSetWindowIconifyCallback)
NOOP(glfwSetWindowMaximizeCallback) NOOP(glfwSetWindowContentScaleCallback) NOOP(glfwDestroyCursor) NOOP(glfwSetCursor)
NOOP(glfwGetPreeditCursorRectangle) NOOP(glfwSetPreeditCursorRectangle) NOOP(glfwResetPreeditText)
NOOP(glfwGetPreeditCandidate) NOOP(glfwSetCharModsCallback) NOOP(glfwSetPreeditCallback) NOOP(glfwSetIMEStatusCallback)
NOOP(glfwSetPreeditCandidateCallback) NOOP(glfwSetCursorEnterCallback) NOOP(glfwSetDropCallback)
NOOP(glfwSetJoystickUserPointer) NOOP(glfwGetJoystickUserPointer) NOOP(glfwSetClipboardString)

// =====================================================================================================================
// Android input -> queued events / controller state (UI thread)
static int akey_to_glfw(int k) {
    if (k >= AKEYCODE_A && k <= AKEYCODE_Z) return 65 + k - AKEYCODE_A;
    if (k >= AKEYCODE_0 && k <= AKEYCODE_9) return 48 + k - AKEYCODE_0;
    if (k >= AKEYCODE_F1 && k <= AKEYCODE_F12) return 290 + k - AKEYCODE_F1;
    switch (k) {
    case AKEYCODE_SPACE: return 32; case AKEYCODE_ENTER: return 257; case AKEYCODE_DEL: return 259; case AKEYCODE_TAB: return 258;
    case AKEYCODE_ESCAPE: case AKEYCODE_BACK: return 256; case AKEYCODE_SHIFT_LEFT: return 340; case AKEYCODE_SHIFT_RIGHT: return 344;
    case AKEYCODE_CTRL_LEFT: return 341; case AKEYCODE_CTRL_RIGHT: return 345; case AKEYCODE_ALT_LEFT: return 342;
    case AKEYCODE_DPAD_UP: return 265; case AKEYCODE_DPAD_DOWN: return 264; case AKEYCODE_DPAD_LEFT: return 263;
    case AKEYCODE_DPAD_RIGHT: return 262; case AKEYCODE_MINUS: return 45; case AKEYCODE_EQUALS: return 61;
    case AKEYCODE_LEFT_BRACKET: return 91; case AKEYCODE_RIGHT_BRACKET: return 93; case AKEYCODE_BACKSLASH: return 92;
    case AKEYCODE_SEMICOLON: return 59; case AKEYCODE_APOSTROPHE: return 39; case AKEYCODE_GRAVE: return 96;
    case AKEYCODE_COMMA: return 44; case AKEYCODE_PERIOD: return 46; case AKEYCODE_SLASH: return 47;
    case AKEYCODE_FORWARD_DEL: return 261; case AKEYCODE_MOVE_HOME: return 268; case AKEYCODE_MOVE_END: return 269;
    }
    return -1;
}
static unsigned akey_to_pad(int k) {
    switch (k) {
    case AKEYCODE_BUTTON_A: return PB_A; case AKEYCODE_BUTTON_B: return PB_B; case AKEYCODE_BUTTON_X: return PB_X;
    case AKEYCODE_BUTTON_Y: return PB_Y; case AKEYCODE_BUTTON_L1: return PB_L; case AKEYCODE_BUTTON_R1: return PB_R;
    case AKEYCODE_BUTTON_L2: return PB_ZL; case AKEYCODE_BUTTON_R2: return PB_ZR; case AKEYCODE_BUTTON_THUMBL: return PB_LS;
    case AKEYCODE_BUTTON_THUMBR: return PB_RS; case AKEYCODE_BUTTON_START: return PB_PLUS; case AKEYCODE_BUTTON_SELECT: return PB_MINUS;
    }
    return 0;
}
static int handle_input(AInputEvent *e) {
    int src = AInputEvent_getSource(e);
    int from_pad = (src & AINPUT_SOURCE_GAMEPAD) == AINPUT_SOURCE_GAMEPAD || (src & AINPUT_SOURCE_JOYSTICK) == AINPUT_SOURCE_JOYSTICK;
    if (AInputEvent_getType(e) == AINPUT_EVENT_TYPE_KEY) {
        int k = AKeyEvent_getKeyCode(e), down = AKeyEvent_getAction(e) == AKEY_EVENT_ACTION_DOWN;
        if (AKeyEvent_getAction(e) == AKEY_EVENT_ACTION_MULTIPLE) return 0;
        unsigned pb = akey_to_pad(k);
        if (from_pad && (k == AKEYCODE_DPAD_UP || k == AKEYCODE_DPAD_DOWN || k == AKEYCODE_DPAD_LEFT || k == AKEYCODE_DPAD_RIGHT))
            pb = k == AKEYCODE_DPAD_UP ? PB_UP : k == AKEYCODE_DPAD_DOWN ? PB_DOWN : k == AKEYCODE_DPAD_LEFT ? PB_LEFT : PB_RIGHT;
        if (pb && !pad_active) LOG("controller key %d from device %d (source %#x)", k, AInputEvent_getDeviceId(e), src);
        if (pb) { pad_seen = 1; pad_active = 1; pad_key_bits = down ? pad_key_bits | pb : pad_key_bits & ~pb; return 1; }
        if (k == AKEYCODE_VOLUME_UP || k == AKEYCODE_VOLUME_DOWN) return 0;   // let the system change the volume
        int g = akey_to_glfw(k);
        if (g < 0) return 0;
        push((Ev){ EV_KEY, g, down });
        int c = glfw_to_char(g, (AKeyEvent_getMetaState(e) & AMETA_SHIFT_ON) != 0);
        if (down && c) push((Ev){ EV_CHAR, c, 0 });
        return 1;
    }
    if (from_pad) {   // sticks, triggers, D-pad as a hat
        if (!pad_active) LOG("controller motion from device %d (source %#x)", AInputEvent_getDeviceId(e), src);
        pad_seen = 1; pad_active = 1;
        pad_ax[0] = AMotionEvent_getAxisValue(e, AMOTION_EVENT_AXIS_X, 0);
        pad_ax[1] = AMotionEvent_getAxisValue(e, AMOTION_EVENT_AXIS_Y, 0);
        pad_ax[2] = AMotionEvent_getAxisValue(e, AMOTION_EVENT_AXIS_Z, 0);
        pad_ax[3] = AMotionEvent_getAxisValue(e, AMOTION_EVENT_AXIS_RZ, 0);
        float lt = fmaxf(AMotionEvent_getAxisValue(e, AMOTION_EVENT_AXIS_LTRIGGER, 0), AMotionEvent_getAxisValue(e, AMOTION_EVENT_AXIS_BRAKE, 0));
        float rt = fmaxf(AMotionEvent_getAxisValue(e, AMOTION_EVENT_AXIS_RTRIGGER, 0), AMotionEvent_getAxisValue(e, AMOTION_EVENT_AXIS_GAS, 0));
        pad_ax[4] = lt; pad_ax[5] = rt;
        float hx = AMotionEvent_getAxisValue(e, AMOTION_EVENT_AXIS_HAT_X, 0), hy = AMotionEvent_getAxisValue(e, AMOTION_EVENT_AXIS_HAT_Y, 0);
        unsigned b = 0;
        if (lt > 0.5f) b |= PB_ZL; if (rt > 0.5f) b |= PB_ZR;
        if (hx < -0.5f) b |= PB_LEFT; if (hx > 0.5f) b |= PB_RIGHT; if (hy < -0.5f) b |= PB_UP; if (hy > 0.5f) b |= PB_DOWN;
        pad_bits = b;
        return 1;
    }
    // every finger, by pointer id: a = 0 down, 1 move, 2 up
    int action = AMotionEvent_getAction(e), act = action & AMOTION_EVENT_ACTION_MASK;
    int idx = (action & AMOTION_EVENT_ACTION_POINTER_INDEX_MASK) >> AMOTION_EVENT_ACTION_POINTER_INDEX_SHIFT;
    size_t np = AMotionEvent_getPointerCount(e);
#define TOUCH(i, a) push((Ev){ EV_TOUCH, (a), AMotionEvent_getPointerId(e, (i)), AMotionEvent_getX(e, (i)), AMotionEvent_getY(e, (i)) })
    if (act == AMOTION_EVENT_ACTION_DOWN || act == AMOTION_EVENT_ACTION_POINTER_DOWN) TOUCH(idx, 0);
    else if (act == AMOTION_EVENT_ACTION_UP || act == AMOTION_EVENT_ACTION_POINTER_UP) TOUCH(idx, 2);
    else if (act == AMOTION_EVENT_ACTION_MOVE) for (size_t i = 0; i < np; i++) TOUCH(i, 1);
    else if (act == AMOTION_EVENT_ACTION_CANCEL) for (size_t i = 0; i < np; i++) TOUCH(i, 2);
#undef TOUCH
    return 1;
}
static int on_input(int fd, int events, void *data) {
    (void)fd; (void)events; (void)data;
    AInputEvent *e;
    while (input_queue && AInputQueue_getEvent(input_queue, &e) >= 0) {
        if (AInputQueue_preDispatchEvent(input_queue, e)) continue;
        AInputQueue_finishEvent(input_queue, e, handle_input(e));
    }
    return 1;
}

// Immersive: status and navigation bars hidden, a swipe from the edge shows them for a moment. Through JNI, as a
// NativeActivity has no Java code of its own: getWindow().getInsetsController().hide(systemBars()) (API 30+).
static void hide_system_bars(ANativeActivity *a) {
    JNIEnv *env = a->env;
    jclass act = (*env)->GetObjectClass(env, a->clazz);
    jobject window = (*env)->CallObjectMethod(env, a->clazz, (*env)->GetMethodID(env, act, "getWindow", "()Landroid/view/Window;"));
    jclass wc = (*env)->GetObjectClass(env, window);
    jobject ctl = (*env)->CallObjectMethod(env, window, (*env)->GetMethodID(env, wc, "getInsetsController", "()Landroid/view/WindowInsetsController;"));
    jclass types = (*env)->FindClass(env, "android/view/WindowInsets$Type");
    jint bars = (*env)->CallStaticIntMethod(env, types, (*env)->GetStaticMethodID(env, types, "systemBars", "()I"));
    if (ctl) {
        jclass cc = (*env)->GetObjectClass(env, ctl);
        (*env)->CallVoidMethod(env, ctl, (*env)->GetMethodID(env, cc, "setSystemBarsBehavior", "(I)V"), 2);   // BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE
        (*env)->CallVoidMethod(env, ctl, (*env)->GetMethodID(env, cc, "hide", "(I)V"), bars);
        (*env)->DeleteLocalRef(env, cc);
    }
    if ((*env)->ExceptionCheck(env)) { (*env)->ExceptionDescribe(env); (*env)->ExceptionClear(env); }
    (*env)->DeleteLocalRef(env, ctl); (*env)->DeleteLocalRef(env, types); (*env)->DeleteLocalRef(env, wc);
    (*env)->DeleteLocalRef(env, window); (*env)->DeleteLocalRef(env, act);
}
static void on_focus(ANativeActivity *a, int focused) { LOG("focus %d", focused); app_focused = focused; if (focused) hide_system_bars(a); }

// --- NativeActivity callbacks (UI thread) ---
static void on_window_created(ANativeActivity *a, ANativeWindow *w) {
    (void)a; pthread_mutex_lock(&lock); window = w; pthread_cond_broadcast(&cond); pthread_mutex_unlock(&lock);
    start_game();
}
static void on_window_destroyed(ANativeActivity *a, ANativeWindow *w) {
    (void)a; (void)w;
    pthread_mutex_lock(&lock); window = NULL; pthread_mutex_unlock(&lock);
    // the surface must be gone before this returns; the game thread notices at its next swap
    for (int i = 0; i < 100 && surf != EGL_NO_SURFACE && surf_window == w; i++) usleep(10000);
}
static void on_input_queue_created(ANativeActivity *a, AInputQueue *q) {
    (void)a; input_queue = q; AInputQueue_attachLooper(q, ALooper_forThread(), 1, on_input, NULL);
}
static void on_input_queue_destroyed(ANativeActivity *a, AInputQueue *q) { (void)a; AInputQueue_detachLooper(q); input_queue = NULL; }
static void on_destroy(ANativeActivity *a) { (void)a; pthread_mutex_lock(&lock); app_closing = 1; pthread_cond_broadcast(&cond); pthread_mutex_unlock(&lock); }

API void ANativeActivity_onCreate(ANativeActivity *a, void *saved, size_t saved_size) {
    (void)saved; (void)saved_size;
    a->callbacks->onNativeWindowCreated = on_window_created;
    a->callbacks->onNativeWindowDestroyed = on_window_destroyed;
    a->callbacks->onInputQueueCreated = on_input_queue_created;
    a->callbacks->onInputQueueDestroyed = on_input_queue_destroyed;
    a->callbacks->onDestroy = on_destroy;
    a->callbacks->onWindowFocusChanged = on_focus;
    hide_system_bars(a);
    ANativeActivity_setWindowFlags(a, 0x00000400 /* FULLSCREEN */ | 0x00000080 /* KEEP_SCREEN_ON */, 0);
    if (data_dir[0]) return;   // recreated (rotation etc.): the game keeps running
    redirect_stdio();
    snprintf(data_dir, sizeof data_dir, "%s", a->externalDataPath ? a->externalDataPath : a->internalDataPath);
    snprintf(tmp_dir, sizeof tmp_dir, "%s/tmp", a->internalDataPath);
    Dl_info di; dladdr((void *)ANativeActivity_onCreate, &di);
    snprintf(lib_dir, sizeof lib_dir, "%s", di.dli_fname); *strrchr(lib_dir, '/') = 0;
    t0 = now_s();
    LOG("onCreate: data %s, libs %s", data_dir, lib_dir);
}
