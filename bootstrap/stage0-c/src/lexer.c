#include "basalt.h"

const char *token_kind_name(TokenKind k) {
    static const char *names[] = {
        [TOK_EOF] = "EOF",
        [TOK_ERROR] = "ERROR",
        [TOK_INT] = "INT",
        [TOK_FLOAT] = "FLOAT",
        [TOK_STRING] = "STRING",
        [TOK_CHAR] = "CHAR",
        [TOK_IDENT] = "IDENT",
        [TOK_BOX] = "box",
        [TOK_CONST_BOX] = "const_box",
        [TOK_SAFE] = "safe",
        [TOK_FREE] = "free",
        [TOK_RETURN] = "return",
        [TOK_IF] = "if",
        [TOK_ELSE] = "else",
        [TOK_WHILE] = "while",
        [TOK_FOR] = "for",
        [TOK_SWITCH] = "switch",
        [TOK_CASE] = "case",
        [TOK_DEFAULT] = "default",
        [TOK_BREAK] = "break",
        [TOK_CONTINUE] = "continue",
        [TOK_PANIC] = "panic",
        [TOK_IMPORT] = "import",
        [TOK_TRUE] = "true",
        [TOK_FALSE] = "false",
        [TOK_AT_CPU] = "@cpu",
        [TOK_AT_GPU] = "@gpu",
        [TOK_AT_SAFE] = "@safe",
        [TOK_AT_TRUSTED] = "@trusted",
        [TOK_AT_MACRO] = "@macro",
        [TOK_AT_IMPORT] = "@import",
        [TOK_INT_TYPE] = "int",
        [TOK_FLOAT_TYPE] = "float",
        [TOK_STRING_TYPE] = "string",
        [TOK_VOID] = "void",
        [TOK_PLUS] = "+",
        [TOK_MINUS] = "-",
        [TOK_STAR] = "*",
        [TOK_SLASH] = "/",
        [TOK_PERCENT] = "%",
        [TOK_AMP] = "&",
        [TOK_PIPE] = "|",
        [TOK_CARET] = "^",
        [TOK_TILDE] = "~",
        [TOK_LSHIFT] = "<<",
        [TOK_RSHIFT] = ">>",
        [TOK_EQ] = "==",
        [TOK_NEQ] = "!=",
        [TOK_LT] = "<",
        [TOK_GT] = ">",
        [TOK_LE] = "<=",
        [TOK_GE] = ">=",
        [TOK_AND] = "&&",
        [TOK_OR] = "||",
        [TOK_NOT] = "!",
        [TOK_ASSIGN] = "=",
        [TOK_ARROW] = "->",
        [TOK_INTO] = "-into",
        [TOK_COLON] = ":",
        [TOK_COMMA] = ",",
        [TOK_DOT] = ".",
        [TOK_LPAREN] = "(",
        [TOK_RPAREN] = ")",
        [TOK_LBRACK] = "[",
        [TOK_RBRACK] = "]",
        [TOK_LBRACE] = "{",
        [TOK_RBRACE] = "}",
        [TOK_SEMI] = ";",
    };
    if (k < 0 || k >= TOK_COUNT) return "?";
    return names[k] ? names[k] : "?";
}

typedef struct {
    Source *src;
    const char *p;
    const char *end;
    int line;
    int col;
    Token *toks;
    int ntoks;
    int cap;
} Lexer;

static void lex_add(Lexer *L, Token t) {
    if (L->ntoks >= L->cap) {
        L->cap = L->cap ? L->cap * 2 : 256;
        L->toks = xrealloc(L->toks, L->cap * sizeof(Token));
    }
    L->toks[L->ntoks++] = t;
}

