#pragma once

#include "config.h"
#include "green_rx.h"
#include "mcp_server.h"
#include "panel_rx.h"

#include <driver/uart.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>

#include <algorithm>
#include <cinttypes>
#include <cstdio>
#include <string>

class GreenUart {
public:
    void Expect(uint32_t n) {
        portENTER_CRITICAL(&lock_);
        expect_ = n;
        waiting_ = true;
        ready_ = false;
        portEXIT_CRITICAL(&lock_);
    }
    bool Pop(uint16_t& s, unsigned& k, uint32_t& at) {
        portENTER_CRITICAL(&lock_);
        bool ok = ready_;
        if (ok) {
            s = seq_;
            k = key_;
            at = at_;
            ready_ = false;
        }
        portEXIT_CRITICAL(&lock_);
        return ok;
    }
    void Start() {
        McpServer::GetInstance().AddTool(
            "self.commonscope.get_green_temperature",
            "查询原屏绿板 TXW818 的串口温度字段和接线状态。不是 C3 温度。raw "
            "数值单位未知，不要报作摄氏度；stale 是旧值，no_data 或 no_field 时没有温度读数。",
            PropertyList(), [this](const PropertyList&) -> ToolResult {
                GreenStats s;
                portENTER_CRITICAL(&lock_);
                s = published_;
                const bool ok = running_;
                portEXIT_CRITICAL(&lock_);
                if (!ok)
                    return std::string("uart_unavailable: 原屏绿板串口监听未启动");
                char msg[220];
                Format(s, esp_timer_get_time() / 1000, msg, sizeof(msg));
                return std::string(msg);
            });
        uart_config_t u{};
        u.baud_rate = CS_BAUD;
        u.data_bits = UART_DATA_8_BITS;
        u.parity = UART_PARITY_DISABLE;
        u.stop_bits = UART_STOP_BITS_1;
        u.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
        u.source_clk = UART_SCLK_DEFAULT;
        esp_err_t err = uart_param_config(CS_UART, &u);
        if (err == ESP_OK)
            err = uart_set_pin(CS_UART, UART_PIN_NO_CHANGE, CS_RX, UART_PIN_NO_CHANGE,
                               UART_PIN_NO_CHANGE);
        if (err == ESP_OK)
            err = gpio_set_pull_mode(CS_RX, GPIO_FLOATING);
        if (err == ESP_OK)
            err = uart_driver_install(CS_UART, 4096, 0, 32, &queue_, 0);
        if (err == ESP_OK && xTaskCreate(Run, "green-rx", 4096, this, 3, nullptr) != pdPASS) {
            uart_driver_delete(CS_UART);
            err = ESP_ERR_NO_MEM;
        }
        if (err != ESP_OK) {
            ESP_LOGE(kTag, "UART unavailable (%s); BOOT audio stays enabled", esp_err_to_name(err));
            return;
        }
        portENTER_CRITICAL(&lock_);
        running_ = true;
        portEXIT_CRITICAL(&lock_);
#if CONFIG_COMMONSCOPE_C3_XZVIEW
        ESP_LOGI(kTag, "v06.1 panel UART: GPIO3 RX and GPIO10 TX both 1000000 8N1");
#else
        ESP_LOGI(kTag, "v04 RX-only GPIO3 1000000 8N1; GPIO10 not assigned");
#endif
    }

private:
    static constexpr const char* kTag = "GreenUART";
    QueueHandle_t queue_ = nullptr;
    portMUX_TYPE lock_ = portMUX_INITIALIZER_UNLOCKED;
    GreenRx rx_;
    PanelLine parser_{};
    uint32_t expect_ = 0, at_ = 0;
    unsigned key_ = 3;
    uint16_t seq_ = 0;
    bool waiting_ = false, ready_ = false;
    void PanelBytes(const uint8_t* p, size_t len, uint32_t at) {
        for (size_t i = 0; i < len; i++) {
            uint32_t n;
            uint16_t s;
            unsigned k;
            if (parser_.Feed(p[i], n, s, k)) {
                portENTER_CRITICAL(&lock_);
                if (waiting_ && expect_ == n) {
                    ready_ = true;
                    waiting_ = false;
                    at_ = at;
                    key_ = k;
                    seq_ = s;
                }
                portEXIT_CRITICAL(&lock_);
            }
        }
    }

    GreenStats published_;
    bool running_ = false;

    static void Format(const GreenStats& s, int64_t now, char* out, size_t n) {
        int count = std::snprintf(out, n,
                                  "source=green_uart status=%s bytes=%" PRIu64 " samples=%" PRIu64
                                  " uart_errors=%" PRIu32,
                                  s.Status(now), s.bytes, s.samples, s.faults);
        if (s.samples && count >= 0 && static_cast<size_t>(count) < n)
            std::snprintf(out + count, n - count,
                          " reported_raw=%" PRId32 " unit=unverified age_s=%" PRId64, s.raw,
                          (now - s.last_ms) / 1000);
    }

    static void Run(void* arg) { static_cast<GreenUart*>(arg)->Listen(); }
    void Listen() {
        uint8_t bytes[256];
        int64_t next = 0, next_publish = 0;
        for (;;) {
            uart_event_t event{};
            if (xQueueReceive(queue_, &event, pdMS_TO_TICKS(100))) {
                if (event.type == UART_DATA) {
                    for (size_t left = event.size; left;) {
                        int read =
                            uart_read_bytes(CS_UART, bytes, std::min(left, sizeof(bytes)), 0);
                        if (read <= 0) {
                            rx_.Gap();
                            parser_.used = 0;
                            break;
                        }
                        PanelBytes(bytes, read, (uint32_t)(esp_timer_get_time() / 1000));
                        rx_.Feed(bytes, read, esp_timer_get_time() / 1000);
                        left -= static_cast<size_t>(read);
                    }
                } else if (event.type == UART_BUFFER_FULL || event.type == UART_FIFO_OVF ||
                           event.type == UART_FRAME_ERR || event.type == UART_PARITY_ERR ||
                           event.type == UART_BREAK) {
                    rx_.Gap();
                    parser_.used = 0;
                    uart_flush_input(CS_UART);
                    xQueueReset(queue_);
                }
            }
            const int64_t now = esp_timer_get_time() / 1000;
            if (now >= next_publish) {
                next_publish = now + 250;
                portENTER_CRITICAL(&lock_);
                published_ = rx_.stats;
                portEXIT_CRITICAL(&lock_);
            }
            if (now >= next) {
                next = now + 5000;
                char msg[220];
                Format(rx_.stats, now, msg, sizeof(msg));
                ESP_LOGI(kTag, "%s", msg);
            }
        }
    }
};
