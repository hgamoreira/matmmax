/* L02 program store (XD118): names, limits, console commands, the engine's
 * read-only stream and the browser's atomic save. No allocation of its own;
 * the backend owns file handles. */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include "store.h"
#include "engine.h"
#include "port.h"

static void *save_file;
static size_t save_bytes;
static char save_name[MS_STORE_NAME_MAX+1];

static int is_letter(int c) { return (c>='a' && c<='z') || (c>='A' && c<='Z'); }
static int is_digit(int c) { return c>='0' && c<='9'; }
static int lower(int c) { return c>='A' && c<='Z' ? c-'A'+'a' : c; }

/* A program name: letter, then letters/digits/_; optional .m; lower-cased.
 * The same rule makes every saved program runnable by its bare name. */
static int valid_name(const char *in, char out[MS_STORE_NAME_MAX+1])
{
    size_t n=strlen(in), i;
    if (n>2 && in[n-2]=='.' && lower((unsigned char)in[n-1])=='m') n-=2;
    if (!n || n>MS_STORE_NAME_MAX || !is_letter((unsigned char)in[0])) return 0;
    for (i=0;i<n;++i) {
        int c=(unsigned char)in[i];
        if (!is_letter(c) && !is_digit(c) && c!='_') return 0;
        out[i]=(char)lower(c);
    }
    out[n]=0;
    return 1;
}
int ms_store_path(const char *name, char path[MS_STORE_NAME_MAX+4])
{
    char base[MS_STORE_NAME_MAX+1];
    if (!valid_name(name,base)) return 0;
    snprintf(path,MS_STORE_NAME_MAX+4,"/%s.m",base);
    return 1;
}

/* --- the engine's stream: fopen(name,"r") in the generated L02 profile --- */
static ssize_t cookie_read(void *cookie, char *buffer, size_t n)
{
    long got=ms_fs_read(cookie,buffer,n);
    return got<0 ? -1 : (ssize_t)got;
}
static int cookie_close(void *cookie) { ms_fs_close(cookie); return 0; }
FILE *ms_store_fopen(const char *name, const char *mode)
{
    char path[MS_STORE_NAME_MAX+4];
    cookie_io_functions_t io;
    void *file;
    FILE *f;
    if (strcmp(mode,"r") || !ms_fs_mounted() || !ms_store_path(name,path)) return NULL;
    file=ms_fs_open(path,0);
    if (!file) return NULL;
    memset(&io,0,sizeof(io));
    io.read=cookie_read; io.close=cookie_close;
    f=fopencookie(file,"r",io);
    if (!f) { ms_fs_close(file); return NULL; }
    setvbuf(f,NULL,_IOFBF,128);   /* bounded C-library buffer while a script is read */
    return f;
}

/* --- sorted listing without a table: repeated passes for the next name --- */
typedef struct { char prev[MS_STORE_NAME_MAX+1], best[MS_STORE_NAME_MAX+1]; long size; int started, found; } Pick;
static int pick(void *ctx, const char *name, long size)
{
    Pick *p=(Pick *)ctx;
    char base[MS_STORE_NAME_MAX+1], check[MS_STORE_NAME_MAX+1];
    size_t n=strlen(name);
    if (n<3 || n>MS_STORE_NAME_MAX+2 || strcmp(name+n-2,".m")) return 1;
    memcpy(base,name,n-2); base[n-2]=0;
    if (!valid_name(base,check) || strcmp(base,check)) return 1;  /* only names save could create */
    if (p->started && strcmp(base,p->prev)<=0) return 1;
    if (!p->found || strcmp(base,p->best)<0) { strcpy(p->best,base); p->size=size; p->found=1; }
    return 1;
}
static int next_entry(Pick *p)
{
    p->found=0;
    if (!ms_fs_each(pick,p)) return -1;
    if (p->found) { strcpy(p->prev,p->best); p->started=1; }
    return p->found;
}

static const char *unavailable="error: the program store is not available; see :store\n";

