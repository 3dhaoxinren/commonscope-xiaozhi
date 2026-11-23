#include "wifi_runtime.h"
static uint32_t bounded(const char *s,uint32_t max){
    for(uint32_t i=0;i<=max;i++)if(!s[i])return i;
    return max+1;
}
static int usable(uint32_t ip){
    uint32_t first=ip&255u;
    return ip && ip!=0xffffffffu && first && first!=127u && first<224u;
}
static int valid_mask(uint32_t mask){
    uint32_t big=((mask&255u)<<24)|((mask&0xff00u)<<8)|((mask>>8)&0xff00u)|(mask>>24);
    uint32_t inv=~big;return big && (inv&(inv+1u))==0;
}
void wifi_runtime_init(WifiRuntime *r){
    r->phase=WIFI_IDLE;r->reason=WIFI_OK;r->started=0;r->ipv4=0;
    r->ap_ip=0;r->ap_mask=0;r->owns_sta_netif=0;
    r->inhibit_syscfg_save=0;r->sta=0;r->ap=0;
}
int wifi_config_valid(const WifiConfig *c){
    if(!c)return 0;
    uint32_t ssid=bounded(c->ssid,32),pw=bounded(c->password,32);
    if(!ssid || ssid>32 || pw>32)return 0;
    /* Conservative exact-target password limit, not generic WPA's63 bytes. */
    if(c->key_mgmt==4)return pw==0;
    return c->key_mgmt==2 && pw>=8;
}
static int ap_valid(const WifiRuntime *r,const WifiOps *o){
    return r->ap && o->has_netif(r->ap) && o->running(2) &&
        o->ip(r->ap)==r->ap_ip && o->mask(r->ap)==r->ap_mask;
}
void wifi_runtime_stop(WifiRuntime *r,const WifiOps *o,uint32_t reason){
    r->ipv4=0;r->reason=reason;
    int ok=1;
    if(r->sta){
        if(o->stop(1))ok=0;
        if(o->has_netif(r->sta)){
            if(o->dhcp(r->sta,0))ok=0;
            if(o->clear_ip(r->sta))ok=0;
        }
    }
    /* Never tear down AP/DHCP server. If radio bit dropped, reapply AP config. */
    if(!o->running(2)){
        if(o->flush(2) || o->start(2))ok=0;
    }
    r->phase=ok && ap_valid(r,o)?WIFI_AP_RETAINED:WIFI_FAILED;
    /* Do NOT clear inhibit_syscfg_save: queued vendor events may arrive late. */
}
int wifi_runtime_begin(WifiRuntime *r,const WifiOps *o,const WifiConfig *c,uint32_t now){
    if(!wifi_config_valid(c)){r->reason=WIFI_INVALID;return 0;}
    if(r->phase==WIFI_WAIT_DHCP || r->phase==WIFI_GOT_IP){r->reason=WIFI_BUSY;return 0;}
    r->ap=o->device(86);r->sta=o->device(85);r->ipv4=0;
    if(!r->ap || !r->sta || !o->has_netif(r->ap) || !o->running(2)){
        r->reason=WIFI_AP_NOT_READY;return 0;
    }
    r->ap_ip=o->ip(r->ap);r->ap_mask=o->mask(r->ap);
    if(!usable(r->ap_ip) || !valid_mask(r->ap_mask) ||
       (o->has_netif(r->sta) && !r->owns_sta_netif)){
        r->reason=WIFI_AP_NOT_READY;return 0;
    }
    r->inhibit_syscfg_save=1;
    if(o->stop(1))goto failed;
    if(!o->has_netif(r->sta)){
        if(o->add(r->sta,"w1"))goto failed;
        r->owns_sta_netif=1;
    }
    if(o->dhcp(r->sta,0) || o->clear_ip(r->sta))goto failed;
    /* Flush uses original AP syscfg but only touches STA interface. Override
     * credentials directly afterwards; never copy password/SSID into syscfg. */
    if(o->flush(1) || o->configure_sta(c) || o->start(1) || o->dhcp(r->sta,1))goto failed;
    r->phase=WIFI_WAIT_DHCP;r->reason=WIFI_OK;r->started=now;return 1;
failed:
    wifi_runtime_stop(r,o,WIFI_DRIVER);return 0;
}
void wifi_runtime_poll(WifiRuntime *r,const WifiOps *o,uint32_t now,int associated){
    if(r->phase!=WIFI_WAIT_DHCP && r->phase!=WIFI_GOT_IP)return;
    if(r->phase==WIFI_WAIT_DHCP && (uint32_t)(now-r->started)>=45000u){
        wifi_runtime_stop(r,o,WIFI_DHCP_TIMEOUT);return;
    }
    if(r->phase==WIFI_GOT_IP && !associated){wifi_runtime_stop(r,o,WIFI_LINK_LOST);return;}
    uint32_t ip=o->ip(r->sta),mask=o->mask(r->sta);
    if(associated && usable(ip) && valid_mask(mask)){
        /* The built-in AP192.168.1.1/24 often overlaps household LANs. Do not
         * silently promise LAN reachability with two overlapping lwIP routes. */
        if((ip&r->ap_mask)==(r->ap_ip&r->ap_mask) || (ip&mask)==(r->ap_ip&mask)){
            wifi_runtime_stop(r,o,WIFI_SUBNET_CONFLICT);return;
        }
        r->ipv4=ip;r->phase=WIFI_GOT_IP;return;
    }
    r->ipv4=0;
    if(r->phase==WIFI_GOT_IP || (uint32_t)(now-r->started)>=45000u)
        wifi_runtime_stop(r,o,WIFI_DHCP_TIMEOUT);
}
int wifi_ipv4_text(uint32_t ip,char out[16]){
    uint32_t at=0;
    if(!usable(ip)){out[0]=0;return 0;}
    for(uint32_t i=0;i<4;i++){
        uint32_t n=(ip>>(8*i))&255u;
        if(i)out[at++]='.';
        if(n>=100)out[at++]=(char)('0'+n/100);
        if(n>=10)out[at++]=(char)('0'+(n/10)%10);
        out[at++]=(char)('0'+n%10);
    }
    out[at]=0;return 1;
}
void wifi_ip_screen(const WifiRuntime *r,char out[16],uint32_t *visible){
    *visible=r->phase==WIFI_GOT_IP && wifi_ipv4_text(r->ipv4,out);
    if(!*visible)out[0]=0;
}