static void skip_ws_and_comments(Lexer *L) {
    for (;;) {
        while (L->p < L->end && isspace((unsigned char)*L->p)) {
            if (*L->p == '\n') { L->line++; L->col = 1; }
            else L->col++;
            L->p++;
        }
        if (L->p + 1 < L->end && L->p[0] == '/' && L->p[1] == '/') {
            while (L->p < L->end && *L->p != '\n') { L->p++; L->col++; }
            continue;
        }
        if (L->p + 1 < L->end && L->p[0] == '/' && L->p[1] == '*') {
            L->p += 2; L->col += 2;
            while (L->p + 1 < L->end && !(L->p[0] == '*' && L->p[1] == '/')) {
                if (*L->p == '\n') { L->line++; L->col = 1; }
                else L->col++;
                L->p++;
            }
            if (L->p + 1 < L->end) { L->p += 2; L->col += 2; }
            continue;
        }
        break;
    }
}

static TokenKind keyword_or_ident(const char *s, size_t n) {
    #define KW(str, tok) if (n == sizeof(str)-1 && !memcmp(s, str, n)) return tok
    KW("box", TOK_BOX);
    KW("const_box", TOK_CONST_BOX);
    KW("safe", TOK_SAFE);
    KW("free", TOK_FREE);
    KW("return", TOK_RETURN);
    KW("if", TOK_IF);
    KW("else", TOK_ELSE);
    KW("while", TOK_WHILE);
    KW("for", TOK_FOR);
    KW("switch", TOK_SWITCH);
    KW("case", TOK_CASE);
    KW("default", TOK_DEFAULT);
    KW("break", TOK_BREAK);
    KW("continue", TOK_CONTINUE);
    KW("panic", TOK_PANIC);
    KW("true", TOK_TRUE);
    KW("false", TOK_FALSE);
    KW("int", TOK_INT_TYPE);
    KW("float", TOK_FLOAT_TYPE);
    KW("string", TOK_STRING_TYPE);
    KW("void", TOK_VOID);
    #undef KW
    return TOK_IDENT;
}

static void lex_number(Lexer *L) {
    const char *start = L->p;
    int line = L->line, col = L->col;
    bool is_float = false;
    int base = 10;

    if (*L->p == '0' && (L->p[1] == 'x' || L->p[1] == 'X')) {
        base = 16; L->p += 2; L->col += 2;
    } else if (*L->p == '0' && (L->p[1] == 'b' || L->p[1] == 'B')) {
        base = 2; L->p += 2; L->col += 2;
    }

    while (L->p < L->end) {
        char c = *L->p;
        if (base == 16 && isxdigit((unsigned char)c)) { L->p++; L->col++; }
        else if (base == 2 && (c == '0' || c == '1')) { L->p++; L->col++; }
        else if (base == 10 && isdigit((unsigned char)c)) { L->p++; L->col++; }
        else if (base == 10 && c == '.' && !is_float) {
            is_float = true; L->p++; L->col++;
        } else if (base == 10 && (c == 'e' || c == 'E')) {
            is_float = true; L->p++; L->col++;
            if (L->p < L->end && (*L->p == '+' || *L->p == '-')) { L->p++; L->col++; }
        } else break;
    }

    Token t = {0};
    t.start = start;
    t.len = (size_t)(L->p - start);
    t.line = line;
    t.col = col;

    if (is_float) {
        t.kind = TOK_FLOAT;
        t.val.f64 = strtod(start, NULL);
    } else {
        t.kind = TOK_INT;
        t.val.i64 = strtoll(start, NULL, base);
    }
    lex_add(L, t);
}

static void lex_string(Lexer *L) {
    const char *start = L->p;
    int line = L->line, col = L->col;
    L->p++; L->col++; /* skip " */

    char buf[4096];
    size_t bi = 0;

    while (L->p < L->end && *L->p != '"') {
        if (*L->p == '\\' && L->p + 1 < L->end) {
            L->p++; L->col++;
            char c = *L->p++;
            L->col++;
            switch (c) {
                case 'n': buf[bi++] = '\n'; break;
                case 't': buf[bi++] = '\t'; break;
                case 'r': buf[bi++] = '\r'; break;
                case '\\': buf[bi++] = '\\'; break;
                case '"': buf[bi++] = '"'; break;
                case '0': buf[bi++] = '\0'; break;
                default: buf[bi++] = c; break;
            }
        } else {
            if (*L->p == '\n') { L->line++; L->col = 1; }
            else L->col++;
            buf[bi++] = *L->p++;
        }
        if (bi >= sizeof(buf) - 1) die("string too long");
    }
    if (L->p >= L->end || *L->p != '"')
        error_at(line, col, "unterminated string");
    L->p++; L->col++;

    Token t = {0};
    t.kind = TOK_STRING;
    t.start = start;
    t.len = (size_t)(L->p - start);
    t.line = line;
    t.col = col;
    t.val.str = xstrndup(buf, bi);
    lex_add(L, t);
}

