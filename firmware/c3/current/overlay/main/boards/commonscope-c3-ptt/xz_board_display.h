#pragma once
#include <driver/uart.h>
#include <esp_log.h>
#include <cstring>
#include "application.h"
#include "config.h"
#include "display/display.h"
#include "xz_display.h"
#include "cs_lan.h"
#include "maintenance_ui.h"
#if !CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG || !CONFIG_ESP_CONSOLE_SECONDARY_NONE
#error "UART0 must not be the console"
#endif
class XzBoardDisplay final : public NoDisplay {
    static constexpr size_t Limit = 42;
    struct Msg {
        char kind;
        char text[Limit + 1];
    };
    portMUX_TYPE mu_ = portMUX_INITIALIZER_UNLOCKED;
    Msg q_[4]{};
    unsigned rd_ = 0, count_ = 0;
    bool enabled_ = false, uart_ready_ = false;
    uint32_t last_ = 0;
    int sent_ = -1;
    cs_lan::HintView hint_view_{};

public:
    bool Start() {
        uart_config_t u{};
        u.baud_rate = CS_BAUD;  // Same three-pad console, both directions at 1 Mbaud.
        u.data_bits = UART_DATA_8_BITS;
        u.parity = UART_PARITY_DISABLE;
        u.stop_bits = UART_STOP_BITS_1;
        u.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
        u.source_clk = UART_SCLK_DEFAULT;
        if (uart_param_config(UART_NUM_0, &u) != ESP_OK ||
            uart_driver_install(UART_NUM_0, 256, 256, 0, nullptr, 0) != ESP_OK)
            return false;
        if (uart_set_pin(UART_NUM_0, CS_TX, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE,
                         UART_PIN_NO_CHANGE) != ESP_OK) {
            uart_driver_delete(UART_NUM_0);
            return false;
        }
        return uart_ready_ = true;
    }
    void Link(bool up) {
        portENTER_CRITICAL(&mu_);
        if (enabled_ != up) {
            enabled_ = up;
            rd_ = count_ = 0;
            sent_ = -1;
            hint_view_.sent = cs_lan::Off;
        }
        portEXIT_CRITICAL(&mu_);
    }
    bool Send(const char* payload) {
        if (!uart_ready_)
            return false;
        char cmd[132];
        int n = snprintf(cmd, sizeof(cmd), "AT+XZVIEW=%s\r\n", payload);
        return n > 0 && (size_t)n < sizeof(cmd) && uart_write_bytes(UART_NUM_0, cmd, n) == n;
    }
    void SetChatMessage(const char* role, const char* s) override {
        if (!role || !s) {
            return;
        }
        char k = !strcmp(role, "user") ? 'U' : !strcmp(role, "assistant") ? 'A' : 0;
        if (!k)
            return;
        if (*s) cs_lan::note(k == 'U' ? cs_lan::Recognized : cs_lan::Reply);
        Msg m{};
        m.kind = k;
        size_t used = 0, n = strnlen(s, Limit + 4);
        for (size_t i = 0; i < n;) {
            const auto* p = (const unsigned char*)s + i;
            if (*p < 32) {
                m.text[used++] = ' ';
                i++;
            } else {
                unsigned w = xz_width(p, n - i);
                if (!w || used + w > Limit) {
                    break;
                }
                memcpy(m.text + used, p, w);
                used += w;
                i += w;
            }
            if (used == Limit)
                break;
        }
        if (!used)
            return;
        portENTER_CRITICAL(&mu_);
        if (enabled_) {
            if (count_ == 4) {
                rd_ = (rd_ + 1) % 4;
                --count_;
            }
            q_[(rd_ + count_) % 4] = m;
            ++count_;
        }
        portEXIT_CRITICAL(&mu_);
    }
    // Called from board worker only, not an audio/network callback.
    void Tick(uint32_t now, DeviceState st) {
        if (!enabled_)
            return;
        // Only this task emits maintenance frames. No fictitious ASR/reply log entries.
        const auto hint=hint_view_.visible(cs_lan::panel_hint(),now);
        if(hint!=cs_lan::Off && st==kDeviceStateIdle) {
            if(hint_view_.due(hint,now)){
                char txt[43]{}, line[110]; const char* msg=cs_lan::HintText(hint,txt);
                size_t n=msg ? xz_pack(line,sizeof(line),'A',msg,strlen(msg)) : 0;
                if(n){line[n-1]=0;if(Send(line)) hint_view_.published(hint,now);}
            }
            return;
        }
        if(hint_view_.sent!=cs_lan::Off){hint_view_.published(cs_lan::Off,now);sent_=-1;}
        {
            Msg m{};
            bool thinking=false;
#if CONFIG_BOARD_TYPE_COMMONSCOPE_C3_PTT && CONFIG_USE_ESP_WAKE_WORD
            thinking=(st==kDeviceStateListening && Application::GetInstance().CsWaitReply());
#endif
            int view=thinking ? 100 : int(st);
            const char* state = thinking ? "thinking" : st == kDeviceStateListening    ? "listening"
                                : st == kDeviceStateSpeaking   ? "speaking"
                                : st == kDeviceStateConnecting ? "connecting"
                                                               : "idle";
            if (sent_ != view || ((st == kDeviceStateSpeaking || st == kDeviceStateListening ||
                                      st == kDeviceStateConnecting) &&
                                     now - last_ >= 1800)) {
                m.kind = 'S';
                strcpy(m.text, state);
                sent_ = view;
                last_ = now;
            } else {
                portENTER_CRITICAL(&mu_);
                if (count_) {
                    m = q_[rd_];
                    rd_ = (rd_ + 1) % 4;
                    --count_;
                }
                portEXIT_CRITICAL(&mu_);
            }
            if (!m.kind)
                return;
            char line[110];
            size_t n = xz_pack(line, sizeof(line), m.kind, m.text, strlen(m.text));
            if (n) {
                line[n - 1] = 0;
                Send(line);
            }
        }
    }
};
