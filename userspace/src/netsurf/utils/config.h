#ifndef NETSURF_UTILS_CONFIG_H_
#define NETSURF_UTILS_CONFIG_H_

#include <stddef.h>

#if defined(__ntux__)

#include <stdint.h>
#include <limits.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

#define HAVE_STRNDUP 1
#define HAVE_STRCASESTR 1
#define HAVE_STRCHRNUL 1
#define HAVE_STRPTIME 1
#define HAVE_STRFTIME 1
#define HAVE_STRTOULL 1
#define HAVE_SYS_SELECT 1
#define HAVE_POSIX_INET_HEADERS 1
#define HAVE_INETATON 1
#define HAVE_INETPTON 1
#define HAVE_UTSNAME 1
#define HAVE_REALPATH 1
#define HAVE_MKDIR 1
#define HAVE_STDOUT 1
#define HAVE_SIGPIPE 1
#define HAVE_SCANDIR 1
#define HAVE_DIRFD 1
#define HAVE_UNLINKAT 1
#define HAVE_FSTATAT 1
#define HAVE_MMAP 1
#define HAVE_REGEX 1
#define HAVE_EXECINFO 1

#define WITH_REGEX
#undef WITH_NSLOG
#undef WITH_JS

#else /* !__ntux__ */

#if defined(__NetBSD__)
#include <sys/param.h>
#if (defined(__NetBSD_Version__) && __NetBSD_Prereq__(8,0,0))
#define NetBSD_v8
#endif
#endif

#if defined(__GLIBC__)
#if __GLIBC_PREREQ(2, 38)
#define NS_NEW_GLIBC
#endif
#endif

/* Try to detect which features the target OS supports */

#if (defined(_GNU_SOURCE) && \
     !defined(__APPLE__) || \
     defined(__amigaos4__) || \
     defined(__HAIKU__) || \
     (defined(_POSIX_C_SOURCE) && ((_POSIX_C_SOURCE - 0) >= 200809L)) && \
     !defined(__riscos__))
#define HAVE_STRNDUP
#else
#undef HAVE_STRNDUP
char *strndup(const char *s, size_t n);
#endif

#if ((defined(_GNU_SOURCE) ||			\
      defined(NS_NEW_GLIBC) || \
      defined(__APPLE__) ||			\
      defined(__HAIKU__) ||			\
      defined(__NetBSD__) ||			\
      defined(__OpenBSD__)) &&			\
     !defined(__serenity__))
#define HAVE_STRCASESTR
#else
#undef HAVE_STRCASESTR
char *strcasestr(const char *haystack, const char *needle);
#endif

#if (defined(_WIN32) ||	      \
     defined(__riscos__) ||   \
     defined(__HAIKU__) ||    \
     defined(__BEOS__) ||     \
     defined(__amigaos4__) || \
     defined(__AMIGA__) ||    \
     defined(__MINT__))
#undef HAVE_STRPTIME
#undef HAVE_STRFTIME
#else
#define HAVE_STRPTIME
#define HAVE_STRFTIME
#endif

#if ((defined(_GNU_SOURCE) && !defined(__APPLE__)) ||	\
     defined(NS_NEW_GLIBC) ||				\
     defined(__riscos__) ||				\
     defined(__HAIKU__) ||				\
     defined(NetBSD_v8))
#define HAVE_STRCHRNUL
#else
#undef HAVE_STRCHRNUL
char *strchrnul(const char *s, int c);
#endif

#define HAVE_STRTOULL
#if !defined(__amigaos4__) && defined(__AMIGA__)
#undef HAVE_STRTOULL
#endif

#define HAVE_SYS_SELECT
#define HAVE_POSIX_INET_HEADERS
#if (defined(_WIN32))
#undef HAVE_SYS_SELECT
#undef HAVE_POSIX_INET_HEADERS
#endif

#define HAVE_INETATON
#if (defined(_WIN32) || \
     defined(__serenity__))
#undef HAVE_INETATON
#endif

#define HAVE_INETPTON
#if (defined(_WIN32))
#undef HAVE_INETPTON
#endif

#define HAVE_UTSNAME
#if (defined(_WIN32))
#undef HAVE_UTSNAME
#endif

#define HAVE_REALPATH
#if (defined(_WIN32))
#undef HAVE_REALPATH
char *realpath(const char *path, char *resolved_path);
#endif

#define HAVE_MKDIR
#if (defined(_WIN32))
#undef HAVE_MKDIR
#endif

#define HAVE_SIGPIPE
#if (defined(_WIN32))
#undef HAVE_SIGPIPE
#endif

#define HAVE_STDOUT
#if (defined(_WIN32))
#undef HAVE_STDOUT
#endif

#define HAVE_MMAP
#if (defined(_WIN32) || defined(__riscos__) || defined(__HAIKU__) || defined(__BEOS__) || defined(__amigaos4__) || defined(__AMIGA__) || defined(__MINT__))
#undef HAVE_MMAP
#endif

#define HAVE_SCANDIR
#if (defined(_WIN32) ||				\
     defined(__serenity__))
#undef HAVE_SCANDIR
#endif

#define HAVE_DIRFD
#define HAVE_UNLINKAT
#define HAVE_FSTATAT
#if (defined(_WIN32) || defined(__riscos__) || defined(__HAIKU__) || defined(__BEOS__) || defined(__amigaos4__) || defined(__AMIGA__) || defined(__MINT__))
#undef HAVE_DIRFD
#undef HAVE_UNLINKAT
#undef HAVE_FSTATAT
#endif

#define HAVE_REGEX
#if (defined(__serenity__))
#undef HAVE_REGEX
#endif

#if ((defined(__linux__) && defined(__GLIBC__) && !defined(__UCLIBC__)) || \
     defined(__APPLE__))
#define HAVE_EXECINFO
#endif

#if defined(riscos)
    #define WITH_THEME_INSTALL
#elif defined(__HAIKU__) || defined(__BEOS__)
    #include <inttypes.h>
    #if defined(__HAIKU__)
    #endif
    #if defined(__BEOS__)
    	#define NO_IPV6 1
    #endif
#else
    #define WITH_MMAP
#endif

#if (defined(__amigaos4__) ||			\
     defined(__AMIGA__) ||			\
     defined(nsatari) ||			\
     defined(__serenity__))
	#define NO_IPV6
#endif

#endif /* !__ntux__ */
#endif