static int list_console(void)
{
    Pick p; char line[96]; int count=0, r;
    unsigned long used=0, total=0;
    if (!ms_fs_mounted()) { ms_write(unavailable); return 2; }
    memset(&p,0,sizeof(p));
    while ((r=next_entry(&p))>0) {
        snprintf(line,sizeof(line),"  %-24s %6ld\n",p.best,p.size); ms_write(line); ++count;
    }
    if (r<0) { ms_write("error: could not read the program store\n"); return 2; }
    if (!count) ms_write("  (no saved programs; Save to board in the browser editor)\n");
    if (ms_fs_usage(&used,&total)) {
        snprintf(line,sizeof(line),"  %d program%s; %lu of %lu KiB used\n",count,count==1 ? "" : "s",
                 (used+1023)/1024,total/1024);
        ms_write(line);
    }
    return 1;
}
static void console_sink(const char *text, size_t n)
{
    char piece[129];
    while (n) {
        size_t k=n>128 ? 128 : n;
        memcpy(piece,text,k); piece[k]=0; ms_write(piece);
        text+=k; n-=k;
    }
}
static int last_newline;
static void type_sink(const char *text, size_t n) { if (n) { console_sink(text,n); last_newline=text[n-1]=='\n'; } }

static int read_into(const char *path, void (*sink)(const char *, size_t))
{
    char buffer[128];
    long got;
    void *file=ms_fs_open(path,0);
    if (!file) return 0;
    while ((got=ms_fs_read(file,buffer,sizeof(buffer)))>0) sink(buffer,(size_t)got);
    ms_fs_close(file);
    return got==0;
}

static int store_admin(const char *rest)
{
    char line[200]; unsigned long used=0, total=0; Pick p; int count=0;
    while (*rest==' ') ++rest;
    if (!*rest) {
        if (!ms_fs_layout_ok()) { ms_write("Program store: disabled (flash layout differs from this build).\n"); return 2; }
        if (!ms_fs_mounted()) {
            ms_write("Program store: not available (not formatted or unreadable).\n"
                     ":store format confirm erases it and starts an empty store.\n");
            return 2;
        }
        memset(&p,0,sizeof(p));
        while (next_entry(&p)>0) ++count;
        ms_fs_usage(&used,&total);
        snprintf(line,sizeof(line),"Program store: LittleFS, %d program%s, %lu of %lu KiB used.\n"
                 "Up to %u bytes per program; names: letter, then letters/digits/_ (max %u).\n",
                 count,count==1 ? "" : "s",(used+1023)/1024,total/1024,(unsigned)MS_STORE_FILE_MAX,(unsigned)MS_STORE_NAME_MAX);
        ms_write(line);
        return 1;
    }
    if (!strcmp(rest,"format")) {
        ms_write("This erases every saved program. Type :store format confirm to proceed.\n");
        return 1;
    }
    if (!strcmp(rest,"format confirm")) {
        ms_store_save_abort();
        if (!ms_fs_layout_ok() || !ms_fs_format()) { ms_write("error: formatting the program store failed\n"); return 2; }
        ms_write("Program store formatted; it is empty.\n");
        return 1;
    }
    ms_write("error: :store, :store format confirm\n");
    return 2;
}

int ms_store_command(void *session, const char *line)
{
    char word[16], arg[40], extra[2], path[MS_STORE_NAME_MAX+4];
    int n;
    while (*line==' ') ++line;
    if (!strncmp(line,":store",6) && (!line[6] || line[6]==' ')) return store_admin(line+6);
    n=sscanf(line,"%15s %39s %1s",word,arg,extra);
    if (n<1) return 0;
    if (!strcmp(word,"ls") || !strcmp(word,"dir")) {
        if (n!=1 || (session && ms_engine_is_variable(session,word))) return 0;
        return list_console();
    }
    if (strcmp(word,"type") && strcmp(word,"rm") && strcmp(word,"delete")) return 0;
    if (n>2 || (session && ms_engine_is_variable(session,word))) return 0;
    if (n==1 || !ms_store_path(arg,path)) {
        ms_write(!strcmp(word,"type") ? "error: type NAME shows a saved program\n" :
                 "error: rm NAME deletes a saved program\n");
        if (n==2) ms_write("error: names are a letter, then letters, digits or _ (at most 24)\n");
        return 2;
    }
    if (!ms_fs_mounted()) { ms_write(unavailable); return 2; }
    if (ms_fs_size(path)<0) { ms_write("error: no saved program "); ms_write(path+1); ms_write("\n"); return 2; }
    if (!strcmp(word,"type")) {
        last_newline=1;
        if (!read_into(path,type_sink)) { ms_write("\nerror: could not read the program\n"); return 2; }
        if (!last_newline) ms_write("\n");
        return 1;
    }
    if (!ms_fs_remove(path)) { ms_write("error: could not delete the program\n"); return 2; }
    if (session) ml_file_changed((MlSession *)session,path+1);
    ms_write("Deleted "); ms_write(path+1); ms_write(".\n");
    return 1;
}

