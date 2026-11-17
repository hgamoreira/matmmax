/* Bounded HTTP upgrade and small, unfragmented RFC6455 messages. */
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include "web.h"

int ms_frame_byte(MsFrame *p, unsigned char c)
{
    if (!p->stage) {
        if ((c&0x70) || !(c&0x80) || ((c&15)!=1 && (c&15)!=8 && (c&15)!=9 && (c&15)!=10)) return -1;
        p->opcode=c&15; p->stage=1; p->count=0; return 0;
    }
    if (p->stage==1) {
        if (!(c&0x80)) return -1; /* Client frames must be masked. */
        p->length=c&127;
        if (p->length==127 || (p->opcode>=8 && p->length>125)) return -1;
        p->stage=p->length==126 ? 2 : 4;
        return 0;
    }
    if (p->stage==2) { if (c) return -1; p->stage=3; return 0; }
    if (p->stage==3) {
        if (c<126 || c>192) return -1;
        p->length=c; p->stage=4; return 0;
    }
    if (p->stage<8) {
        p->mask[p->stage++-4]=c;
        if (p->stage==8 && !p->length) { p->stage=0; p->data[0]=0; return 1; }
        return 0;
    }
    p->data[p->count]=(unsigned char)(c^p->mask[p->count%4]);
    if (++p->count==p->length) { p->data[p->count]=0; p->stage=0; return 1; }
    return 0;
}

static uint32_t rol(uint32_t x, unsigned n) { return (x<<n)|(x>>(32-n)); }
void ms_web_accept(const char key[25], char accept[29])
{
    /* A valid key is 24 bytes plus the fixed 36-byte WebSocket GUID. */
    unsigned char message[128]={0}, digest[20];
    uint32_t h[5]={0x67452301,0xefcdab89,0x98badcfe,0x10325476,0xc3d2e1f0};
    static const char b64[]="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    unsigned block,i,j=0;
    memcpy(message,key,24); memcpy(message+24,"258EAFA5-E914-47DA-95CA-C5AB0DC85B11",36);
    message[60]=0x80; message[126]=1; message[127]=0xe0; /* 60*8 bits. */
    for (block=0;block<2;++block) {
        uint32_t w[80],a=h[0],b=h[1],c=h[2],d=h[3],e=h[4];
        for (i=0;i<16;++i) { unsigned k=block*64+i*4; w[i]=((uint32_t)message[k]<<24)|((uint32_t)message[k+1]<<16)|((uint32_t)message[k+2]<<8)|message[k+3]; }
        for (i=16;i<80;++i) w[i]=rol(w[i-3]^w[i-8]^w[i-14]^w[i-16],1);
        for (i=0;i<80;++i) {
            uint32_t f,k,t;
            if (i<20) { f=(b&c)|(~b&d); k=0x5a827999; }
            else if (i<40) { f=b^c^d; k=0x6ed9eba1; }
            else if (i<60) { f=(b&c)|(b&d)|(c&d); k=0x8f1bbcdc; }
            else { f=b^c^d; k=0xca62c1d6; }
            t=rol(a,5)+f+e+k+w[i]; e=d; d=c; c=rol(b,30); b=a; a=t;
        }
        h[0]+=a; h[1]+=b; h[2]+=c; h[3]+=d; h[4]+=e;
    }
    for (i=0;i<20;++i) digest[i]=(unsigned char)(h[i/4]>>(24-8*(i%4)));
    for (i=0;i<20;i+=3) {
        unsigned n=20-i; uint32_t v=(uint32_t)digest[i]<<16;
        if (n>1) v|=(uint32_t)digest[i+1]<<8;
        if (n>2) v|=digest[i+2];
        accept[j++]=b64[(v>>18)&63]; accept[j++]=b64[(v>>12)&63];
        accept[j++]=n>1 ? b64[(v>>6)&63] : '='; accept[j++]=n>2 ? b64[v&63] : '=';
    }
    accept[j]=0;
}
static int equal(const char *a,const char *b)
{
    while (*a && *b) { if (tolower((unsigned char)*a++)!=tolower((unsigned char)*b++)) return 0; }
    return !*a && !*b;
}
static int contains_token(char *s, const char *target)
{
    while (*s) {
        char word[32]; unsigned n=0;
        while (*s==' ' || *s=='\t' || *s==',') ++s;
        while (*s && *s!=',') { if (n+1>=sizeof(word)) return 0; word[n++]=*s++; }
        while (n && (word[n-1]==' ' || word[n-1]=='\t')) --n;
        word[n]=0; if (equal(word,target)) return 1;
    }
    return 0;
}
int ms_http_request(char *request,const char *host,const char *token,char accept[29])
{
    char *line=strstr(request,"\r\n"), *p;
    char *h=NULL,*origin=NULL,*upgrade=NULL,*connection=NULL,*key=NULL,*version=NULL,*protocol=NULL;
    int websocket;
    char expected[96];
    if (!line) return 400;
    *line=0;
    websocket=!strcmp(request,"GET /ws HTTP/1.1");
    if (!websocket && strcmp(request,"GET / HTTP/1.1")) return 404;
    p=line+2;
    while (*p) {
        char *end=strstr(p,"\r\n"),*colon,*v,**slot=NULL;
        if (!end) return 400;
        if (end==p) { if (end[2]) return 400; break; }
        *end=0; colon=strchr(p,':'); if (!colon) return 400;
        *colon=0; v=colon+1; while (*v==' ' || *v=='\t') ++v;
        line=v+strlen(v); while (line>v && (line[-1]==' ' || line[-1]=='\t')) *--line=0;
        if (equal(p,"Host")) slot=&h;
        else if (equal(p,"Origin")) slot=&origin;
        else if (equal(p,"Upgrade")) slot=&upgrade;
        else if (equal(p,"Connection")) slot=&connection;
        else if (equal(p,"Sec-WebSocket-Key")) slot=&key;
        else if (equal(p,"Sec-WebSocket-Version")) slot=&version;
        else if (equal(p,"Sec-WebSocket-Protocol")) slot=&protocol;
        else if (equal(p,"Content-Length") || equal(p,"Transfer-Encoding")) return 400;
        if (slot) { if (*slot) return 400; *slot=v; }
        p=end+2;
    }
    char alias[64];
    snprintf(alias,sizeof(alias),"mathsmith.local%s",strchr(host,':') ? strchr(host,':') : "");
    if (!h || (strcmp(h,host) && strcmp(h,alias))) return 403; /* Fixed local alias or numeric address only. */
    if (!websocket) return 200;
    snprintf(expected,sizeof(expected),"http://%s",h);
    if (!origin || strcmp(origin,expected)) return 403;
    snprintf(expected,sizeof(expected),"mathsmith.%s",token);
    if (!protocol || strcmp(protocol,expected)) return 403;
    if (!upgrade || !equal(upgrade,"websocket") || !connection || !contains_token(connection,"upgrade") ||
        !version || strcmp(version,"13") || !key || strlen(key)!=24 || key[22]!='=' || key[23]!='=') return 400;
    for (p=key;p<key+22;++p) if (!isalnum((unsigned char)*p) && *p!='+' && *p!='/') return 400;
    /* Last base64 digit has only its first two bits populated for 16 bytes. */
    if (!strchr("AQgw",key[21])) return 400;
    ms_web_accept(key,accept);
    return 101;
}
