#ifndef TCC_CONFIG_H
#define TCC_CONFIG_H

/* NTux TCC runtime layout */
#define CONFIG_TCCDIR "/boot/tcc"
#define CONFIG_SYSROOT ""
#define CONFIG_TCC_SYSINCLUDEPATHS "{B}/include"
#define CONFIG_TCC_LIBPATHS "{B}/lib"
#define CONFIG_TCC_CRTPREFIX "{B}/lib"
#define CONFIG_TCC_ELFINTERP "-"

/* Keep the build lean and avoid missing platform deps. */
#define CONFIG_TCC_STATIC 1
#define CONFIG_TCC_BCHECK 0
#define CONFIG_TCC_BACKTRACE 0
#define CONFIG_TCC_SEMLOCK 0

/* TCC version string for banner output */
#define TCC_VERSION "0.9.27"

#endif
