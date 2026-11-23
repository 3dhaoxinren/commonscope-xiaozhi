/* Optional C3 -> TXW818 view protocol, no UART, rendering or flash side effects.
 * Frame: @XZ1|kind|UPPERCASE_HEX_UTF8|CRC16\n. CRC covers kind through payload
 * (kind|payload). Pass each received byte through xz_feed; copy event for UI. */
#ifndef XZ_DISPLAY_H
#define XZ_DISPLAY_H
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#define XZ_TEXT_MAX 192u
#define XZ_FRAME_MAX (13u + 2u * XZ_TEXT_MAX)
typedef struct { char kind; char text[XZ_TEXT_MAX+1]; } xz_event;
typedef struct { char line[XZ_FRAME_MAX]; size_t n; } xz_reader; /* zero-init */
static inline unsigned xz_width(const unsigned char *p, size_t n) {
    if (!n) return 0;
    unsigned c=p[0], w=(c<128)?1:(c>=194 && c<=223)?2:(c>=224 && c<=239)?3:(c>=240 && c<=244)?4:0;
    if (!w || n<w || c<32 || c==127) return 0;
    for(unsigned i=1;i<w;i++) if((p[i]&192)!=128) return 0;
    if((c==224 && p[1]<160)||(c==237 && p[1]>=160)||(c==240 && p[1]<144)||(c==244 && p[1]>=144)
       || (c==194 && p[1]<160)) return 0;
    return w;
}
static inline int xz_valid(char kind,const unsigned char *s,size_t n) {
    if (!s || !n || n>XZ_TEXT_MAX) return 0;
    if (kind=='S') {
        const char *const states[]={"offline","connecting","idle","listening","thinking","speaking"};
        for(unsigned i=0;i<sizeof(states)/sizeof(states[0]);i++)
            if(strlen(states[i])==n && !memcmp(s,states[i],n)) return 1;
        return 0;
    }
    if(kind!='U' && kind!='A') return 0;
    for(size_t i=0;i<n;) {unsigned w=xz_width(s+i,n-i); if(!w) return 0; i+=w;}
    return 1;
}
static inline unsigned xz_crc(const char *p,size_t n) {
    unsigned c=65535;
    while(n--) {c^=(unsigned)(unsigned char)*p++ << 8;
        for(int i=0;i<8;i++) c=((c<<1)^((c&32768)?0x1021u:0u))&65535;}
    return c;
}
static inline int xz_hex(char c) {
    return (c>='0' && c<='9')?c-'0':(c>='A' && c<='F')?c-'A'+10:-1;
}
/* Returns byte count (wire data is not NUL terminated); refuses oversize text.
 * Caller can split long subtitles on UTF-8 character boundaries. */
static inline size_t xz_pack(char *out,size_t cap,char kind,const char *s,size_t n) {
    static const char h[]="0123456789ABCDEF";
    if (!out || !xz_valid(kind,(const unsigned char*)s,n) || cap<13+2*n) return 0;
    memcpy(out,"@XZ1|",5);out[5]=kind;out[6]='|';size_t i=7;
    for(size_t k=0;k<n;k++){unsigned v=(unsigned char)s[k];out[i++]=h[v>>4];out[i++]=h[v&15];}
    unsigned c=xz_crc(out+5,i-5);out[i++]='|';
    for(int k=12;k>=0;k-=4) out[i++]=h[(c>>k)&15];
    out[i++]='\n';return i;
}
static inline int xz_feed(xz_reader *r,unsigned char b,xz_event *ev) {
    if(b=='@'){r->line[0]='@';r->n=1;return 0;}
    if(!r->n)return 0;
    if(b!='\n'){if(b<32 || b>126 || r->n>=sizeof(r->line)){r->n=0;return 0;}
        r->line[r->n++]=(char)b;return 0;}
    const char *p=r->line;size_t n=r->n;r->n=0;
    if(n<14 || n>12+2*XZ_TEXT_MAX || memcmp(p,"@XZ1|",5) || p[6]!='|' || p[n-5]!='|' || ((n-12)%2)) return 0;
    unsigned c=0;for(size_t i=n-4;i<n;i++){int h=xz_hex(p[i]);if(h<0)return 0;c=(c<<4)|(unsigned)h;}
    if(xz_crc(p+5,n-10)!=c) return 0;
    xz_event e;size_t len=(n-12)/2;
    for(size_t i=0;i<len;i++){int a=xz_hex(p[7+2*i]),d=xz_hex(p[8+2*i]);if(a<0 || d<0)return 0;e.text[i]=(char)((a<<4)|d);}
    if(!xz_valid(p[5],(const unsigned char*)e.text,len))return 0;
    e.kind=p[5];e.text[len]=0;ev->kind=e.kind;memcpy(ev->text,e.text,len+1);return 1;
}
#endif
