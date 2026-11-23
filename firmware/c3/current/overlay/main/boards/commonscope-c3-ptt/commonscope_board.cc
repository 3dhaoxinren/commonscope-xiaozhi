#include "application.h"
#include "assets/lang_config.h"
#include "bridge.h"
#include "chip_temperature.h"
#include "panel_audio_codec.h"
#include "cs_lan.h"
#include "config.h"
#include "display/display.h"
#include "ptt_receiver.h"
#include "wifi_board.h"
#if CONFIG_COMMONSCOPE_C3_RXMON
#include "green_uart.h"
#if CONFIG_COMMONSCOPE_C3_XZVIEW
#include "panel_info.h"
#include "xz_board_display.h"
extern "C" void commonscope_clock_anchor(uint32_t utc) { panel_clock_anchor_impl(utc); }
#endif
#endif

#include <driver/uart.h>
#include <esp_log.h>
#include <esp_random.h>
#include <esp_timer.h>

#if CONFIG_USE_ESP_WAKE_WORD
#include <model_path.h>
// App-mapped constants: model and code roll back together. No partition writes.
alignas(16) static const uint8_t cs_wake_weights[] = {
#include "wake_weights.inc"
};
#endif

class CommonScopeC3 : public WifiBoard {
    static constexpr const char* kTag = "CommonScopeC3";
#if CONFIG_COMMONSCOPE_C3_XZVIEW
    XzBoardDisplay display_;
    PanelInfo info_;
    PanelGesture gesture_;
    bool have_query_ = false, was_live_ = false;
    uint32_t query_ = 0, time_ = 0, wx_ = 0;
    bool sync_ = true;
    uint32_t poll_sent_ = 0, good_reply_ = 0, report_at_ = 0;
    void PanelTick() {
        auto& app = Application::GetInstance();
        const uint32_t now = Millis();
        auto state = app.GetDeviceState();
        bool idle_for_keys = state == kDeviceStateIdle;
#if CONFIG_BOARD_TYPE_COMMONSCOPE_C3_PTT && CONFIG_USE_ESP_WAKE_WORD
        idle_for_keys = idle_for_keys && !app.CsFollowActive();
        static uint32_t tick_follow=0;
        if((app.CsFollowActive() || state==kDeviceStateListening) && now-tick_follow>=100u){
            tick_follow=now;app.CsServiceFollow();
        }
#endif

        unsigned ev = gesture_.Expire(now);
        if (ev) {
            cs_lan::note(cs_lan::Key, 4);
            ESP_LOGW(kTag, "panel link lost: cancel");
            app.CsPttIntent(ev);
        }
        uint16_t seq;
        unsigned key = 3;
        uint32_t at;
        bool fresh = green_.Pop(seq, key, at) && now - at < 150;
        if (fresh) {
            ++good_reply_;
            ev = gesture_.Sample(at, seq, key, idle_for_keys && !cs_lan::busy(),
                                 state == kDeviceStateIdle || state == kDeviceStateConnecting ||
                                     state == kDeviceStateListening);
            if (ev) {
                cs_lan::note(cs_lan::Key, ev);
                ESP_LOGI(kTag, "panel + %s", ev == 1 ? "START" : ev == 2 ? "SUBMIT" : "CANCEL");
                app.CsPttIntent(ev);
            }
        }
        cs_lan::tick(now, gesture_.live, idle_for_keys, fresh, key, unsigned(state));
        if (gesture_.live != was_live_) {
            cs_lan::note(cs_lan::PanelLink, gesture_.live);
            ESP_LOGI(kTag, "panel %s", gesture_.live ? "linked" : "disconnected");
            display_.Link(gesture_.live);
            sync_ = true;
            was_live_ = gesture_.live;
        }
        if (!have_query_ || now - query_ >= (gesture_.live ? 75u : 450u)) {
            have_query_ = true;
            query_ = now;
            uint32_t nonce = esp_random();
            char q[17] = "@Q2|";
            phx(q + 4, nonce, 8);
            pseal(q, 12);
            green_.Expect(nonce);
            if (display_.Send(q))
                ++poll_sent_;
        }
        if (now - report_at_ >= 10000u) {
            report_at_ = now;
            ESP_LOGI(kTag, "panel UART sent=%lu valid_reply=%lu linked=%u",
                     (unsigned long)poll_sent_, (unsigned long)good_reply_,
                     (unsigned)gesture_.live);
        }
        if (!gesture_.live)
            return;
        display_.Tick(now, state);
        if (sync_ || now - time_ >= 3000) {
            uint32_t t = panel_now();
            time_ = now;
            if (t) {
                char msg[17] = "@T2|";
                phx(msg + 4, t, 8);
                pseal(msg, 12);
                display_.Send(msg);
            }
        }
        if (sync_ || now - wx_ >= 5000) {
            wx_ = now;
            char msg[34];
            if (info_.Weather(msg))
                display_.Send(msg);
        }
        sync_ = false;
    }
#else
    NoDisplay display_;
#endif
#if CONFIG_COMMONSCOPE_C3_RXMON
    GreenUart green_;
#endif
    ChipTemperatureMonitor chip_temperature_;
    CsPttReceiver receiver_{};
    Ptt bench_ptt_{};
    unsigned raw_ = 0, stable_ = 0;
    uint32_t changed_ = 0, started_ = 0;
    bool offline_test_ = false;

