#if !defined(O_PRIVATE_INCLUDED)
#define O_PRIVATE_INCLUDED

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include "tcutil.h"
#include "o.h"

enum oNodeType {
    NODE_PHRASE,
    NODE_FUZZY,
    NODE_AND,
    NODE_OR,
    NODE_NOT,
};

typedef enum oNodeType oNodeType;

struct oNode {
    oNodeType type;
    union {
        struct {
            TCXSTR* s;
        } phrase;
        struct {
            struct oNode* left;
            struct oNode* right;
        } binop;
    } u;
};

typedef struct oNode oNode;

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

oNode* oParser_parse(oDB* db, const char* cond);

#endif
/**
 * vim: tabstop=4 shiftwidth=4 expandtab softtabstop=4
 */
