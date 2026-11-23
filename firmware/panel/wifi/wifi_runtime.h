#ifndef WIFI_RUNTIME_H
#define WIFI_RUNTIME_H
#include <stdint.h>
enum { WIFI_IDLE,WIFI_WAIT_DHCP,WIFI_GOT_IP,WIFI_AP_RETAINED,WIFI_FAILED };
enum { WIFI_OK,WIFI_INVALID,WIFI_BUSY,WIFI_AP_NOT_READY,WIFI_DRIVER,
       WIFI_DHCP_TIMEOUT,WIFI_SUBNET_CONFLICT,WIFI_LINK_LOST };
typedef struct {
    char ssid[33],password[33];
    uint32_t key_mgmt; /* Exact target: NONE=4, WPA-PSK=2. */
} WifiConfig;
typedef struct {
    void *(*device)(uint32_t id);
    int (*has_netif)(void *device);
    uint32_t (*ip)(void *device);
    uint32_t (*mask)(void *device);
    int (*add)(void *device,const char *name);
    int (*dhcp)(void *device,int enable);
    int (*clear_ip)(void *device);
    int (*stop)(uint32_t interface_id);
    int (*start)(uint32_t interface_id);
    int (*flush)(uint32_t interface_id);
    int (*configure_sta)(const WifiConfig *config);
    int (*running)(uint32_t interface_id);
} WifiOps;
typedef struct {
    uint32_t phase,reason,started;
    volatile uint32_t ipv4; /* one aligned UI snapshot; zero hides stale IP */
    uint32_t ap_ip,ap_mask,owns_sta_netif;
    /* Lifetime latch: late STA events must not write settings after rollback.
     * Set BEFORE changing the driver, clear only by reboot (new zeroed state). */
    volatile uint32_t inhibit_syscfg_save;
    void *sta,*ap;
} WifiRuntime;
void wifi_runtime_init(WifiRuntime *r);
int wifi_config_valid(const WifiConfig *c);
int wifi_runtime_begin(WifiRuntime *r,const WifiOps *o,const WifiConfig *c,uint32_t now);
void wifi_runtime_poll(WifiRuntime *r,const WifiOps *o,uint32_t now,int sta_associated);
void wifi_runtime_stop(WifiRuntime *r,const WifiOps *o,uint32_t reason);
/* Raw target lwIP IPv4 word (octet0 in low byte); never prints Wi-Fi secrets. */
int wifi_ipv4_text(uint32_t address,char out[16]);
/* Pure UI model: actual DHCP IP only, never a default/AP address as station IP. */
void wifi_ip_screen(const WifiRuntime *r,char address[16],uint32_t *visible);
#endif