    static uint32_t Millis() { return static_cast<uint32_t>(esp_timer_get_time() / 1000); }

    static void UartWorker(void* arg) {
        auto* self = static_cast<CommonScopeC3*>(arg);
        for (;;) {
            self->PollUart();
            self->PollChipTemperature();
            vTaskDelay(pdMS_TO_TICKS(25));
        }
    }

    static void BenchWorker(void* arg) {
        auto* self = static_cast<CommonScopeC3*>(arg);
        for (;;) {
#if CONFIG_COMMONSCOPE_C3_XZVIEW
            self->PanelTick();
#else
            self->PollBootButton();
#endif
            self->PollChipTemperature();
            vTaskDelay(pdMS_TO_TICKS(20));
        }
    }

    void PollChipTemperature() {
        auto state = Application::GetInstance().GetDeviceState();
        chip_temperature_.Poll(Millis(), DeviceStateMachine::GetStateName(state));
    }

    void PollUart() {
        auto& app = Application::GetInstance();
        unsigned expired = cs_rx_expire(&receiver_, Millis());
        if (expired != CS_RX_NONE)
            app.CsPttIntent(expired);

        auto state = app.GetDeviceState();
        if (state != kDeviceStateIdle && !(receiver_.owned && (state == kDeviceStateConnecting ||
                                                               state == kDeviceStateListening))) {
            return;
        }

        uart_flush_input(CS_UART);
        uint32_t nonce = esp_random();
        char request[18] = "AT+CSNG=";
        cs_hex(request + 8, nonce, 8);
        request[16] = '\r';
        request[17] = '\n';
        if (uart_write_bytes(CS_UART, request, sizeof(request)) != sizeof(request))
            return;

        char line[33];
        unsigned used = 0;
        bool truncated = false;
        uint8_t ch;
        const int64_t deadline = esp_timer_get_time() + 25000;
        while (esp_timer_get_time() < deadline) {
            if (uart_read_bytes(CS_UART, &ch, 1, pdMS_TO_TICKS(2) + 1) != 1)
                continue;
            if (used < sizeof(line))
                line[used++] = static_cast<char>(ch);
            else
                truncated = true;
            if (ch != '\n')
                continue;

            uint32_t seq, mode;
            const bool good = !truncated && cs_reply(line, used, nonce, &seq, &mode);
            used = 0;
            truncated = false;
            if (!good)
                continue;

            const uint32_t now = Millis();
            expired = cs_rx_expire(&receiver_, now);
            if (expired != CS_RX_NONE)
                app.CsPttIntent(expired);
            const unsigned event =
                cs_rx_accept(&receiver_, now, seq, mode, app.GetDeviceState() == kDeviceStateIdle);
            if (event != CS_RX_NONE)
                app.CsPttIntent(event);
            return;
        }
    }