static void lex_char(Lexer *L) {
    const char *start = L->p;
    int line = L->line, col = L->col;
    L->p++; L->col++; /* ' */

    int64_t val = 0;
    if (*L->p == '\\') {
        L->p++; L->col++;
        switch (*L->p) {
            case 'n': val = '\n'; break;
            case 't': val = '\t'; break;
            case 'r': val = '\r'; break;
            case '\\': val = '\\'; break;
            case '\'': val = '\''; break;
            case '0': val = 0; break;
            default: val = *L->p; break;
        }
        L->p++; L->col++;
    } else {
        val = (unsigned char)*L->p;
        L->p++; L->col++;
    }
    if (*L->p != '\'')
        error_at(line, col, "unterminated or invalid char literal");
    L->p++; L->col++;

    Token t = {0};
    t.kind = TOK_CHAR;
    t.start = start;
    t.len = (size_t)(L->p - start);
    t.line = line;
    t.col = col;
    t.val.i64 = val;
    lex_add(L, t);
}

Token *lex(Source *src, int *out_ntoks) {
    Lexer L = {0};
    L.src = src;
    L.p = src->src;
    L.end = src->src + src->len;
    L.line = 1;
    L.col = 1;
    L.cap = 256;
    L.toks = xmalloc(L.cap * sizeof(Token));

    while (L.p < L.end) {
        skip_ws_and_comments(&L);
        if (L.p >= L.end) break;

        const char *start = L.p;
        int line = L.line, col = L.col;
        char c = *L.p;

        /* @decorators */
        if (c == '@') {
            L.p++; L.col++;
            const char *idstart = L.p;
            while (L.p < L.end && (isalnum((unsigned char)*L.p) || *L.p == '_')) {
                L.p++; L.col++;
            }
            size_t n = (size_t)(L.p - idstart);
            Token t = {0};
            t.start = start;
            t.len = (size_t)(L.p - start);
            t.line = line;
            t.col = col;
            if (n == 3 && !memcmp(idstart, "cpu", 3)) t.kind = TOK_AT_CPU;
            else if (n == 3 && !memcmp(idstart, "gpu", 3)) t.kind = TOK_AT_GPU;
            else if (n == 4 && !memcmp(idstart, "safe", 4)) t.kind = TOK_AT_SAFE;
            else if (n == 7 && !memcmp(idstart, "trusted", 7)) t.kind = TOK_AT_TRUSTED;
            else if (n == 5 && !memcmp(idstart, "macro", 5)) t.kind = TOK_AT_MACRO;
            else if (n == 6 && !memcmp(idstart, "import", 6)) t.kind = TOK_AT_IMPORT;
            else {
                t.kind = TOK_ERROR;
                error_at(line, col, "unknown decorator @%.*s", (int)n, idstart);
            }
            lex_add(&L, t);
            continue;
        }

        /* identifiers / keywords */
        if (isalpha((unsigned char)c) || c == '_') {
            while (L.p < L.end && (isalnum((unsigned char)*L.p) || *L.p == '_')) {
                L.p++; L.col++;
            }
            size_t n = (size_t)(L.p - start);
            Token t = {0};
            t.kind = keyword_or_ident(start, n);
            t.start = start;
            t.len = n;
            t.line = line;
            t.col = col;
            lex_add(&L, t);
            continue;
        }

        /* numbers */
        if (isdigit((unsigned char)c)) {
            lex_number(&L);
            continue;
        }

        /* strings */
        if (c == '"') {
            lex_string(&L);
            continue;
        }

        /* chars */
        if (c == '\'') {
            lex_char(&L);
            continue;
        }

        /* multi-char operators */
        if (c == '-' && L.p + 4 < L.end && !memcmp(L.p, "-into", 5) &&
            !isalnum((unsigned char)L.p[5]) && L.p[5] != '_') {
            Token t = {0};
            t.kind = TOK_INTO;
            t.start = start;
            t.len = 5;
            t.line = line;
            t.col = col;
            L.p += 5; L.col += 5;
            lex_add(&L, t);
            continue;
        }
        if (c == '-' && L.p[1] == '>') {
            Token t = {TOK_ARROW, start, 2, line, col};
            L.p += 2; L.col += 2;
            lex_add(&L, t);
            continue;
        }
        if (c == '<' && L.p[1] == '<') {
            Token t = {TOK_LSHIFT, start, 2, line, col};
            L.p += 2; L.col += 2;
            lex_add(&L, t);
            continue;
        }
        if (c == '>' && L.p[1] == '>') {
            Token t = {TOK_RSHIFT, start, 2, line, col};
            L.p += 2; L.col += 2;
            lex_add(&L, t);
            continue;
        }
        if (c == '=' && L.p[1] == '=') {
            Token t = {TOK_EQ, start, 2, line, col};
            L.p += 2; L.col += 2;
            lex_add(&L, t);
            continue;
        }
        if (c == '!' && L.p[1] == '=') {
            Token t = {TOK_NEQ, start, 2, line, col};
            L.p += 2; L.col += 2;
            lex_add(&L, t);
            continue;
        }
        if (c == '<' && L.p[1] == '=') {
            Token t = {TOK_LE, start, 2, line, col};
            L.p += 2; L.col += 2;
            lex_add(&L, t);
            continue;
        }
        if (c == '>' && L.p[1] == '=') {
            Token t = {TOK_GE, start, 2, line, col};
            L.p += 2; L.col += 2;
            lex_add(&L, t);
            continue;
        }
        if (c == '&' && L.p[1] == '&') {
            Token t = {TOK_AND, start, 2, line, col};
            L.p += 2; L.col += 2;
            lex_add(&L, t);
            continue;
        }
        if (c == '|' && L.p[1] == '|') {
            Token t = {TOK_OR, start, 2, line, col};
            L.p += 2; L.col += 2;
            lex_add(&L, t);
            continue;
        }

        /* single char */
        TokenKind k = TOK_ERROR;
        switch (c) {
            case '+': k = TOK_PLUS; break;
            case '-': k = TOK_MINUS; break;
            case '*': k = TOK_STAR; break;
            case '/': k = TOK_SLASH; break;
            case '%': k = TOK_PERCENT; break;
            case '&': k = TOK_AMP; break;
            case '|': k = TOK_PIPE; break;
            case '^': k = TOK_CARET; break;
            case '~': k = TOK_TILDE; break;
            case '<': k = TOK_LT; break;
            case '>': k = TOK_GT; break;
            case '!': k = TOK_NOT; break;
            case '=': k = TOK_ASSIGN; break;
            case ':': k = TOK_COLON; break;
            case ',': k = TOK_COMMA; break;
            case '.': k = TOK_DOT; break;
            case '(': k = TOK_LPAREN; break;
            case ')': k = TOK_RPAREN; break;
            case '[': k = TOK_LBRACK; break;
            case ']': k = TOK_RBRACK; break;
            case '{': k = TOK_LBRACE; break;
            case '}': k = TOK_RBRACE; break;
            case ';': k = TOK_SEMI; break;
            default:
                error_at(line, col, "unexpected character '%c'", c);
        }
        Token t = {k, start, 1, line, col};
        L.p++; L.col++;
        lex_add(&L, t);
    }

    Token eof = {TOK_EOF, L.p, 0, L.line, L.col};
    lex_add(&L, eof);
    *out_ntoks = L.ntoks;
    return L.toks;
}
