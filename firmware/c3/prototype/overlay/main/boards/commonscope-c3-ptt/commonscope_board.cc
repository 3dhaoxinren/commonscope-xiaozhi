#include "wifi_board.h"
#include "application.h"
#include "codecs/no_audio_codec.h"
#include "display/display.h"
#include "config.h"
#include "bridge.h"
#include <driver/uart.h>
#include <esp_random.h>
#include <esp_timer.h>
#include <cstring>
#include <atomic>

class CommonScopeC3 : public WifiBoard {
    NoDisplay display_;  // Original board keeps rendering its own clock/IP.
    std::atomic<bool> poll_pending_{false};
    static void Worker(void*arg) {
        auto*self=static_cast<CommonScopeC3*>(arg);
        for(;;){
            if(!self->poll_pending_.exchange(true)) {
                Application::GetInstance().Schedule([self](){self->Poll();self->poll_pending_=false;});
            }
            vTaskDelay(pdMS_TO_TICKS(50));
        }
    }
    bool have_seq_=false, owned_=false;
    uint32_t last_seq_=0, seen_=0;
    void Poll() {  // Main application task; no direct calls from UART IRQ.
        auto&app=Application::GetInstance();
        uint32_t now=(uint32_t)(esp_timer_get_time()/1000);
        auto state=app.GetDeviceState();
        if(owned_ && (uint32_t)(now-seen_)>=350){
            app.CsPttIntent(3);owned_=false;have_seq_=false;
        }
        // Do not keep native PTT armed while playing TTS/configuring/activating.
        if(state!=kDeviceStateIdle && !(owned_ && (state==kDeviceStateConnecting||state==kDeviceStateListening)))return;
        uart_flush_input(CS_UART);
        uint32_t nonce=esp_random();char request[20]="AT+CSNG=";
        cs_hex(request+8,nonce,8);request[16]='\r';request[17]='\n';
        if(uart_write_bytes(CS_UART,request,18)!=18)return;
        char line[160];unsigned used=0;uint8_t byte;
        int64_t deadline=esp_timer_get_time()+25000;
        while(esp_timer_get_time()<deadline){
            if(uart_read_bytes(CS_UART,&byte,1,pdMS_TO_TICKS(2))!=1)continue;
            if(used==sizeof(line))used=0;
            line[used++]=(char)byte;
            if(byte!='\n')continue;
            uint32_t seq,mode;
            // Match a complete marked line only. Ignore native boot logs/OK.
            bool good=cs_reply(line,used,nonce,&seq,&mode);used=0;
            if(!good)continue;
            seen_=now;
            if(have_seq_ && seq==last_seq_)return;
            if(have_seq_ && (int32_t)(seq-last_seq_)<=0)return;
            bool baseline=!have_seq_;have_seq_=true;last_seq_=seq;
            if(mode==1){if(!baseline&&state==kDeviceStateIdle){owned_=true;app.CsPttIntent(1);}}
            else if(owned_){owned_=false;app.CsPttIntent(mode==2?2:3);}
            return;
        }
    }
public:
    CommonScopeC3(){
        uart_config_t cfg={};cfg.baud_rate=CS_BAUD;cfg.data_bits=UART_DATA_8_BITS;
        cfg.parity=UART_PARITY_DISABLE;cfg.stop_bits=UART_STOP_BITS_1;
        cfg.flow_ctrl=UART_HW_FLOWCTRL_DISABLE;cfg.source_clk=UART_SCLK_DEFAULT;
        ESP_ERROR_CHECK(uart_param_config(CS_UART,&cfg));
        ESP_ERROR_CHECK(uart_set_pin(CS_UART,CS_TX,CS_RX,UART_PIN_NO_CHANGE,UART_PIN_NO_CHANGE));
        ESP_ERROR_CHECK(uart_driver_install(CS_UART,1024,0,0,nullptr,0));
        if(xTaskCreate(Worker,"cs-uart",4096,this,3,nullptr)!=pdPASS)ESP_ERROR_CHECK(ESP_ERR_NO_MEM);
    }
    AudioCodec* GetAudioCodec()override{
        static NoAudioCodecDuplex codec(CS_RATE,CS_RATE,CS_BCLK,CS_WS,CS_DOUT,CS_DIN);
        return &codec;
    }
    Display* GetDisplay()override{return &display_;}
};
DECLARE_BOARD(CommonScopeC3);
