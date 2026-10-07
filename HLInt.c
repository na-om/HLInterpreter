/*
 * HLInt.c - A simple interpreter for the hypothetical language HL.
 *
 * Usage:  HLInt PROG1.HL        (or run with no argument and type the file name)
 *
 * Process:
 *   1. Reads the HL source file.
 *   2. Removes all spaces and writes the result to NOSPACES.TXT.
 *   3. Writes the reserved words and symbols found to RES_SYM.TXT.
 *   4. Prints "ERROR" if there is a syntax error, otherwise "NO ERROR(S) FOUND"
 *      and then runs the program.
 *
 * HL grammar (keywords and identifiers are case-insensitive):
 *   program     -> statement*
 *   statement   -> ID ':' type ';'                       declaration
 *                | ID (':=' | '=') expr ';'              assignment
 *                | 'output' '<<' (STRING | expr) ';'     output
 *                | 'if' '(' expr relop expr ')' statement one-way if
 *   type        -> 'integer' | 'double'
 *   expr        -> term (('+' | '-') term)*
 *   term        -> INTEGER | DOUBLE | ID
 *   relop       -> '<' | '>' | '==' | '!='
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <setjmp.h>

#define MAX_SRC    65536
#define MAX_TOKENS 4096
#define MAX_VARS   100

enum { T_ID, T_INT, T_DBL, T_STR, T_KW, T_SYM, T_END };

typedef struct { int type; char text[64]; } Token;
typedef struct { char name[64]; int isdbl; double val; } Var;

static const char *KEYWORDS[] = { "integer", "double", "output", "if" };
/* longer symbols first so ":=" is matched before ":" */
static const char *SYMBOLS[] = { ":=", "<<", "==", "!=", ":", ";", "+", "-", "(", ")", "<", ">", "=" };
#define COUNT(a) (sizeof(a) / sizeof(a[0]))

static Token tokens[MAX_TOKENS];
static int ntokens, pos;
static Var vars[MAX_VARS];
static int nvars;
static char found[64][8];      /* unique reserved words/symbols, in order found */
static int nfound;
static char errmsg[160];
static jmp_buf on_error;

static void fail(const char *fmt, const char *a, const char *b)
{
    snprintf(errmsg, sizeof errmsg, fmt, a, b);
    longjmp(on_error, 1);
}

/* ---------- lexer ---------- */

static void record_found(const char *s)
{
    for (int i = 0; i < nfound; i++)
        if (!strcmp(found[i], s)) return;
    strcpy(found[nfound++], s);
}

static void add_token(int type, const char *start, size_t len)
{
    if (ntokens >= MAX_TOKENS - 1) fail("program too long%s%s", "", "");
    if (len >= sizeof tokens[0].text) fail("token too long: '%.20s...'%s", start, "");
    Token *t = &tokens[ntokens++];
    t->type = type;
    memcpy(t->text, start, len);
    t->text[len] = '\0';
    if (type == T_ID || type == T_KW)                  /* case-insensitive */
        for (char *p = t->text; *p; p++) *p = (char)tolower((unsigned char)*p);
    if (type == T_ID)
        for (size_t k = 0; k < COUNT(KEYWORDS); k++)
            if (!strcmp(t->text, KEYWORDS[k])) t->type = T_KW;
    if (t->type == T_KW || t->type == T_SYM) record_found(t->text);
}

static void lex(const char *s)
{
    while (*s) {
        if (*s == '\n') { s++; continue; }
        const char *start = s;
        if (isalpha((unsigned char)*s) || *s == '_') {
            while (isalnum((unsigned char)*s) || *s == '_') s++;
            add_token(T_ID, start, s - start);
        } else if (isdigit((unsigned char)*s)) {
            while (isdigit((unsigned char)*s)) s++;
            if (*s == '.') {
                const char *frac = ++s;
                while (isdigit((unsigned char)*s)) s++;
                char num[64];
                snprintf(num, sizeof num, "%.*s", (int)(s - start), start);
                if (s == frac) fail("invalid number '%s'%s", num, "");
                if (s - frac > 2) fail("double '%s' has more than 2 decimal places%s", num, "");
                add_token(T_DBL, start, s - start);
            } else {
                add_token(T_INT, start, s - start);
            }
        } else if (*s == '"') {
            s++;
            while (*s && *s != '"' && *s != '\n') s++;
            if (*s != '"') fail("unterminated string%s%s", "", "");
            add_token(T_STR, start + 1, s - start - 1);
            s++;
        } else {
            size_t k;
            for (k = 0; k < COUNT(SYMBOLS); k++) {
                size_t n = strlen(SYMBOLS[k]);
                if (!strncmp(s, SYMBOLS[k], n)) { add_token(T_SYM, s, n); s += n; break; }
            }
            if (k == COUNT(SYMBOLS)) {
                char c[2] = { *s, 0 };
                fail("invalid character '%s'%s", c, "");
            }
        }
    }
    tokens[ntokens].type = T_END;
    strcpy(tokens[ntokens].text, "end of file");
}

/* ---------- parser / evaluator ---------- */

static Token *peek(void) { return &tokens[pos]; }

static int is(const char *s)
{
    Token *t = peek();
    return (t->type == T_KW || t->type == T_SYM) && !strcmp(t->text, s);
}

static void expect(const char *s)
{
    if (!is(s)) fail("expected '%s' but found '%s'", s, peek()->text);
    pos++;
}

