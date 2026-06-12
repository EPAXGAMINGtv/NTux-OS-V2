#ifndef USER_ASSERT_H
#define USER_ASSERT_H

#define assert(expr) do { if (!(expr)) { for(;;); } } while (0)

#endif
