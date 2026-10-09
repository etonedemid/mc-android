// glibc-internal names the prebuilt GraalVM/JDK static libraries (built against glibc) call; bionic has the plain ones.
// struct stat is the same on both (the kernel's generic aarch64 layout).
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <android/log.h>
int __xstat(int v, const char *p, struct stat *s) { return stat(p, s); }
int __xstat64(int v, const char *p, struct stat *s) { return stat(p, s); }
int __lxstat64(int v, const char *p, struct stat *s) { return lstat(p, s); }
int __fxstat64(int v, int fd, struct stat *s) { return fstat(fd, s); }
int __xmknod(int v, const char *p, mode_t m, dev_t *d) { return mknod(p, m, *d); }
int __xpg_strerror_r(int e, char *b, size_t n) { return strerror_r(e, b, n); }
char *__strdup(const char *s) { return strdup(s); }
ssize_t __getdelim(char **l, size_t *n, int d, FILE *f) { return getdelim(l, n, d, f); }
int *__errno_location(void) { return &errno; }

// SVM finds the JDK's native methods (Java_*, JNI_OnLoad_*) with dlsym on dlopen(NULL), the main program. In an app
// that is app_process, so the image's own library stands in for it.
#include <dlfcn.h>
void *__real_dlopen(const char *f, int flags);
// The JDK's own libraries (AWT and friends) are linked in statically too; asked for by path, they are this library.
static int builtin_jdk_lib(const char *f) {
    static const char *names[] = { "libawt.so", "libawt_headless.so", "libmlib_image.so", "libjavajpeg.so", "liblcms.so",
        "libjava.so", "libnio.so", "libnet.so", "libzip.so", "libprefs.so", "libmanagement_ext.so", "libjvm.so" };
    const char *b = strrchr(f, '/'); b = b ? b + 1 : f;
    for (size_t i = 0; i < sizeof names / sizeof *names; i++) if (!strcmp(b, names[i])) return 1;
    return 0;
}
void *__wrap_dlopen(const char *f, int flags) {
    if (!f || builtin_jdk_lib(f)) {
        static void *self;
        if (!self) { Dl_info di; dladdr((void *)__wrap_dlopen, &di); self = __real_dlopen(di.dli_fname, RTLD_NOW | RTLD_NOLOAD); }
        return self;
    }
    return __real_dlopen(f, flags);
}

// glibc's ctype tables (the static JDK libraries use the macros, which index these): built from bionic's functions,
// with glibc's little-endian bit layout, indexable from -128.
#include <ctype.h>
char *__strtok_r(char *s, const char *d, char **save) { return strtok_r(s, d, save); }
static unsigned short ctype_b[384];
static int ctype_upper[384];
static void ctype_init(void) {
    static int done;
    if (done) return;
    for (int c = -128; c < 256; c++) {
        int u = (unsigned char)c;   // -128..-1 alias 128..255, as in glibc's tables for signed chars
        unsigned short b = 0;
        int bits[] = { isupper(u), islower(u), isalpha(u), isdigit(u), isxdigit(u), isspace(u), isprint(u), isgraph(u),
                       isblank(u), iscntrl(u), ispunct(u), isalnum(u) };
        for (int i = 0; i < 12; i++) if (bits[i]) b |= i < 8 ? (1 << i) << 8 : (1 << i) >> 8;
        ctype_b[c + 128] = b;
        ctype_upper[c + 128] = c < 0 ? c : toupper(u);
    }
    done = 1;
}
const unsigned short **__ctype_b_loc(void) { static const unsigned short *p; ctype_init(); p = ctype_b + 128; return &p; }
const int **__ctype_toupper_loc(void) { static const int *p; ctype_init(); p = ctype_upper + 128; return &p; }

// struct addrinfo: glibc has ai_addr before ai_canonname, bionic the other way round. The static JDK libraries (libnet)
// were built against glibc, so their getaddrinfo results are copied into glibc's layout.
#include <netdb.h>
struct glibc_addrinfo {
    int ai_flags, ai_family, ai_socktype, ai_protocol;
    socklen_t ai_addrlen;
    struct sockaddr *ai_addr;
    char *ai_canonname;
    struct glibc_addrinfo *ai_next;
};
int __real_getaddrinfo(const char *n, const char *s, const struct addrinfo *h, struct addrinfo **r);
void __real_freeaddrinfo(struct addrinfo *a);
int __wrap_getaddrinfo(const char *n, const char *s, const struct glibc_addrinfo *gh, struct glibc_addrinfo **out) {
    struct addrinfo h, *res = NULL;
    if (gh) {
        memset(&h, 0, sizeof h);
        h.ai_flags = gh->ai_flags; h.ai_family = gh->ai_family; h.ai_socktype = gh->ai_socktype; h.ai_protocol = gh->ai_protocol;
    }
    int rc = __real_getaddrinfo(n, s, gh ? &h : NULL, &res);
    if (rc) { *out = NULL; return rc; }
    struct glibc_addrinfo *head = NULL, **tail = &head;
    for (struct addrinfo *a = res; a; a = a->ai_next) {
        struct glibc_addrinfo *g = calloc(1, sizeof *g + a->ai_addrlen);
        g->ai_flags = a->ai_flags; g->ai_family = a->ai_family; g->ai_socktype = a->ai_socktype; g->ai_protocol = a->ai_protocol;
        g->ai_addrlen = a->ai_addrlen;
        g->ai_addr = (struct sockaddr *)(g + 1); memcpy(g->ai_addr, a->ai_addr, a->ai_addrlen);
        g->ai_canonname = a->ai_canonname ? strdup(a->ai_canonname) : NULL;
        *tail = g; tail = &g->ai_next;
    }
    __real_freeaddrinfo(res);
    *out = head;
    return 0;
}
void __wrap_freeaddrinfo(struct glibc_addrinfo *g) {
    while (g) { struct glibc_addrinfo *n = g->ai_next; free(g->ai_canonname); free(g); g = n; }
}
