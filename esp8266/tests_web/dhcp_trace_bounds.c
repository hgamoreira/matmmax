#include <stdio.h>
#include <string.h>
#include "dhcp_trace.h"
int main(int argc,char **argv)
{
    unsigned char packet[2048],copy[2048],mac[6]={0x34,0x5f,0x45,0x40,0x57,0x3a};
    MsDhcpPacket out; size_t n,i,j; FILE *file;
    if (argc!=2 || !(file=fopen(argv[1],"rb"))) return 2;
    n=fread(packet,1,sizeof(packet),file); fclose(file);
    for (i=0;i<=n;++i) ms_dhcp_packet_read(packet,i,mac,&out);
    for (i=0;i<n;++i) for (j=0;j<256;j+=17) {
        memcpy(copy,packet,n); copy[i]=(unsigned char)j;
        ms_dhcp_packet_read(copy,n,mac,&out);
        ms_dhcp_packet_read(copy,i,mac,&out);
    }
    return 0;
}
