/* D1 mini labels only: UART and flash pins are intentionally unreachable. */
#include <stdio.h>
#include <string.h>
#include "web.h"
#include "pins.h"
int ms_platform_command(const char *line)
{
    char cmd[16], name[16], mode[16], value[16], extra[2], out[120];
    int n, d, level, status;
    if (line[0]!=':') return 0;
    if (!strncmp(line,":wifi",5) || !strncmp(line,":web",4)) return ms_wifi_command(line);
    if (!strcmp(line,":pins")) {
        ms_write("D0=GPIO16 D1=GPIO5 D2=GPIO4 D3=GPIO0 D4=GPIO2\n"
                 "D5=GPIO14 D6=GPIO12 D7=GPIO13 D8=GPIO15; A0=ADC\n"
                 "3.3 V digital only. D3/D4/D8 affect boot; D4 LED is active low.\n"
                 "UART GPIO1/3 and flash GPIO6..11 reserved. D0 has no pullup.\n"
                 "Commands: :pin D1 output 0; :write D1 1; :read D1; :analog A0\n");
        return 1;
    }
    n=sscanf(line,"%15s %15s %15s %15s %1s",cmd,name,mode,value,extra);
    if (strcmp(cmd,":pin") && strcmp(cmd,":read") && strcmp(cmd,":write") && strcmp(cmd,":analog")) return 0;
    if (!strcmp(cmd,":analog")) {
        if (n!=2 || strcmp(name,"A0")) goto syntax;
        ms_pin_analog(name,&level);
        snprintf(out,sizeof(out),"A0 = %d (raw ADC 0..1023)\n",level); ms_write(out); return 1;
    }
    if (n<2 || (d=ms_pin_index(name))<0) {
        ms_write("error: use D0..D8; UART/flash pins are reserved\n"); return 1;
    }
    if (!strcmp(cmd,":pin")) {
        if (n==3 && !strcmp(mode,"input")) { status=ms_pin_configure(d,0,0); }
        else if (n==3 && !strcmp(mode,"pullup") && d!=0) { status=ms_pin_configure(d,2,0); }
        else if ((n==3 || n==4) && !strcmp(mode,"output")) {
            if (n==4 && strcmp(value,"0") && strcmp(value,"1")) goto syntax;
            level=n==4 ? value[0]-'0' : 0;
            status=ms_pin_configure(d,1,level);
        } else goto syntax;
        if (status!=MS_PIN_OK) goto failed;
        snprintf(out,sizeof(out),"%s GPIO%u: %s\n",name,(unsigned)ms_pin_gpio(d),mode); ms_write(out); return 1;
    }
    if (!strcmp(cmd,":write")) {
        if (n!=3 || (strcmp(mode,"0") && strcmp(mode,"1"))) goto syntax;
        status=ms_pin_write(d,mode[0]-'0');
        if (status!=MS_PIN_OK) goto failed;
    } else if (n!=2) goto syntax;
    status=ms_pin_read(d,&level); if (status!=MS_PIN_OK) goto failed;
    snprintf(out,sizeof(out),"%s = %d\n",name,level); ms_write(out); return 1;
failed:
    ms_write("error: "); ms_write(ms_pin_error(status)); ms_write("\n"); return 1;
syntax:
    ms_write("error: :pin Dn input|pullup|output [0|1]; :read Dn; :write Dn 0|1; :analog A0\n");
    return 1;
}
