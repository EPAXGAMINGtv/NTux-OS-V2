#ifndef USER_ASSERT_H
#define USER_ASSERT_H

void abort(void);

#define assert(expr) do { if (!(expr)) abort(); } while (0)

#endif