    void PollBootButton() {
        auto& app = Application::GetInstance();
        const uint32_t now = Millis();
        const unsigned raw = gpio_get_level(CS_BOOT) == 0 ? 2u : 0u;
        if (raw != raw_) {
            raw_ = raw;
            changed_ = now;
        } else if ((uint32_t)(now - changed_) >= 40) {
            stable_ = raw_;
        }

        const auto state = app.GetDeviceState();
        const bool recording = bench_ptt_.state == RECORDING;
        const bool allowed =
            state == kDeviceStateIdle || state == kDeviceStateWifiConfiguring ||
            (recording && (state == kDeviceStateAudioTesting || state == kDeviceStateConnecting ||
                           state == kDeviceStateListening));
        auto event = ptt_step(&bench_ptt_, now, stable_, allowed, true);
        if (bench_ptt_.state == RECORDING &&
            (uint32_t)(now - started_) >= (offline_test_ ? 5000u : 30000u) &&
            event != START_AUDIO) {
            bench_ptt_.state = REQUIRE_RELEASE;
            event = offline_test_ ? SUBMIT_AUDIO : CANCEL_AUDIO;
        }
        switch (event) {
            case SHOW_IP:  // Short press is an audible speaker test on the standalone build.
                app.Schedule([] {
                    auto& instance = Application::GetInstance();
                    const auto state = instance.GetDeviceState();
                    if (state == kDeviceStateIdle || state == kDeviceStateWifiConfiguring) {
                        instance.PlaySound(Lang::Sounds::OGG_POPUP);
                    }
                });
                break;
            case START_AUDIO:
                started_ = now;
                offline_test_ = state == kDeviceStateWifiConfiguring;
                ESP_LOGI(kTag, "%s capture requested",
                         offline_test_ ? "Offline bench" : "Cloud PTT");
                app.CsPttIntent(CS_RX_START);
                break;
            case SUBMIT_AUDIO:
                app.CsPttIntent(CS_RX_SUBMIT);
                break;
            case CANCEL_AUDIO:
                app.CsPttIntent(CS_RX_CANCEL);
                break;
            default:
                break;
        }
    }

    void StartUartWorker() {
        uart_config_t cfg{};
        cfg.baud_rate = CS_BAUD;
        cfg.data_bits = UART_DATA_8_BITS;
        cfg.parity = UART_PARITY_DISABLE;
        cfg.stop_bits = UART_STOP_BITS_1;
        cfg.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
        cfg.source_clk = UART_SCLK_DEFAULT;
        ESP_ERROR_CHECK(uart_param_config(CS_UART, &cfg));
        ESP_ERROR_CHECK(
            uart_set_pin(CS_UART, CS_TX, CS_RX, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
        ESP_ERROR_CHECK(uart_driver_install(CS_UART, 1024, 0, 0, nullptr, 0));
        ESP_ERROR_CHECK(xTaskCreate(UartWorker, "cs-uart", 4096, this, 3, nullptr) == pdPASS
                            ? ESP_OK
                            : ESP_ERR_NO_MEM);
    }

    void StartBenchWorker() {
        gpio_config_t cfg{};
        cfg.pin_bit_mask = UINT64_C(1) << CS_BOOT;
        cfg.mode = GPIO_MODE_INPUT;
        cfg.pull_up_en = GPIO_PULLUP_ENABLE;
        ESP_ERROR_CHECK(gpio_config(&cfg));
        raw_ = stable_ = gpio_get_level(CS_BOOT) == 0 ? 2u : 0u;
        changed_ = Millis();
#if CONFIG_COMMONSCOPE_C3_RXMON
        green_.Start();
#if CONFIG_COMMONSCOPE_C3_XZVIEW
        if (!display_.Start())
            ESP_LOGE(kTag, "panel UART TX unavailable");
        info_.Start();
        ESP_LOGI(kTag, "v06.4: plus=PTT; WakeNet9s; hold minus 3s for paired LAN maintenance");
#endif
#else
        ESP_LOGI(kTag, "BENCH: UART bridge off; tap BOOT for tone, hold 500 ms for audio");
#endif
        ESP_ERROR_CHECK(xTaskCreate(BenchWorker, "cs-bench", 4096, this, 3, nullptr) == pdPASS
                            ? ESP_OK
                            : ESP_ERR_NO_MEM);
    }

public:
    CommonScopeC3() {
#if CONFIG_COMMONSCOPE_C3_BENCH
        StartBenchWorker();
#else
        StartUartWorker();
#endif
    }

    AudioCodec* GetAudioCodec() override {
#if CONFIG_USE_ESP_WAKE_WORD
        static const bool attached = []() {
            auto* model = srmodel_load(const_cast<uint8_t*>(cs_wake_weights));
            if (model == nullptr || model->num != 1) return false;
            Application::GetInstance().GetAudioService().SetModelsList(model);
            ESP_LOGI("CsWake", "single-app WakeNet9s model attached");
            return true;
        }();
        (void)attached;
#endif
        static PanelAudioCodec codec(CS_RATE, CS_RATE, CS_BCLK, CS_WS, CS_DOUT, CS_DIN);
        return &codec;
    }

    Display* GetDisplay() override { return &display_; }
};

DECLARE_BOARD(CommonScopeC3);
