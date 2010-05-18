#if !defined(O_PRIVATE_INCLUDED)
#define O_PRIVATE_INCLUDED

#include <stdio.h>

#define array_sizeof(a) (sizeof(a) / sizeof(a[0]))

#if defined(__GNUC__)
#   define TRACE(...) do { \
    printf("%s:%d ", __FILE__, __LINE__); \
    printf(__VA_ARGS__); \
    printf("\n"); \
    fflush(stdout); \
} while (0)
#else
#   define TRACE
#endif

typedef int BOOL;
#define TRUE    (42 == 42)
#define FALSE   (!TRUE)

#endif
/**
 * vim: tabstop=4 shiftwidth=4 expandtab softtabstop=4
 */
