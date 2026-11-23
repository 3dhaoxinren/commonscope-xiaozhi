#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "wifi_runtime.h"

typedef struct { unsigned has, run; uint32_t ip, mask; } Netif;
static Netif ap, sta;
static unsigned ap_starts, ap_stops, driver_fault;
static void *dev(uint32_t id) { return id == 85 ? &sta : id == 86 ? &ap : 0; }
static int has(void *n) { return ((Netif *)n)->has; }
static uint32_t ip(void *n) { return ((Netif *)n)->ip; }
static uint32_t mask(void *n) { return ((Netif *)n)->mask; }
static int add(void *n, const char *name) {
    assert(n == &sta && strcmp(name, "w1") == 0);
    sta.has = 1;
    return 0;
}
static int dhcp(void *n, int enabled) { assert(n == &sta); return enabled && driver_fault; }
static int clear(void *n) { assert(n == &sta); sta.ip = sta.mask = 0; return 0; }
static int stop(uint32_t id) {
    if (id == 2) { ++ap_stops; ap.run = 0; }
    else { assert(id == 1); sta.run = 0; }
    return 0;
}
static int start(uint32_t id) {
    if (id == 2) { ++ap_starts; ap.run = 1; }
    else { assert(id == 1); sta.run = 1; }
    return 0;
}
static int flush(uint32_t id) { assert(id == 1 || id == 2); return 0; }
static int configure(const WifiConfig *c) { assert(wifi_config_valid(c)); return 0; }
static int running(uint32_t id) { return id == 1 ? sta.run : ap.run; }
static const WifiOps ops = {dev, has, ip, mask, add, dhcp, clear, stop, start, flush, configure, running};
static void reset(WifiRuntime *r) {
    ap = (Netif){1, 1, 0x0101a8c0u, 0x00ffffffu};
    sta = (Netif){0};
    ap_starts = ap_stops = driver_fault = 0;
    *r = (WifiRuntime){0};
    wifi_runtime_init(r);
}
int main(void) {
    WifiRuntime r;
    const WifiConfig c = {"host-fixture", "", 4};
    char address[16];
    uint32_t shown;
    // Startup with no AP is rejected without tearing down/reconfiguring interfaces.
    reset(&r); ap.run = 0;
    assert(!wifi_runtime_begin(&r,&ops,&c,0));
    assert(r.reason == WIFI_AP_NOT_READY && !ap_starts && !ap_stops && !sta.run);
    // Normal AP stays up while station gets a valid separate subnet.
    reset(&r); assert(wifi_runtime_begin(&r,&ops,&c,0));
    assert(ap.run && sta.run && !ap_stops && r.phase == WIFI_WAIT_DHCP);
    wifi_ip_screen(&r,address,&shown); assert(!shown && !address[0]);
    sta.ip=0x2a00000au; sta.mask=0x00ffffffu;
    wifi_runtime_poll(&r,&ops,100,1);
    wifi_ip_screen(&r,address,&shown);assert(shown && !strcmp(address,"10.0.0.42"));
    // Lost link reopens AP when needed and discards the old station IP.
    ap.run=0; wifi_runtime_poll(&r,&ops,101,0);
    assert(r.phase==WIFI_AP_RETAINED && r.reason==WIFI_LINK_LOST &&
           r.inhibit_syscfg_save && !sta.run && !r.ipv4 && ap.run && ap_starts==1 && !ap_stops);
    // Overlapping subnet, bad DHCP, and timeout all keep the built-in AP.
    reset(&r); assert(wifi_runtime_begin(&r,&ops,&c,10));
    sta.ip=0x6401a8c0u; sta.mask=0x00ffffffu; wifi_runtime_poll(&r,&ops,11,1);
    assert(r.phase==WIFI_AP_RETAINED && r.reason==WIFI_SUBNET_CONFLICT && ap.run && !ap_stops);
    reset(&r); driver_fault=1; assert(!wifi_runtime_begin(&r,&ops,&c,5));
    assert(r.phase==WIFI_AP_RETAINED && r.reason==WIFI_DRIVER && ap.run && !ap_stops);
    reset(&r); assert(wifi_runtime_begin(&r,&ops,&c,0xfffffff0u));
    wifi_runtime_poll(&r,&ops,0xfffffff0u+45000u,0);
    assert(r.phase==WIFI_AP_RETAINED && r.reason==WIFI_DHCP_TIMEOUT && ap.run && !ap_stops);
    puts("PASS 6 AP/STA host scenarios (no device I/O)");
}
