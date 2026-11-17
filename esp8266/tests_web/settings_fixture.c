#include "settings.h"
static int (*rd)(unsigned,void *,size_t);
static int (*er)(unsigned);
static int (*wr)(unsigned,size_t,const void *,size_t);
void ms_test_settings_hooks(int (*r)(unsigned,void *,size_t),int (*e)(unsigned),
                            int (*w)(unsigned,size_t,const void *,size_t))
{ rd=r; er=e; wr=w; }
int ms_settings_read(unsigned s,void *p,size_t n) { return rd(s,p,n); }
int ms_settings_erase(unsigned s) { return er(s); }
int ms_settings_write(unsigned s,size_t o,const void *p,size_t n) { return wr(s,o,p,n); }
