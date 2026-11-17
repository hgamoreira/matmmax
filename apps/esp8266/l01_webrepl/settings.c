/* Two independently erased records. Commit the new generation last, preserving
 * the previous record during interrupted erase/program. No SDK credential writes. */
#include <string.h>
#include "settings.h"
typedef union { uint32_t align; unsigned char b[MS_SETTINGS_SIZE]; } Record;
typedef struct { MsSettings value; int valid, deleted; } Decoded;
static uint32_t get32(const unsigned char *p)
{ return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24); }
static void put32(unsigned char *p,uint32_t n)
{ unsigned i; for (i=0;i<4;++i) p[i]=(unsigned char)(n>>(8*i)); }
static uint32_t checksum(const unsigned char *p,size_t size)
{
    uint32_t c=0xffffffffU; unsigned k;
    while (size--) { c^=*p++; for (k=0;k<8;++k) c=(c>>1)^((c&1) ? 0xedb88320U : 0); }
    return c^0xffffffffU;
}
static int text_ok(const char *s,size_t n)
{
    size_t i;
    for (i=0;i<n;++i) if ((unsigned char)s[i]<32 || (unsigned char)s[i]>126) return 0;
    return s[n]==0;
}
static int value_ok(const MsSettings *s)
{
    size_t ns=0,np=0,i;
    while (ns<33 && s->ssid[ns]) ++ns;
    while (np<64 && s->password[np]) ++np;
    if (!ns || ns>32 || np>63 || !text_ok(s->ssid,ns) || !text_ok(s->password,np)) return 0;
    for (i=0;i<32;++i) if (!((s->key[i]>='0' && s->key[i]<='9') || (s->key[i]>='a' && s->key[i]<='f'))) return 0;
    return !s->key[32];
}
static Decoded decode(const Record *r)
{
    Decoded d; memset(&d,0,sizeof(d));
    if (memcmp(r->b,"MSF5",4) || r->b[4]!=1 || r->b[7]>1 ||
        get32(r->b+156)!=0x4d534f4bU || get32(r->b+152)!=checksum(r->b,152)) return d;
    d.value.generation=get32(r->b+8); d.deleted=r->b[7];
    if (d.deleted) { if (r->b[5] || r->b[6]) return d; d.valid=1; return d; }
    if (!r->b[5] || r->b[5]>32 || r->b[6]>63) return d;
    memcpy(d.value.ssid,r->b+12,33); memcpy(d.value.password,r->b+45,64); memcpy(d.value.key,r->b+109,33);
    if (!value_ok(&d.value) || strlen(d.value.ssid)!=r->b[5] || strlen(d.value.password)!=r->b[6]) {
        memset(&d.value,0,sizeof(d.value)); return d;
    }
    d.valid=1; return d;
}
static int latest(Decoded d[2],int *which)
{
    Record r; unsigned i;
    for (i=0;i<2;++i) {
        if (!ms_settings_read(i,r.b,sizeof(r.b))) { memset(d,0,2*sizeof(*d)); return MS_STORE_IO; }
        d[i]=decode(&r);
    }
    memset(&r,0,sizeof(r));
    if (!d[0].valid && !d[1].valid) { *which=-1; return MS_STORE_EMPTY; }
    *which=d[0].valid ? 0 : 1;
    if (d[0].valid && d[1].valid) {
        uint32_t difference=d[1].value.generation-d[0].value.generation;
        if (difference && difference<0x80000000U) *which=1;
    }
    return MS_STORE_OK;
}
int ms_settings_load(MsSettings *s)
{
    Decoded d[2]; int which,result;
    memset(s,0,sizeof(*s)); result=latest(d,&which);
    if (result==MS_STORE_OK && !d[which].deleted) *s=d[which].value;
    else if (result==MS_STORE_OK) result=MS_STORE_EMPTY;
    memset(d,0,sizeof(d)); return result;
}
static int commit(unsigned slot,const MsSettings *s,uint32_t generation,int deleted)
{
    Record r,verify; Decoded decoded; uint32_t mark=0x4d534f4bU;
    memset(&r,0,sizeof(r)); memcpy(r.b,"MSF5",4); r.b[4]=1; r.b[7]=(unsigned char)deleted;
    put32(r.b+8,generation);
    if (!deleted) {
        r.b[5]=(unsigned char)strlen(s->ssid); r.b[6]=(unsigned char)strlen(s->password);
        memcpy(r.b+12,s->ssid,r.b[5]+1); memcpy(r.b+45,s->password,r.b[6]+1); memcpy(r.b+109,s->key,33);
    }
    put32(r.b+152,checksum(r.b,152));
    put32(r.b+156,mark);
    if (!ms_settings_erase(slot) || !ms_settings_write(slot,0,r.b,156) ||
        !ms_settings_write(slot,156,r.b+156,4) || !ms_settings_read(slot,verify.b,sizeof(verify.b))) {
        memset(&r,0,sizeof(r)); return MS_STORE_IO;
    }
    decoded=decode(&verify);
    int result=decoded.valid && !memcmp(r.b,verify.b,sizeof(r.b)) ? MS_STORE_OK : MS_STORE_IO;
    memset(&decoded,0,sizeof(decoded)); memset(&r,0,sizeof(r)); memset(&verify,0,sizeof(verify));
    return result;
}
int ms_settings_save(const MsSettings *s)
{
    Decoded d[2]; int which,result;
    if (!value_ok(s)) return MS_STORE_INVALID;
    result=latest(d,&which); if (result==MS_STORE_IO) return result;
    if (which>=0 && !d[which].deleted && !strcmp(s->ssid,d[which].value.ssid) &&
        !strcmp(s->password,d[which].value.password) && !strcmp(s->key,d[which].value.key)) result=MS_STORE_UNCHANGED;
    else result=commit(which<0 ? 0 : 1-which,s,which<0 ? 1 : d[which].value.generation+1,0);
    memset(d,0,sizeof(d)); return result;
}
int ms_settings_forget(void)
{
    Decoded d[2]; int which,result=latest(d,&which);
    if (result==MS_STORE_IO) return result;
    if (which<0) {
        result=ms_settings_erase(0) && ms_settings_erase(1) ? MS_STORE_OK : MS_STORE_IO;
    } else if (d[which].deleted) result=ms_settings_erase(1-which) ? MS_STORE_OK : MS_STORE_CLEANUP;
    else {
        result=commit(1-which,NULL,d[which].value.generation+1,1);
        if (result==MS_STORE_OK && !ms_settings_erase(which)) result=MS_STORE_CLEANUP;
    }
    memset(d,0,sizeof(d)); return result;
}