void ms_store_start(void)
{
    if (!ms_fs_layout_ok()) { ms_write("Program store: disabled (flash layout differs from this build).\n"); return; }
    if (ms_fs_mount()) { ms_write("Program store ready: ls lists programs, run NAME runs one.\n"); return; }
    if (ms_fs_blank()) {
        if (ms_fs_format()) ms_write("Program store: erased flash found; formatted an empty store.\n");
        else ms_write("Program store: formatting erased flash failed; :store for details.\n");
        return;
    }
    ms_write("Program store: not readable. :store format confirm erases it and starts empty.\n");
}

/* --- browser save: temporary file, then an atomic rename over name.m --- */
int ms_store_saving(void) { return save_name[0]!=0; }
/* A save is active from a valid begin until end succeeds; until then the
 * temporary file is removed on any failure, including after it was closed. */
void ms_store_save_abort(void)
{
    if (save_file) { ms_fs_close(save_file); save_file=NULL; }
    if (save_name[0]) ms_fs_remove(MS_STORE_TEMP);
    save_bytes=0; save_name[0]=0;
}
static int save_fail(const char *message)
{
    ms_store_save_abort();
    ms_write("error: "); ms_write(message); ms_write("; nothing was saved\n");
    return 0;
}
int ms_store_save_begin(const char *name)
{
    char checked[MS_STORE_NAME_MAX+1];
    ms_store_save_abort();
    if (!valid_name(name,checked)) return save_fail("names are a letter, then letters, digits or _ (at most 24)");
    if (!ms_fs_mounted()) return save_fail("the program store is not available (see :store)");
    strcpy(save_name,checked);
    ms_fs_remove(MS_STORE_TEMP);
    save_file=ms_fs_open(MS_STORE_TEMP,1);
    if (!save_file) return save_fail("could not create a file in the program store");
    return 1;
}
int ms_store_save_data(const char *data, size_t n)
{
    size_t i;
    if (!save_file) return save_fail("no save in progress");
    if (n>MS_STORE_FILE_MAX-save_bytes) return save_fail("a program is limited to 16384 bytes");
    for (i=0;i<n;++i) {
        unsigned char c=(unsigned char)data[i];
        if ((c<32 || c>126) && c!='\n') return save_fail("programs are printable ASCII lines");
    }
    if (ms_fs_write(save_file,data,n)!=(long)n) return save_fail("flash write failed (is the store full?)");
    save_bytes+=n;
    return 1;
}
int ms_store_save_end(void *session)
{
    char path[MS_STORE_NAME_MAX+4], line[96];
    size_t bytes=save_bytes;
    if (!save_file) return save_fail("no save in progress");
    ms_fs_close(save_file); save_file=NULL;      /* save_name stays set: failures below remove the temp */
    snprintf(path,sizeof(path),"/%s.m",save_name);
    if (ms_fs_size(MS_STORE_TEMP)!=(long)bytes) return save_fail("the written file could not be verified");
    if (!ms_fs_rename(MS_STORE_TEMP,path)) return save_fail("could not replace the saved program");
    if (session) ml_file_changed((MlSession *)session,save_name);
    snprintf(line,sizeof(line),"Saved %s (%u bytes).\n",path+1,(unsigned)bytes);
    ms_write(line);
    save_bytes=0; save_name[0]=0;
    return 1;
}

/* --- browser read and list: raw text only, no messages (caller reports) --- */
int ms_store_read(const char *name, void (*sink)(const char *, size_t))
{
    char path[MS_STORE_NAME_MAX+4];
    if (!ms_fs_mounted() || !ms_store_path(name,path)) return 0;
    return read_into(path,sink);
}
int ms_store_list(void (*sink)(const char *, size_t))
{
    Pick p; char line[48]; int r, n;
    if (!ms_fs_mounted()) return 0;
    memset(&p,0,sizeof(p));
    while ((r=next_entry(&p))>0) {
        n=snprintf(line,sizeof(line),"%s %ld\n",p.best,p.size);
        sink(line,(size_t)n);
    }
    return r==0;
}
