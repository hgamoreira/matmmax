#include <cstdio>
#include "web.h"
#include "dhcp_trace.h"
extern "C" void ms_write(const char *text) { std::fputs(text,stdout); }
int main(int argc,char **argv)
{
    unsigned char data[2048];
    FILE *input;
    if (argc!=2 || !(input=std::fopen(argv[1],"rb"))) return 2;
    size_t n=std::fread(data,1,sizeof(data),input); std::fclose(input);
    ms_dhcp_trace_start();
    ms_dhcp_trace_observe(0,(const char *)data,n,0,1);
    ms_dhcp_trace_diagnostics();
    return 0;
}
