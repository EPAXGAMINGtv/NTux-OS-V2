#ifndef NTUX_COMPAT_H
#define NTUX_COMPAT_H

#if defined(__ntux__)

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include <time.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/select.h>
#include <sys/mman.h>
#include <dirent.h>
#include <limits.h>
#include <signal.h>
#include <locale.h>
#include <math.h>
#include <setjmp.h>
#include <assert.h>
#include <inttypes.h>

#ifndef SSIZE_MAX
#define SSIZE_MAX LONG_MAX
#endif

/* timespec not in NTux time.h */
struct timespec {
    time_t tv_sec;
    long tv_nsec;
};

/* socklen_t used by netdb stubs below */
typedef uint32_t socklen_t;

/* Missing POSIX defines */
#ifndef S_IRWXU
#define S_IRWXU 00700
#endif
#ifndef AT_SYMLINK_NOFOLLOW
#define AT_SYMLINK_NOFOLLOW 0x100
#endif
#ifndef ENAMETOOLONG
#define ENAMETOOLONG 36
#endif
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#ifndef M_PI_2
#define M_PI_2 1.57079632679489661923
#endif
#ifndef RAND_MAX
#define RAND_MAX 32767
#endif

/* NetSurf requires PRIu32 from inttypes */
#ifndef PRIu32
#define PRIu32 "u"
#endif
#ifndef PRIsizet
#define PRIsizet "zu"
#endif

/* Extra stdlib/string functions used by NetSurf */
char *strndup(const char *s, size_t n);
char *strchrnul(const char *s, int c);
int strcasecmp(const char *s1, const char *s2);
int strncasecmp(const char *s1, const char *s2, size_t n);
float strtof(const char *nptr, char **endptr);
int scandir(const char *dirp, struct dirent ***namelist, int (*filter)(const struct dirent *), int (*compar)(const struct dirent **, const struct dirent **));
int ftruncate(int fd, off_t length);
int fgetc(FILE *stream);
void perror(const char *s);
size_t strcspn(const char *s, const char *reject);
long long strtoll(const char *nptr, char **endptr, int base);
char *strcasestr(const char *haystack, const char *needle);
char *strtok(char *str, const char *delim);
float ceilf(float x);
int rand(void);
int fstatat(int dirfd, const char *pathname, struct stat *buf, int flags);
int dirfd(void *dirp);
int unlinkat(int dirfd, const char *pathname, int flags);

/* signal ops not in NTux signal.h */
typedef struct { unsigned long __val[16]; } sigset_t;
static inline int sigemptyset(sigset_t *s) { s->__val[0] = 0; return 0; }
static inline int sigfillset(sigset_t *s) { s->__val[0] = ~0UL; return 0; }
static inline int sigaddset(sigset_t *s, int n) { s->__val[0] |= (1UL << ((n-1) & 63)); return 0; }
static inline int sigdelset(sigset_t *s, int n) { s->__val[0] &= ~(1UL << ((n-1) & 63)); return 0; }
static inline int sigismember(const sigset_t *s, int n) { return (s->__val[0] >> ((n-1) & 63)) & 1; }
struct sigaction { void (*sa_handler)(int); sigset_t sa_mask; int sa_flags; void (*sa_sigaction)(int, void*, void*); };
#define SA_RESTART 0x10000000
#define SA_SIGINFO 4
#define SA_NOCLDSTOP 8
#define SA_NOCLDWAIT 0x20
#define SIG_BLOCK 0
#define SIG_UNBLOCK 1
#define SIG_SETMASK 2
static inline int sigaction(int sig, const struct sigaction *act, struct sigaction *old) { (void)sig; (void)act; (void)old; return 0; }
static inline int sigprocmask(int how, const sigset_t *set, sigset_t *old) { (void)how; (void)set; (void)old; return 0; }

