#include <string.h>
#include <ctype.h>
#include "engine.h"
#include "mat.h"
#include "port.h"
static MlSession *session;
static char input[192];
static size_t length;
static int pending, overflow, after_cr;
static void sink(void *ctx, const char *text) { (void)ctx; ms_write(text); }
static void prompt(void) { ms_write(pending ? "..> " : ">> "); }
static void help(void)
{
    ms_write("MatmMax Light: expressions, variables, functions, loops, small matrices.\n"
             "Limits: 16 variables, 64 elements/matrix (8x8 square), double=8 bytes.\n"
             "Platform: :help :info :mem :reset :reboot\n"
             "Esc/Ctrl+C cancels; abort ends an open block. Input: 191 characters.\n"
             "Wi-Fi off. No files, FFT, graphics or app launcher in L00.\n");
}
/* Bound syntactic recursion before entering the shared recursive parser.
 * Quotes may be strings or transpose; a quote following a value is transpose.
 * Nested function calls have their own reduced engine limits. */
static int safe_depth(const char *p)
{
    int depth=0, unary=0, quote=0, value=0;
    for (; *p; ++p) {
        int c=(unsigned char)*p;
        if (quote) {
            if (c==quote) {
                if (p[1]==quote) ++p;
                else { quote=0; value=1; }
            }
            continue;
        }
        if (c=='%') break;
        if (c=='"' || (c=='\'' && !value)) { quote=c; continue; }
        if (c=='(' || c=='[') { if (++depth>8) return 0; value=0; unary=0; }
        else if (c==')' || c==']') { if (depth>0) --depth; value=1; unary=0; }
        else if (c=='+' || c=='-' || c=='~') { if (++unary>8) return 0; value=0; }
        else if (!isspace(c)) {
            unary=0;
            if (isalnum(c) || c=='_' || c=='.' || c=='\'') value=1;
            else value=0;
        }
    }
    return 1;
}
static void evaluate(void)
{
    int result;
    input[length]='\0';
    if (overflow) ms_write("Input too long; line discarded.\n");
    else if (!strcmp(input,":mem") || !strcmp(input,":info")) ms_diagnostics();
    else if (!strcmp(input,":help") || (!pending && !strcmp(input,"help"))) help();
    else if (!strcmp(input,":reboot")) ms_restart();
    else if (!strcmp(input,":reset")) {
        if (session) ml_close(session);
        session=ml_open(sink,NULL); pending=0;
        ms_write(session ? "New MatmMax workspace.\n" : "Not enough memory for MatmMax.\n");
    } else if (*input==':') ms_write("Unknown platform command; use :help.\n");
    else if (!session) ms_write("Engine unavailable; use :mem or :reset.\n");
    else if (!safe_depth(input)) ms_write("Expression nesting/unary limit is 8; line discarded.\n");
    else {
        result=ml_eval(session,input);
        pending=result==ML_MORE;
        if (result==ML_QUIT) ms_write("MatmMax is the main prompt; workspace kept.\n");
    }
    length=0; overflow=0; prompt();
}
void ms_console_start(void)
{
    mat_poll_hook=ms_poll;
    ms_write("\nMatmMax Light L00 - ESP8266 D1 mini\n");
    ms_diagnostics();
    session=ml_open(sink,NULL);
    ms_write(session ? "Ready. Type help.\n" : "Engine allocation failed; diagnostics remain available.\n");
    prompt();
}
void ms_console_key(int c)
{
    if (c=='\n' && after_cr) { after_cr=0; return; }
    after_cr=c=='\r';
    if (c==27 || c==3) {
        length=0; overflow=0;
        if (pending && session) { ml_eval(session,"abort"); pending=0; }
        ms_write("\nCancelled.\n"); prompt();
    } else if (c=='\r' || c=='\n') { ms_write("\n"); evaluate(); }
    else if (c==8 || c==127) {
        if (length && !overflow) { --length; ms_write("\b \b"); }
    } else if (c>=32 && c<127 && !overflow) {
        if (length<sizeof(input)-1) { char s[2]; input[length++]=(char)c; s[0]=(char)c; s[1]=0; ms_write(s); }
        else overflow=1;
    }
}
