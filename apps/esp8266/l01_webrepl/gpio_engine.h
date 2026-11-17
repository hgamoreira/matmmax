/* Included only in generated L01 engine, after its existing BuiltinFn helpers.
 * Shared engine sources stay unchanged. No I/O before validating arguments and
 * reserving the numeric return value. */
#ifndef MS_GPIO_ENGINE_H
#define MS_GPIO_ENGINE_H
#include "pins.h"
static int gpio_text(Parser *P,const Mat *m,char *out,unsigned capacity)
{
    long i,n=MAT_N(m);
    if (m->kind!=MAT_CHAR || m->r!=1 || n<1 || n>=(long)capacity) goto bad;
    for (i=0;i<n;++i) {
        double value=m->d[i];
        if (!isfinite(value) || value<32 || value>126 || floor(value)!=value) goto bad;
        out[i]=(char)value;
    }
    out[n]=0; return 1;
bad:
    set_err(P->s,"pin labels/modes must be short printable strings"); return 0;
}
static int gpio_integer(Parser *P,const Mat *m,const char *name,int maximum,int *out)
{
    double value;
    if (m->kind==MAT_CHAR || m->r!=1 || m->c!=1) goto bad;
    value=m->d[0];
    if (!isfinite(value) || value<0 || value>maximum || floor(value)!=value) goto bad;
    *out=(int)value; return 1;
bad:
    set_err(P->s,"%s needs an integer from 0 to %d",name,maximum); return 0;
}
static Mat *gpio_result(Parser *P,Mat *result,int status)
{
    if (status==MS_PIN_OK) return result;
    mat_free(result); set_err(P->s,"%s",ms_pin_error(status)); return NULL;
}
static Mat *b_pinmode(Parser *P,Mat **a,int n)
{
    char label[4],mode[8]; int pin,kind=-1,initial=0; Mat *result;
    if (!gpio_text(P,a[0],label,sizeof(label)) || !gpio_text(P,a[1],mode,sizeof(mode))) return NULL;
    pin=ms_pin_index(label);
    if (!strcmp(mode,"input")) kind=0;
    else if (!strcmp(mode,"output")) kind=1;
    else if (!strcmp(mode,"pullup")) kind=2;
    if (n==3 && kind!=1) { set_err(P->s,"initial level is only valid for output mode"); return NULL; }
    if (n==3 && !gpio_integer(P,a[2],"initial level",1,&initial)) return NULL;
    result=need(P,mat_scalar(0)); if (!result) return NULL;
    return gpio_result(P,result,ms_pin_configure(pin,kind,initial));
}
static Mat *b_digitalwrite(Parser *P,Mat **a,int n)
{
    char label[4]; int value; Mat *result; (void)n;
    if (!gpio_text(P,a[0],label,sizeof(label)) || !gpio_integer(P,a[1],"digital level",1,&value)) return NULL;
    result=need(P,mat_scalar(0)); if (!result) return NULL;
    return gpio_result(P,result,ms_pin_write(ms_pin_index(label),value));
}
static Mat *b_digitalread(Parser *P,Mat **a,int n)
{
    char label[4]; int value=0,status; Mat *result; (void)n;
    if (!gpio_text(P,a[0],label,sizeof(label))) return NULL;
    result=need(P,mat_scalar(0)); if (!result) return NULL;
    status=ms_pin_read(ms_pin_index(label),&value); result->d[0]=(double)value;
    return gpio_result(P,result,status);
}
static Mat *b_analogread(Parser *P,Mat **a,int n)
{
    char label[4]="A0"; int value=0,status; Mat *result;
    if (n && !gpio_text(P,a[0],label,sizeof(label))) return NULL;
    result=need(P,mat_scalar(0)); if (!result) return NULL;
    status=ms_pin_analog(label,&value); result->d[0]=(double)value;
    return gpio_result(P,result,status);
}
static Mat *b_delay(Parser *P,Mat **a,int n)
{
    int milliseconds; Mat *result; (void)n;
    if (!gpio_integer(P,a[0],"delay milliseconds",60000,&milliseconds)) return NULL;
    result=need(P,mat_scalar(0)); if (!result) return NULL;
    return gpio_result(P,result,ms_pin_delay((unsigned long)milliseconds));
}
#endif