/* pthread stubs */
typedef int pthread_t;
typedef int pthread_mutex_t;
typedef int pthread_mutexattr_t;
typedef int pthread_cond_t;
typedef int pthread_condattr_t;
typedef int pthread_key_t;
typedef int pthread_once_t;
typedef int pthread_attr_t;
#define PTHREAD_MUTEX_INITIALIZER 0
#define PTHREAD_ONCE_INIT 0
#define PTHREAD_CREATE_DETACHED 1
static inline int pthread_create(pthread_t *t, const pthread_attr_t *a, void *(*f)(void*), void *d) { (void)t; (void)a; (void)f; (void)d; return -1; }
static inline void pthread_exit(void *r) { (void)r; }
static inline int pthread_join(pthread_t t, void **r) { (void)t; (void)r; return -1; }
static inline int pthread_detach(pthread_t t) { (void)t; return 0; }
static inline int pthread_mutex_init(pthread_mutex_t *m, const pthread_mutexattr_t *a) { (void)a; *m = 0; return 0; }
static inline int pthread_mutex_destroy(pthread_mutex_t *m) { (void)m; return 0; }
static inline int pthread_mutex_lock(pthread_mutex_t *m) { (void)m; return 0; }
static inline int pthread_mutex_unlock(pthread_mutex_t *m) { (void)m; return 0; }
static inline int pthread_cond_init(pthread_cond_t *c, const pthread_condattr_t *a) { (void)a; *c = 0; return 0; }
static inline int pthread_cond_destroy(pthread_cond_t *c) { (void)c; return 0; }
static inline int pthread_cond_wait(pthread_cond_t *c, pthread_mutex_t *m) { (void)c; (void)m; return 0; }
static inline int pthread_cond_timedwait(pthread_cond_t *c, pthread_mutex_t *m, const struct timespec *t) { (void)c; (void)m; (void)t; return -1; }
static inline int pthread_cond_signal(pthread_cond_t *c) { (void)c; return 0; }
static inline int pthread_cond_broadcast(pthread_cond_t *c) { (void)c; return 0; }
static inline int pthread_key_create(pthread_key_t *k, void (*d)(void*)) { (void)d; *k = 0; return 0; }
static inline void *pthread_getspecific(pthread_key_t k) { (void)k; return NULL; }
static inline int pthread_setspecific(pthread_key_t k, const void *v) { (void)k; (void)v; return 0; }
static inline int pthread_once(pthread_once_t *o, void (*f)(void)) { (void)f; if (*o == 0) { *o = 1; f(); } return 0; }
static inline pthread_t pthread_self(void) { return 0; }
static inline int pthread_equal(pthread_t a, pthread_t b) { return a == b; }
static inline int pthread_attr_init(pthread_attr_t *a) { (void)a; return 0; }
static inline int pthread_attr_destroy(pthread_attr_t *a) { (void)a; return 0; }
static inline int pthread_attr_setdetachstate(pthread_attr_t *a, int s) { (void)a; (void)s; return 0; }
static inline int pthread_attr_setstacksize(pthread_attr_t *a, size_t s) { (void)a; (void)s; return 0; }

/* regex stubs */
typedef struct { } regex_t;
typedef int regoff_t;
typedef struct { int rm_so; int rm_eo; } regmatch_t;
#define REG_EXTENDED 1
#define REG_ICASE 2
#define REG_NOSUB 4
#define REG_NOMATCH 1
#define REG_BADPAT 2
#define REG_ESPACE 3
#define REG_EBRACK 4
#define REG_EPAREN 5
#define REG_EBRACE 6
#define REG_ERANGE 7
#define REG_ECTYPE 8
#define REG_ECOLLATE 9
static inline int regcomp(regex_t *r, const char *s, int f) { (void)r; (void)s; (void)f; return 0; }
static inline int regexec(const regex_t *r, const char *s, size_t n, regmatch_t *m, int f) { (void)r; (void)s; (void)n; (void)m; (void)f; return REG_NOMATCH; }
static inline size_t regerror(int c, const regex_t *r, char *b, size_t n) { (void)c; (void)r; return snprintf(b, n, "regex error"); }
static inline void regfree(regex_t *r) { (void)r; }

/* syslog */
#define LOG_ERR 3
#define LOG_WARNING 4
#define LOG_INFO 6
#define LOG_DEBUG 7
static inline void openlog(const char *id, int opt, int facility) { (void)id; (void)opt; (void)facility; }
static inline void syslog(int priority, const char *format, ...) { (void)priority; (void)format; }
static inline void closelog(void) { }

/* getopt */
extern char *optarg;
extern int optind, opterr, optopt;
struct option { const char *name; int has_arg; int *flag; int val; };
#define no_argument 0
#define required_argument 1
#define optional_argument 2
static inline int getopt(int argc, char * const argv[], const char *optstring) { (void)argc; (void)argv; (void)optstring; return -1; }
static inline int getopt_long(int argc, char * const argv[], const char *optstring, const struct option *lopts, int *lidx) { (void)argc; (void)argv; (void)optstring; (void)lopts; (void)lidx; return -1; }

/* Resource usage */
struct rusage { struct timeval ru_utime; struct timeval ru_stime; long ru_maxrss; };
#define RUSAGE_SELF 0
static inline int getrusage(int who, struct rusage *usage) { (void)who; if (usage) memset(usage, 0, sizeof(*usage)); return 0; }
static inline int backtrace(void **buffer, int size) { (void)buffer; (void)size; return 0; }
static inline char **backtrace_symbols(void *const *buffer, int size) { (void)buffer; (void)size; return NULL; }

/* glob */
typedef struct { size_t gl_pathc; char **gl_pathv; size_t gl_offs; } glob_t;
#define GLOB_NOMATCH 3
static inline int glob(const char *p, int f, int (*e)(const char*, int), glob_t *g) { (void)p; (void)f; (void)e; (void)g; return GLOB_NOMATCH; }
static inline void globfree(glob_t *g) { (void)g; }

/* sys/wait */
#define WNOHANG 1
static inline pid_t waitpid(pid_t pid, int *status, int options) { (void)pid; (void)status; (void)options; errno = ECHILD; return -1; }

