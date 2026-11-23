#pragma once
#include <cstdint>
#include <cstdio>
namespace cs_lan {
enum Hint : unsigned { Off=0, Hold2, Hold1, Search, Contact, Done, Verify, Restart, Download=32, Failure=64 };
inline unsigned WriteHint(size_t wrote, size_t total) {
    return Download + (total ? (unsigned)(wrote * 100u / total) / 10u * 10u : 0u) / 10u;
}
inline bool Terminal(unsigned h) { return h==Done || (h>=Failure && h<=Failure+9); }
inline const char* HintText(unsigned h, char (&b)[43]) {
    switch(h) {
    case Hold2: return "进入维护: 再按住 2 秒";
    case Hold1: return "进入维护: 再按住 1 秒";
    case Search: return "维护: 正在找电脑";
    case Contact: return "维护: 正在连接电脑";
    case Done: return "维护: 日志已发送";
    case Verify: return "更新: 正在校验";
    case Restart: return "校验通过 即将重启";
    case Failure+2: return "未找到电脑 请重试";
    case Failure+3: return "电脑连接失败 E3";
    case Failure+5: return "更新签名失败 E5";
    case Failure+6: return "更新失败 E6 请重试";
    default:
        if(h>=Download && h<=Download+10) { snprintf(b,sizeof(b),"更新: 已写入 %u%%",(h-Download)*10);return b; }
        if(Terminal(h)) {snprintf(b,sizeof(b),"维护失败 E%u",h-Failure);return b;}
        return nullptr;
    }
}
// Owned by the panel UART task only. Terminal states outlast B07E's 5s IP banner.
// Once expired, the same latched terminal value must not open the card again.
struct HintView {
    unsigned raw=Off, sent=Off; uint32_t changed=0, last=0;
    unsigned visible(unsigned h,uint32_t t) {
        if(h!=raw){raw=h;changed=t;}
        return Terminal(h) && t-changed>=8000u ? (unsigned)Off : h;
    }
    bool due(unsigned h,uint32_t t) const {return h!=sent || t-last>=1500u;}
    void published(unsigned h,uint32_t t){sent=h;last=t;}
};
unsigned panel_hint();
}