static Var *lookup(const char *name)
{
    for (int i = 0; i < nvars; i++)
        if (!strcmp(vars[i].name, name)) return &vars[i];
    fail("undeclared variable '%s'%s", name, "");
    return NULL;
}

static double term(int *isdbl)
{
    Token *t = &tokens[pos];
    if (t->type == T_INT) { pos++; return atof(t->text); }
    if (t->type == T_DBL) { pos++; *isdbl = 1; return atof(t->text); }
    if (t->type == T_ID) {
        pos++;
        Var *v = lookup(t->text);
        if (v->isdbl) *isdbl = 1;
        return v->val;
    }
    fail("expected a number or variable but found '%s'%s", t->text, "");
    return 0;
}

static double expr(int *isdbl)
{
    *isdbl = 0;
    double v = term(isdbl);
    while (is("+") || is("-")) {
        int minus = is("-");
        pos++;
        double r = term(isdbl);
        v = minus ? v - r : v + r;
    }
    return v;
}

static void print_value(double v, int isdbl)
{
    if (isdbl) printf("%.2f\n", v);
    else       printf("%d\n", (int)v);
}

/* run == 0 means check only (first pass); run == 1 means also execute */
static void statement(int run)
{
    Token *t = peek();
    int d;

    if (is("output")) {
        pos++;
        expect("<<");
        if (peek()->type == T_STR) {
            if (run) printf("%s\n", peek()->text);
            pos++;
        } else {
            double v = expr(&d);
            if (run) print_value(v, d);
        }
        expect(";");
    } else if (is("if")) {
        pos++;
        expect("(");
        double a = expr(&d);
        const char *op = peek()->text;
        if (!(is("<") || is(">") || is("==") || is("!=")))
            fail("expected '<', '>', '==' or '!=' but found '%s'%s", op, "");
        pos++;
        double b = expr(&d);
        expect(")");
        int cond = !strcmp(op, "<") ? a < b : !strcmp(op, ">") ? a > b
                 : !strcmp(op, "==") ? a == b : a != b;
        if (peek()->type == T_END) fail("'if' is missing its statement%s%s", "", "");
        statement(run && cond);
    } else if (t->type == T_ID) {
        pos++;
        if (is(":")) {                                 /* declaration */
            pos++;
            if (!is("integer") && !is("double"))
                fail("expected 'integer' or 'double' but found '%s'%s", peek()->text, "");
            for (int i = 0; i < nvars; i++)
                if (!strcmp(vars[i].name, t->text)) fail("variable '%s' declared twice%s", t->text, "");
            if (nvars >= MAX_VARS) fail("too many variables%s%s", "", "");
            Var *v = &vars[nvars++];
            strcpy(v->name, t->text);
            v->isdbl = is("double");
            v->val = 0;
            pos++;
            expect(";");
        } else if (is(":=") || is("=")) {              /* assignment */
            pos++;
            Var *v = lookup(t->text);
            double val = expr(&d);
            if (d && !v->isdbl) fail("cannot assign a double value to integer variable '%s'%s", t->text, "");
            expect(";");
            if (run) v->val = val;
        } else {
            fail("expected ':' or ':=' after '%s' but found '%s'", t->text, peek()->text);
        }
    } else {
        fail("unexpected '%s'%s", t->text, "");
    }
}

static void program(int run)
{
    pos = 0;
    nvars = 0;
    while (peek()->type != T_END) statement(run);
}

/* ---------- main ---------- */

int main(int argc, char **argv)
{
    char filename[260];
    static char src[MAX_SRC], nospaces[MAX_SRC];

    if (argc > 1) {
        snprintf(filename, sizeof filename, "%s", argv[1]);
    } else {
        printf("Enter HL source file: ");
        if (scanf("%259s", filename) != 1) return 1;
    }

    FILE *f = fopen(filename, "r");
    if (!f) { printf("Cannot open file '%s'\n", filename); return 1; }
    size_t n = fread(src, 1, MAX_SRC - 1, f);
    src[n] = '\0';
    fclose(f);

    /* 1. remove spaces (text inside "quotes" is kept as-is) */
    int inq = 0;
    size_t j = 0;
    for (size_t i = 0; i < n; i++) {
        char c = src[i];
        if (c == '"') inq = !inq;
        if (c == '\n') inq = 0;
        if (!inq && (c == ' ' || c == '\t' || c == '\r')) continue;
        nospaces[j++] = c;
    }
    nospaces[j] = '\0';

    f = fopen("NOSPACES.TXT", "w");
    if (!f) { printf("Cannot write NOSPACES.TXT\n"); return 1; }
    fputs(nospaces, f);
    fclose(f);

    /* 2. tokenize, 3. check syntax; reserved words/symbols are recorded while tokenizing */
    int ok = 0;
    if (!setjmp(on_error)) {
        lex(nospaces);
        program(0);
        ok = 1;
    }

    f = fopen("RES_SYM.TXT", "w");
    if (!f) { printf("Cannot write RES_SYM.TXT\n"); return 1; }
    for (int i = 0; i < nfound; i++) fprintf(f, "%s\n", found[i]);
    fclose(f);

    if (!ok) {
        printf("ERROR\n(%s)\n", errmsg);
        return 1;
    }
    printf("NO ERROR(S) FOUND\n");

    /* 4. run the program */
    printf("\n--- Program output ---\n");
    if (!setjmp(on_error)) program(1);
    else printf("ERROR\n(%s)\n", errmsg);
    return 0;
}
