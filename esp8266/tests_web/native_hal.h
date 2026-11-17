/* POSIX adapter for exercising the actual firmware transport, not a chip emulator. */
#ifndef MS_NATIVE_HAL_H
#define MS_NATIVE_HAL_H
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <cstdint>
#include <string>
#include <memory>
#include <functional>
#include <unistd.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <chrono>
#include <cerrno>
#include <cstdarg>
#define PROGMEM
#define memcpy_P memcpy
#define INPUT 0
#define OUTPUT 1
#define INPUT_PULLUP 2
#define A0 17
#define WIFI_OFF 0
#define WIFI_STA 1
#define WL_CONNECTED 3
inline unsigned long native_clock_offset_us=0;
static inline unsigned long micros() {
    using namespace std::chrono;
    return (unsigned long)duration_cast<microseconds>(steady_clock::now().time_since_epoch()).count()+native_clock_offset_us;
}
static inline unsigned long millis() { return micros()/1000; }
static inline void optimistic_yield(unsigned) {}
static inline void delay(unsigned) {}
static inline void disable_extra4k_at_link_time() {}
static inline unsigned os_random() {
    unsigned n; FILE *f=fopen("/dev/urandom","rb");
    if (!f || fread(&n,sizeof(n),1,f)!=1) exit(2);
    fclose(f); return n;
}
class NativeSerial {
    int pending=-1;
public:
    bool eof=false;
    void begin(int) { setvbuf(stdout,nullptr,_IONBF,0); fcntl(0,F_SETFL,O_NONBLOCK); }
    void setDebugOutput(bool) {}
    void print(const char *s) { fputs(s,stdout); }
    void println(const char *s) { puts(s); }
    void printf(const char *format,...) { va_list ap; va_start(ap,format); vprintf(format,ap); va_end(ap); }
    void flush() { fflush(stdout); }
    int available() {
        if (pending>=0) return 1;
        unsigned char c; int n=::read(0,&c,1);
        if (n==1) pending=c; else if (!n) eof=true;
        return pending>=0;
    }
    int read() { if (!available()) return -1; int c=pending; pending=-1; return c; }
};
inline NativeSerial Serial;
class NativeESP { public: void restart() { exit(0); } };
inline NativeESP ESP;
struct NativeIP {
    bool present=false;
    bool link_local=false;
    unsigned char operator[](unsigned i) const {
        const unsigned char normal[4]={127,0,0,1},local[4]={169,254,10,20};
        return present && i<4 ? (link_local ? local[i] : normal[i]) : 0;
    }
    std::string toString() { return present ? (link_local ? "169.254.10.20" : "127.0.0.1") : "0.0.0.0"; }
    explicit operator uint32_t() const {
        return (uint32_t)(*this)[0]|((uint32_t)(*this)[1]<<8)|((uint32_t)(*this)[2]<<16)|((uint32_t)(*this)[3]<<24);
    }
};
struct WiFiEventStationModeDisconnected { unsigned reason; };
struct WiFiEventStationModeConnected {
    std::string ssid;
    unsigned char bssid[6]={2,0,0,0,0,1};
    unsigned char channel=6;
};
struct WiFiEventStationModeGotIP {};
using WiFiEventHandler=std::shared_ptr<void>;
class NativeWiFi {
    int opmode=0, sdk_state=0, dhcp_state=0;
    std::weak_ptr<std::function<void(const WiFiEventStationModeDisconnected&)>> disconnected;
    std::weak_ptr<std::function<void(const WiFiEventStationModeConnected&)>> associated;
    std::weak_ptr<std::function<void(const WiFiEventStationModeGotIP&)>> got_ip;
    std::weak_ptr<std::function<void()>> dhcp_timeout;
public:
    unsigned lwip_state=0,lwip_pcb=0,lwip_tries=0;
    int startLwipDhcp() {
        const char *failure=getenv("MS_TEST_LWIP_START_ERROR");
        int error=failure ? atoi(failure) : 0;
        if (error) return error;
        lwip_state=6; lwip_pcb=1; lwip_tries=1;
        if (getenv("MS_TEST_LWIP_START_GET_IP")) {
            sdk_state=5; lwip_state=10;
            if (auto callback=got_ip.lock()) (*callback)({});
        }
        return 0;
    }
    void persistent(bool) {}
    bool mode(int mode) {
        if (mode && getenv("MS_TEST_WIFI_MODE_FAIL")) return false;
        opmode=mode; if (!mode) { sdk_state=0; dhcp_state=0; lwip_state=lwip_pcb=lwip_tries=0; } return true;
    }
    int getMode() { return opmode; }
    bool setAutoReconnect(bool) { return true; }
    int dhcpStatus() { return dhcp_state; }
    bool stopDhcp() { dhcp_state=0; return true; }
    bool config(unsigned ip,unsigned gateway,unsigned mask) {
        if (!opmode || ip || gateway || mask || getenv("MS_TEST_DHCP_CONFIG_FAIL")) return false;
        dhcp_state=1;
        if (getenv("MS_TEST_DHCP_RECOVER_IP")) {
            sdk_state=5;
            if (auto callback=got_ip.lock()) (*callback)({});
        }
        return true;
    }
    bool disconnect(bool,bool) {
        if (sdk_state==5) {
            if (auto callback=disconnected.lock()) (*callback)({8});
        }
        sdk_state=0; return true;
    }
    int begin(const char *ssid,const char *password) {
        if (sdk_state==5 && getenv("MS_TEST_WIFI_REPEAT_BEGIN_NO_EVENTS")) return status();
        const char *expected=getenv("MS_TEST_EXPECT_SSID");
        if (expected && strcmp(expected,ssid)) { fputs("Native SSID mismatch\n",stderr); exit(2); }
        expected=getenv("MS_TEST_EXPECT_PASSWORD");
        if (expected && strcmp(expected,password)) { fputs("Native password mismatch\n",stderr); exit(2); }
        const char *state=getenv("MS_TEST_WIFI_SDK_STATE");
        sdk_state=state ? atoi(state) : 5;
        const char *dhcp=getenv("MS_TEST_DHCP_STATE");
        dhcp_state=dhcp ? atoi(dhcp) : 1;
        const char *stage=getenv("MS_TEST_DIAG_STATE"),*pcb=getenv("MS_TEST_DIAG_PCB"),*tries=getenv("MS_TEST_DIAG_TRIES");
        lwip_state=stage ? strtoul(stage,nullptr,0) : sdk_state==5 ? 10 : 6;
        lwip_pcb=pcb ? strtoul(pcb,nullptr,0) : lwip_state ? 1 : 0;
        lwip_tries=tries ? strtoul(tries,nullptr,0) : lwip_state ? 3 : 0;
        if (const char *elapsed=getenv("MS_TEST_WIFI_ELAPSED_MS")) native_clock_offset_us+=strtoul(elapsed,nullptr,10)*1000;
        if (sdk_state==5 || getenv("MS_TEST_WIFI_ASSOCIATED") || getenv("MS_TEST_WIFI_HAS_IP")) {
            const char *observed=getenv("MS_TEST_WIFI_ASSOCIATED_SSID");
            if (auto callback=associated.lock()) (*callback)({observed ? observed : ssid});
            if (const char *age=getenv("MS_TEST_ASSOCIATED_AGE_MS")) native_clock_offset_us+=strtoul(age,nullptr,10)*1000;
        }
        if ((sdk_state==5 || getenv("MS_TEST_WIFI_HAS_IP")) && !getenv("MS_TEST_WIFI_SKIP_IP_EVENT")) {
            if (auto callback=got_ip.lock()) (*callback)({});
        }
        if (getenv("MS_TEST_WIFI_DHCP_TIMEOUT")) {
            if (auto callback=dhcp_timeout.lock()) (*callback)();
        }
        const char *reason=getenv("MS_TEST_WIFI_REASON");
        if (reason) { if (auto callback=disconnected.lock()) (*callback)({(unsigned)atoi(reason)}); }
        return status();
    }
    int status() { static const int states[]={0,7,6,1,4,WL_CONNECTED}; return sdk_state>=0 && sdk_state<6 ? states[sdk_state] : 7; }
    int sdkStatus() { return sdk_state; }
    WiFiEventHandler onStationModeDisconnected(std::function<void(const WiFiEventStationModeDisconnected&)> f) {
        auto handler=std::make_shared<std::function<void(const WiFiEventStationModeDisconnected&)>>(f);
        disconnected=handler; return handler;
    }
    WiFiEventHandler onStationModeConnected(std::function<void(const WiFiEventStationModeConnected&)> f) {
        auto handler=std::make_shared<std::function<void(const WiFiEventStationModeConnected&)>>(f);
        associated=handler; return handler;
    }
    WiFiEventHandler onStationModeGotIP(std::function<void(const WiFiEventStationModeGotIP&)> f) {
        auto handler=std::make_shared<std::function<void(const WiFiEventStationModeGotIP&)>>(f);
        got_ip=handler; return handler;
    }
    WiFiEventHandler onStationModeDHCPTimeout(std::function<void()> f) {
        auto handler=std::make_shared<std::function<void()>>(f);
        dhcp_timeout=handler; return handler;
    }
    NativeIP localIP() { return NativeIP{opmode!=0 && (sdk_state==5 || getenv("MS_TEST_WIFI_HAS_IP")),getenv("MS_TEST_WIFI_LINK_LOCAL")!=nullptr}; }
};
inline NativeWiFi WiFi;
class NativeMDNS {
public:
    bool begin(const char *) { return !getenv("MS_TEST_MDNS_FAIL"); }
    void addService(const char *,const char *,unsigned) {}
    void update() {}
    void close() {}
};
inline NativeMDNS MDNS;
static inline int wifi_station_get_connect_status() { return WiFi.sdkStatus(); }
static inline int wifi_station_dhcpc_status() { return WiFi.dhcpStatus(); }
static inline bool wifi_station_dhcpc_stop() { return WiFi.stopDhcp(); }
struct NativeSocket {
    int fd;
    explicit NativeSocket(int f):fd(f) {}
    ~NativeSocket() { if (fd>=0) close(fd); }
};
class WiFiClient {
    std::shared_ptr<NativeSocket> sock;
    int fd() const { return sock ? sock->fd : -1; }
public:
    WiFiClient()=default;
    explicit WiFiClient(int f):sock(std::make_shared<NativeSocket>(f)) { fcntl(f,F_SETFL,O_NONBLOCK); }
    explicit operator bool() const { return fd()>=0; }
    bool connected() {
        if (fd()<0) return false;
        char c; int n=recv(fd(),&c,1,MSG_PEEK|MSG_DONTWAIT);
        return n>0 || (n<0 && (errno==EAGAIN || errno==EWOULDBLOCK));
    }
    int available() { int n=0; if (fd()>=0) ioctl(fd(),FIONREAD,&n); return n; }
    int read() { unsigned char c; return recv(fd(),&c,1,MSG_DONTWAIT)==1 ? c : -1; }
    int availableForWrite() {
        static unsigned long start=millis();
        const char *after=getenv("MS_TEST_SOCKET_STALL_AFTER");
        if (after && millis()-start>(unsigned)atoi(after)) return 0;
        if (getenv("MS_TEST_SOCKET_STALL") && millis()%2000>1000) return 0;
        return connected() ? 4096 : 0;
    }
    size_t write(const unsigned char *s,size_t n) {
        int r=send(fd(),s,n,MSG_NOSIGNAL|MSG_DONTWAIT); return r<0 ? 0 : (size_t)r;
    }
    void stop() { if (fd()>=0) { close(fd()); sock->fd=-1; } }
    void abort() { stop(); }
    void setNoDelay(bool) {}
    void setTimeout(int) {}
};
class WiFiServer {
    int fd=-1,waiting=-1;
    unsigned assigned=0;
public:
    explicit WiFiServer(unsigned) {}
    void begin(unsigned,unsigned) {
        fd=socket(AF_INET,SOCK_STREAM,0); if (fd<0) { perror("native socket"); exit(2); }
        int yes=1; setsockopt(fd,SOL_SOCKET,SO_REUSEADDR,&yes,sizeof(yes));
        sockaddr_in a{}; a.sin_family=AF_INET; a.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
        const char *p=getenv("MS_NATIVE_PORT"); a.sin_port=htons(p ? atoi(p) : 0);
        if (bind(fd,(sockaddr *)&a,sizeof(a)) || listen(fd,1)) { perror("native bind/listen"); exit(2); }
        socklen_t n=sizeof(a); getsockname(fd,(sockaddr *)&a,&n); assigned=ntohs(a.sin_port);
        fcntl(fd,F_SETFL,O_NONBLOCK);
    }
    bool hasClient() {
        if (waiting<0 && fd>=0) waiting=::accept(fd,nullptr,nullptr);
        return waiting>=0;
    }
    WiFiClient accept() { if (!hasClient()) return WiFiClient(); int f=waiting; waiting=-1; return WiFiClient(f); }
    void stop() { if (fd>=0) close(fd); if (waiting>=0) close(waiting); fd=waiting=-1; }
    void setNoDelay(bool) {}
    unsigned port() { return assigned; }
};
inline int native_pins[18]={0};
static inline void pinMode(int p,int mode) { if (mode==INPUT_PULLUP) native_pins[p]=1; }
static inline void digitalWrite(int p,int value) {
    native_pins[p]=value;
    if (getenv("MS_TEST_GPIO_TRACE")) fprintf(stderr,"Native GPIO write GPIO%d=%d\n",p,value);
}
static inline int digitalRead(int p) { return native_pins[p]; }
static inline int analogRead(int) { return 321; }
#endif
