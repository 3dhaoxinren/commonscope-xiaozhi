#pragma once
#include <esp_heap_caps.h>
#include <esp_timer.h>
#include <cJSON.h>
#include <array>
#include <cmath>
#include <ctime>
#include <memory>
#include "application.h"
#include "cs_lan.h"
#include "board.h"
#include "nvs.h"
#include "panel_wire.h"
// Wall UTC from OTA response before XiaoZhi's local-time offset is applied.
inline portMUX_TYPE ptime_mux = portMUX_INITIALIZER_UNLOCKED;
inline int64_t ptime_base = 0;
inline uint32_t ptime_epoch = 0;
extern "C" inline void panel_clock_anchor_impl(uint32_t utc) {
    portENTER_CRITICAL(&ptime_mux);
    ptime_base = esp_timer_get_time();
    ptime_epoch = utc;
    portEXIT_CRITICAL(&ptime_mux);
}
inline uint32_t panel_now() {
    portENTER_CRITICAL(&ptime_mux);
    uint32_t t = ptime_epoch;
    int64_t b = ptime_base;
    portEXIT_CRITICAL(&ptime_mux);
    return t ? (uint32_t)(t + (esp_timer_get_time() - b) / 1000000) : 0;
}
class PanelInfo {
    portMUX_TYPE mu_ = portMUX_INITIALIZER_UNLOCKED;
    char frame_[34]{};
    void Publish(const char* s) {
        portENTER_CRITICAL(&mu_);
        memcpy(frame_, s, sizeof(frame_));
        portEXIT_CRITICAL(&mu_);
    }
    static bool Good(const char* s) {
        uint32_t a, b, c, d, e, f, h;
        return pcheck(s, 33, "@W2|", 29) && phread(s + 4, 8, &a) && phread(s + 12, 4, &b) &&
               phread(s + 16, 4, &c) && phread(s + 20, 4, &d) && phread(s + 24, 2, &e) &&
               phread(s + 26, 2, &f) && phread(s + 28, 1, &h) && h <= 1 &&
               pweather(psigned(b), psigned(c), psigned(d), e, f, a);
    }
    static void Cache(char* s, bool save) {
        nvs_handle_t h;
        if (nvs_open("panelwx", save ? NVS_READWRITE : NVS_READONLY, &h) != ESP_OK)
            return;
        if (save) {
            if (nvs_set_blob(h, "shanghai1", s, 34) == ESP_OK)
                (void)nvs_commit(h);
        } else {
            size_t n = 34;
            if (nvs_get_blob(h, "shanghai1", s, &n) != ESP_OK || n != 34 || s[33] || !Good(s))
                s[0] = 0;
        }
        nvs_close(h);
    }
    bool Fetch(char* out) {
        auto& board = Board::GetInstance();
        auto net = board.GetNetwork();
        if (!net || heap_caps_get_largest_free_block(MALLOC_CAP_8BIT) < 28000)
            return false;
        auto h = net->CreateHttp(0);
        if (!h)
            return false;
        h->SetTimeout(4500);
        h->SetHeader("Accept-Encoding", "identity");
        if (!h->Open("GET",
                     "https://api.open-meteo.com/v1/"
                     "forecast?latitude=31.2304&longitude=121.4737&current=temperature_2m,relative_"
                     "humidity_2m,weather_code&daily=temperature_2m_max,temperature_2m_min&"
                     "forecast_days=1&timezone=Asia%2FShanghai&timeformat=unixtime"))
            return false;
        auto status = h->GetStatusCode();
        if (!status || *status != 200 || h->GetBodyLength() > 4096)
            return false;
        std::unique_ptr<char[]> data(new (std::nothrow) char[4097]);
        if (!data)
            return false;
        size_t n = 0;
        int64_t deadline = esp_timer_get_time() + 6500000;
        for (;;) {
            if (n == 4096 || esp_timer_get_time() > deadline ||
                Application::GetInstance().GetDeviceState() != kDeviceStateIdle)
                return false;
            auto got = h->Read(data.get() + n, 4096 - n);
            if (!got || *got < 0)
                return false;
            if (*got == 0)
                break;
            n += *got;
        }
        h->Close();
        data[n] = 0;
        std::unique_ptr<cJSON, decltype(&cJSON_Delete)> root(
            cJSON_ParseWithLength(data.get(), n + 1), cJSON_Delete);
        if (!root)
            return false;
        auto current = cJSON_GetObjectItemCaseSensitive(root.get(), "current"),
             daily = cJSON_GetObjectItemCaseSensitive(root.get(), "daily");
        auto num = [](cJSON* o, const char* k, bool list, double lo, double hi, double& v) {
            auto p = cJSON_GetObjectItemCaseSensitive(o, k);
            if (list)
                p = cJSON_GetArrayItem(p, 0);
            if (!cJSON_IsNumber(p) || !std::isfinite(p->valuedouble) || p->valuedouble < lo ||
                p->valuedouble > hi)
                return false;
            v = p->valuedouble;
            return true;
        };
        double t, l, u, r, c, a;
        if (!num(current, "time", false, 1704067200., 4102444799., a) ||
            !num(current, "temperature_2m", false, -90, 65, t) ||
            !num(daily, "temperature_2m_min", true, -90, 65, l) ||
            !num(daily, "temperature_2m_max", true, -90, 65, u) ||
            !num(current, "relative_humidity_2m", false, 0, 100, r) ||
            !num(current, "weather_code", false, 0, 99, c) || l > u)
            return false;
        memcpy(out, "@W2|", 4);
        phx(out + 4, (uint32_t)a, 8);
        phx(out + 12, (uint32_t)(int)lround(t * 10), 4);
        phx(out + 16, (uint32_t)(int)lround(l * 10), 4);
        phx(out + 20, (uint32_t)(int)lround(u * 10), 4);
        phx(out + 24, (unsigned)r, 2);
        phx(out + 26, (unsigned)c, 2);
        out[28] = '0';
        pseal(out, 29);
        return Good(out);
    }
    void Run() {
        char row[34] = {}, old[34] = {};
        Cache(row, false);
        if (row[0]) {
            memcpy(old, row, 34);
            row[28] = '1';
            pseal(row, 29);
            Publish(row);
        }
        for (;;) {
            if (Application::GetInstance().GetDeviceState() != kDeviceStateIdle) {
                vTaskDelay(pdMS_TO_TICKS(3000));
                continue;
            }
            if (!cs_lan::try_weather()) {
                vTaskDelay(pdMS_TO_TICKS(3000));
                continue;
            }
            bool ok = Fetch(row);
            cs_lan::end_weather();
            if (ok) {
                Publish(row);
                if (memcmp(row, old, 28)) {
                    Cache(row, true);
                    memcpy(old, row, 34);
                }
            }
            vTaskDelay(pdMS_TO_TICKS(ok ? 900000 : 120000));
        }
    }

public:
    void Start() {
        if (xTaskCreate([](void* p) { static_cast<PanelInfo*>(p)->Run(); }, "panel-wx", 5120, this,
                        2, nullptr) != pdPASS)
            ESP_LOGW("Panel", "weather task unavailable");
    }
    bool Weather(char* out) {
        portENTER_CRITICAL(&mu_);
        memcpy(out, frame_, 34);
        portEXIT_CRITICAL(&mu_);
        return out[0] != 0;
    }
};
