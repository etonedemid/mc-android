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
#ifdef TRACE_MPROTECT
int __real_mprotect(void *a, size_t l, int p);
int __wrap_mprotect(void *a, size_t l, int p) {
    int r = __real_mprotect(a, l, p);
    fprintf(stderr, "mprotect(%p, %#zx, %d) = %d errno %d\n", a, l, p, r, r ? errno : 0);
    return r;
}
void *__real_mmap(void *a, size_t l, int p, int f, int fd, off_t o);
void *__wrap_mmap(void *a, size_t l, int p, int f, int fd, off_t o) {
    void *r = __real_mmap(a, l, p, f, fd, o);
    fprintf(stderr, "mmap(%p, %#zx, %d, %#x, %d, %#lx) = %p errno %d\n", a, l, p, f, fd, (long)o, r, r == MAP_FAILED ? errno : 0);
    return r;
}
void *__real_mremap(void *a, size_t l, size_t n, int f, ...);
void *__wrap_mremap(void *a, size_t l, size_t n, int f, void *na) {
    void *r = __real_mremap(a, l, n, f, na);
    fprintf(stderr, "mremap(%p, %#zx, %#zx, %#x) = %p errno %d\n", a, l, n, f, r, r == MAP_FAILED ? errno : 0);
    return r;
}
int __real_open(const char *p, int f, ...);
int __wrap_open(const char *p, int f, int m) {
    int r = __real_open(p, f, m);
    fprintf(stderr, "open(%s, %#x) = %d errno %d\n", p, f, r, r < 0 ? errno : 0);
    return r;
}
#endif
