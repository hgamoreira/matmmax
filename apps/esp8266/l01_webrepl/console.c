#include <string.h>
#include <ctype.h>
#include "engine.h"
#include "mat.h"
#include "port.h"
#include "web.h"
static MlSession *session;
static char input[192];
static size_t length;
static int pending, overflow, after_cr;
static int last_result;
static int wifi_setup;
static char wifi_ssid[33];
static void sink(void *ctx, const char *text) { (void)ctx; ms_write(text); }
static void prompt(void)
{
    ms_write(wifi_setup==1 ? "Wi-Fi SSID (visible): " : wifi_setup==2 ?
             "Wi-Fi password (hidden; Enter for open network): " : pending ? "..> " : ">> ");
}
static void help(void)
{
    ms_write("MathSmith Light: expressions, variables, functions, loops, small matrices.\n"
             "Limits: 16 variables, 64 elements/matrix (8x8 square), double=8 bytes.\n"
             "Platform: :help :info :mem :reset :reboot :wifi :web :pins\n"
             "GPIO: :pin Dn input|pullup|output [0|1], :read Dn, :write Dn 0|1, :analog A0\n"
             "Programs: pinmode, digitalwrite, digitalread, analogread, delay(ms<=60000).\n"
             "Esc/Ctrl+C cancels; abort ends an open block. Input: 191 characters.\n"
             "Wi-Fi: :wifi join saves after connection; :wifi forget clears saved settings.\n"
             "Wi-Fi diagnostics: :wifi diag reads station/AP and DHCP client state.\n"
             "Serial arrows are ignored; use Backspace. Browser key stays across reboot.\n"
             "L01 browser console + program editor; script files stay on your computer.\n"
             "No board files, FFT, graphics or app launcher.\n");
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
    last_result=0;
    input[length]='\0';
    if (overflow) { last_result=1; ms_write("Input too long; line discarded.\n"); }
    else if (wifi_setup==1) {
        if (!length || length>32) ms_write("SSID must contain 1..32 characters. Try again.\n");
        else { memcpy(wifi_ssid,input,length+1); wifi_setup=2; }
    } else if (wifi_setup==2) {
        if (length>63) ms_write("Password must contain at most 63 characters. Try again.\n");
        else {
            ms_wifi_join_network(wifi_ssid,input);
            memset(wifi_ssid,0,sizeof(wifi_ssid)); wifi_setup=0;
        }
    }
    else if (ms_platform_command(input)) { }
    else if (!strcmp(input,":mem") || !strcmp(input,":info")) ms_diagnostics();
    else if (!strcmp(input,":help") || (!pending && !strcmp(input,"help"))) help();
    else if (!strcmp(input,":reboot")) ms_restart();
    else if (!strcmp(input,":reset")) {
        if (session) ml_close(session);
        session=ml_open(sink,NULL); pending=0;
        last_result=session ? 0 : 1;
        ms_write(session ? "New MathSmith workspace.\n" : "Not enough memory for MathSmith.\n");
    } else if (*input==':') { last_result=1; ms_write("Unknown platform command; use :help.\n"); }
    else if (!session) { last_result=1; ms_write("Engine unavailable; use :mem or :reset.\n"); }
    else if (!safe_depth(input)) { last_result=1; ms_write("Expression nesting/unary limit is 8; line discarded.\n"); }
    else {
        result=ml_eval(session,input);
        last_result=result==ML_ERROR || result==ML_QUIT;
        pending=result==ML_MORE;
        if (result==ML_QUIT) ms_write("MathSmith is the main prompt; workspace kept.\n");
    }
    memset(input,0,sizeof(input)); length=0; overflow=0; prompt();
}
void ms_console_start(void)
{
    mat_poll_hook=ms_poll;
    ms_write("\nMathSmith Light " MS_WEB_BUILD " - ESP8266 browser + GPIO\n");
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
        last_result=2;
        length=0; overflow=0;
        memset(input,0,sizeof(input)); memset(wifi_ssid,0,sizeof(wifi_ssid)); wifi_setup=0;
        if (pending && session) { ml_eval(session,"abort"); pending=0; }
        ms_write("\nCancelled.\n"); prompt();
    } else if (c=='\r' || c=='\n') { ms_write("\n"); evaluate(); }
    else if (c==8 || c==127) {
        if (length && !overflow) { input[--length]=0; ms_write("\b \b"); }
    } else if (c>=32 && c<127 && !overflow) {
        if (length<sizeof(input)-1) {
            char s[2]; input[length++]=(char)c; input[length]=0;
            const char *secret=NULL;
            if (wifi_setup==2) secret=input;
            else if (!wifi_setup && !strncmp(input,":wifi join ",11)) {
                /* Leave the inline SSID visible; mask only the password field. */
                const char *p=input+11;
                while (*p==' ') ++p;
                if (*p=='\'' || *p=='"') {
                    char q=*p++;
                    while (*p && *p!=q) { if (*p=='\\' && p[1]) ++p; ++p; }
                    if (*p==q) ++p;
                } else while (*p && *p!=' ') ++p;
                if (*p==' ') { while (*p==' ') ++p; secret=p; }
            }
            s[0]=secret && input+length-1>=secret ? '*' : (char)c;
            s[1]=0; ms_write(s);
        }
        else overflow=1;
    }
}
int ms_console_pending(void) { return pending; }
int ms_console_result(void) { return last_result; }
int ms_console_setup_active(void) { return wifi_setup!=0; }
int ms_console_wifi_setup(void)
{
    if (pending) { ms_write("Use abort to finish the open MathSmith block before Wi-Fi setup.\n"); return 0; }
    wifi_setup=1; memset(wifi_ssid,0,sizeof(wifi_ssid));
    ms_write("Wi-Fi setup: enter SSID, then password, each followed by Enter.\n"
             "SSID is visible; password shows *. Backspace edits; Ctrl+C/Esc cancels.\n");
    return 1;
}
