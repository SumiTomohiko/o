
%include {
#include <assert.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include "o.h"
#include "o/private.h"
}

cond ::= expr. {
}
expr ::= or_expr. {
}
or_expr ::= and_expr. {
}
or_expr ::= or_expr OR and_expr. {
}
and_expr ::= not_expr. {
}
and_expr ::= and_expr AND not_expr. {
}
not_expr ::= phrases. {
}
not_expr ::= not_expr NOT fuzzy_expr. {
}
phrases ::= fuzzy_expr. {
}
phrases ::= phrases fuzzy_expr. {
}
fuzzy_expr ::= atom. {
}
fuzzy_expr ::= PHRASE QUESTION. {
}
atom ::= PHRASE. {
}
atom ::= LPAR expr RPAR. {
}

%code {
enum TokenType {
    TOKEN_PHRASE,
    TOKEN_QUESTION,
    TOKEN_AND,
    TOKEN_OR,
    TOKEN_NOT,
    TOKEN_LPAREN,
    TOKEN_RPAREN,
};

typedef enum TokenType TokenType;

struct Token {
    TokenType type;
    TCXSTR* phrase;
};

typedef struct Token Token;

static Token*
Token_new(oDB* db, TokenType type)
{
    Token* token = (Token*)malloc(sizeof(Token));
    if (token == NULL) {
        oDB_set_msg_of_errno(db, "Memory allocation failed");
        return NULL;
    }
    token->type = type;
    token->phrase = NULL;
    return token;
}

#if 0
static void
Token_delete(oDB* db, Token* token)
{
    if (token->phrase != NULL) {
        tcxstrdel(token->phrase);
    }
    free(token);
}
#endif

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
            (*token)->phrase = buf;
        }
        break;
    case '(':
        *token = Token_new(db, TOKEN_LPAREN);
        break;
    case ')':
        *token = Token_new(db, TOKEN_RPAREN);
        break;
    case '?':
        *token = Token_new(db, TOKEN_QUESTION);
        break;
    default:
        {
            TCXSTR* buf = tcxstrnew();
            while (isspace(LEXER_NEXT_CHAR(lexer))) {
                char c = LEXER_NEXT_CHAR(lexer);
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
                (*token)->phrase = buf;
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
    Token* token = NULL;
    while (Lexer_next_token(db, &lexer, &token)) {
        Parse(parser, token->type, token);
    }
    oNode* node = NULL;
    Parse(parser, 0, &node);
    ParseFree(parser, free);

    return node;
}
}

/**
 * vim: tabstop=4 shiftwidth=4 expandtab softtabstop=4
 */