/* poll */
#define POLLIN 1
#define POLLOUT 4
#define POLLERR 8
#define POLLHUP 16
#define POLLNVAL 32
struct pollfd { int fd; short events; short revents; };
typedef unsigned long nfds_t;
static inline int poll(struct pollfd *fds, nfds_t nfds, int timeout) { (void)fds; (void)nfds; (void)timeout; errno = ENOSYS; return -1; }

/* dlfcn */
#define RTLD_LAZY 1
#define RTLD_NOW 2
#define RTLD_LOCAL 0
#define RTLD_GLOBAL 4
static inline void *dlopen(const char *file, int mode) { (void)file; (void)mode; return NULL; }
static inline void *dlsym(void *handle, const char *name) { (void)handle; (void)name; return NULL; }
static inline int dlclose(void *handle) { (void)handle; return -1; }
static inline char *dlerror(void) { return "Not supported on NTux"; }

/* sys/un */
struct sockaddr_un { unsigned short sun_family; char sun_path[108]; };

/* ifaddrs */
struct ifaddrs { struct ifaddrs *ifa_next; char *ifa_name; unsigned int ifa_flags; struct sockaddr *ifa_addr; struct sockaddr *ifa_netmask; struct sockaddr *ifa_dstaddr; void *ifa_data; };
static inline int getifaddrs(struct ifaddrs **ifap) { *ifap = NULL; errno = ENOSYS; return -1; }
static inline void freeifaddrs(struct ifaddrs *ifa) { (void)ifa; }

/* langinfo */
#define CODESET 0
static inline char *nl_langinfo(int item) { (void)item; return "UTF-8"; }

/* termios */
typedef unsigned int speed_t;
typedef unsigned int tcflag_t;
typedef unsigned char cc_t;
#define NCCS 32
struct termios { tcflag_t c_iflag; tcflag_t c_oflag; tcflag_t c_cflag; tcflag_t c_lflag; cc_t c_cc[NCCS]; };
#define TCSANOW 0
#define ECHO 0x0008
#define ICANON 0x0002
#define VMIN 6
#define VTIME 5
static inline int tcgetattr(int fd, struct termios *t) { (void)fd; (void)t; return 0; }
static inline int tcsetattr(int fd, int opt, const struct termios *t) { (void)fd; (void)opt; (void)t; return 0; }
static inline int cfsetospeed(struct termios *t, speed_t s) { (void)t; (void)s; return 0; }
static inline int cfsetispeed(struct termios *t, speed_t s) { (void)t; (void)s; return 0; }

/* uuid */
static inline void uuid_generate(void *out) { memset(out, 0, 16); }

/* sysctl */
static inline int sysctl(int *name, int nlen, void *oldp, size_t *oldlenp, void *newp, size_t newlen) { (void)name; (void)nlen; (void)oldp; (void)oldlenp; (void)newp; (void)newlen; errno = ENOSYS; return -1; }

/* sys/ioctl */
static inline int ioctl(int fd, unsigned long request, ...) { (void)fd; (void)request; errno = ENOSYS; return -1; }

/* netdb stubs */
struct hostent { char *h_name; char **h_aliases; int h_addrtype; int h_length; char **h_addr_list; };
struct addrinfo { int ai_flags; int ai_family; int ai_socktype; int ai_protocol; socklen_t ai_addrlen; struct sockaddr *ai_addr; char *ai_canonname; struct addrinfo *ai_next; };
#define AI_PASSIVE 1
#define AI_CANONNAME 2
#define AI_NUMERICHOST 4
#define NI_NUMERICHOST 1
#define NI_NUMERICSERV 2
#define NI_MAXHOST 1025
#define NI_MAXSERV 32
#define EAI_AGAIN 1
#define EAI_BADFLAGS 2
#define EAI_FAIL 3
#define EAI_FAMILY 4
#define EAI_MEMORY 5
#define EAI_NONAME 6
#define EAI_SERVICE 7
#define EAI_SOCKTYPE 8
static inline struct hostent *gethostbyname(const char *name) { (void)name; return NULL; }
static inline struct hostent *gethostbyaddr(const void *addr, socklen_t len, int type) { (void)addr; (void)len; (void)type; return NULL; }
static inline int getaddrinfo(const char *node, const char *service, const struct addrinfo *hints, struct addrinfo **res) { (void)node; (void)service; (void)hints; (void)res; return EAI_NONAME; }
static inline void freeaddrinfo(struct addrinfo *res) { (void)res; }
static inline int getnameinfo(const struct sockaddr *sa, socklen_t salen, char *host, socklen_t hostlen, char *serv, socklen_t servlen, int flags) { (void)sa; (void)salen; (void)host; (void)hostlen; (void)serv; (void)servlen; (void)flags; return EAI_NONAME; }

/* fwd declarations for NetSurf types used by headers */
struct ssl_cert_info;
struct nsws_connection;
struct bitmap;
struct dom_document;
struct content;
struct hlcache_handle;
struct browser_window;
struct rect;
struct redraw_context;
struct core_window;
struct nsurl;
struct fetch;
struct object_params;

#endif /* __ntux__ */
#endif /* NTUX_COMPAT_H */
