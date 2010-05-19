
%extra_argument { Arg* arg }
%token_destructor {
    Token_delete(arg->db, $$.token);
}
%token_type { Symbol }
%token_prefix TOKEN_
%include {
#include <assert.h>
#include <ctype.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "tcutil.h"
#include "o.h"
#include "o/parser.h"
#include "o/private.h"

struct Arg {
    oDB* db;
    oNode** node;
};

typedef struct Arg Arg;

static oNode*
oNode_new(oDB* db, oNodeType type)
{
    oNode* node = (oNode*)malloc(sizeof(oNode));
    if (node == NULL) {
        oDB_set_msg_of_errno(db, "Memory allocation failed");
        return NULL;
    }
    node->type = type;
    return node;
}

struct Token {
    int type;
    union {
        TCXSTR* phrase;
    } u;
};

typedef struct Token Token;

static Token*
Token_new(oDB* db, int type)
{
    Token* token = (Token*)malloc(sizeof(Token));
    if (token == NULL) {
        oDB_set_msg_of_errno(db, "Memory allocation failed");
        return NULL;
    }
    token->type = type;
    token->u.phrase = NULL;
    return token;
}

static void
Token_delete(oDB* db, Token* token)
{
    if (token->u.phrase != NULL) {
        tcxstrdel(token->u.phrase);
    }
    free(token);
}

union Symbol {
    Token* token;
    oNode* node;
};

typedef union Symbol Symbol;

static oNode*
create_logical_op_node(oDB* db, oNodeType type, oNode* left, oNode* right)
{
    oNode* node = oNode_new(db, type);
    node->u.logical_op.left = left;
    node->u.logical_op.right = right;
    return node;
}

static oNode*
create_and_node(oDB* db, oNode* left, oNode* right)
{
    return create_logical_op_node(db, NODE_AND, left, right);
}
}

cond ::= expr(A). {
    *(arg->node) = A.node;
}
expr(A) ::= or_expr(B). {
    A = B;
}
or_expr(A) ::= and_expr(B). {
    A = B;
}
or_expr(A) ::= or_expr(B) OR and_expr(C). {
    A.node = create_logical_op_node(arg->db, NODE_OR, B.node, C.node);
}
and_expr(A) ::= not_expr(B). {
    A = B;
}
and_expr(A) ::= and_expr(B) AND not_expr(C). {
    A.node = create_and_node(arg->db, B.node, C.node);
}
not_expr(A) ::= phrases(B). {
    A = B;
}
not_expr(A) ::= not_expr(B) NOT fuzzy_expr(C). {
    A.node = create_logical_op_node(arg->db, NODE_NOT, B.node, C.node);
}
phrases(A) ::= fuzzy_expr(B). {
    A = B;
}
phrases(A) ::= phrases(B) fuzzy_expr(C). {
    A.node = create_and_node(arg->db, B.node, C.node);
}
fuzzy_expr(A) ::= atom(B). {
    A = B;
}
fuzzy_expr(A) ::= PHRASE(B) QUESTION. {
    A.node = oNode_new(arg->db, NODE_FUZZY);
    A.node->u.phrase.s = B.token->u.phrase;
}
atom(A) ::= PHRASE(B). {
    A.node = oNode_new(arg->db, NODE_PHRASE);
    A.node->u.phrase.s = B.token->u.phrase;
}
atom(A) ::= LPAR expr(B) RPAR. {
    A = B;
}

%code {
struct Lexer {
    const char* input;
    unsigned int pos;
};

typedef struct Lexer Lexer;

static void
Lexer_init(oDB* db, Lexer* lexer, const char* input)
{
    lexer->input = input;
    lexer->pos = 0;
}

#define LEXER_NEXT_CHAR(lexer) (lexer)->input[(lexer)->pos]

static BOOL
Lexer_next_token(oDB* db, Lexer* lexer, Token** token)
{
    while (isspace(LEXER_NEXT_CHAR(lexer))) {
        lexer->pos++;
    }
    switch (LEXER_NEXT_CHAR(lexer)) {
    case '\"':
        lexer->pos++;
        {
            TCXSTR* buf = tcxstrnew();
            while ((LEXER_NEXT_CHAR(lexer) != '\"') && (LEXER_NEXT_CHAR(lexer) != '\0')) {
                if (LEXER_NEXT_CHAR(lexer) == '\\') {
                    lexer->pos++;
                }
                char c = LEXER_NEXT_CHAR(lexer);
                tcxstrcat(buf, &c, sizeof(c));
                lexer->pos++;
            }
            *token = Token_new(db, TOKEN_PHRASE);
            (*token)->u.phrase = buf;
        }
        break;
    case '(':
        lexer->pos++;
        *token = Token_new(db, TOKEN_LPAR);
        break;
    case ')':
        lexer->pos++;
        *token = Token_new(db, TOKEN_RPAR);
        break;
    case '?':
        lexer->pos++;
        *token = Token_new(db, TOKEN_QUESTION);
        break;
    case '\0':
        return FALSE;
        break;
    default:
        {
            TCXSTR* buf = tcxstrnew();
            while (1) {
                char c = LEXER_NEXT_CHAR(lexer);
                if ((c == '\0') || (c == '(') || (c == ')') || (c == '?') || isspace(c)) {
                    break;
                }
                tcxstrcat(buf, &c, sizeof(c));
                lexer->pos++;
            }
            const char* s = tcxstrptr(buf);
            if (strcasecmp(s, "and") == 0) {
                *token = Token_new(db, TOKEN_AND);
                tcxstrdel(buf);
            }
            else if (strcasecmp(s, "or") == 0) {
                *token = Token_new(db, TOKEN_OR);
                tcxstrdel(buf);
            }
            else if (strcasecmp(s, "not") == 0) {
                *token = Token_new(db, TOKEN_NOT);
                tcxstrdel(buf);
            }
            else {
                *token = Token_new(db, TOKEN_PHRASE);
                (*token)->u.phrase = buf;
            }
        }
        break;
    }
    return TRUE;
}

oNode*
oParser_parse(oDB* db, const char* cond)
{
    yyParser* parser = ParseAlloc(malloc);
    Lexer lexer;
    Lexer_init(db, &lexer, cond);
    Symbol symbol;
    oNode* node = NULL;
    Arg arg = { db, &node };
#if 0
    ParseTrace(stdout, "parser: ");
#endif
    while (Lexer_next_token(db, &lexer, &symbol.token)) {
        Parse(parser, symbol.token->type, symbol, &arg);
    }
    symbol.token = NULL;
    Parse(parser, 0, symbol, &arg);
    ParseFree(parser, free);

    return node;
}
}

/**
 * vim: tabstop=4 shiftwidth=4 expandtab softtabstop=4
 */
