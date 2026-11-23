#pragma once

#include <driver/temperature_sensor.h>
#include <esp_log.h>

#include <cmath>
#include <cstdint>

// Used by one board worker. No network, GPIO, NVS, or thermal shutdown policy.
class ChipTemperatureMonitor {
public:
    void Poll(uint32_t now_ms, const char* state) {
        if (stopped_ || (sampled_ && (now_ms - last_sample_ms_) < kIntervalMs)) {
            return;
        }
        sampled_ = true;
        last_sample_ms_ = now_ms;

        if (handle_ == nullptr) {
            const temperature_sensor_config_t config =
                TEMPERATURE_SENSOR_CONFIG_DEFAULT(kMinCelsius, kMaxCelsius);
            const auto err = temperature_sensor_install(&config, &handle_);
            if (err != ESP_OK) {
                ESP_LOGW(kTag, "Temperature diagnostic unavailable: %s", esp_err_to_name(err));
                stopped_ = true;
                return;
            }
            ESP_LOGI(kTag, "ESP32-C3 die only, range %d..%d C, interval 10 s",
                     kMinCelsius, kMaxCelsius);
        }

        const auto enable_status = temperature_sensor_enable(handle_);
        if (enable_status != ESP_OK) {
            ESP_LOGW(kTag, "Sensor enable failed: %s", esp_err_to_name(enable_status));
            return;
        }
        float celsius = 0;
        const auto read_status = temperature_sensor_get_celsius(handle_, &celsius);
        const auto disable_status = temperature_sensor_disable(handle_);
        if (disable_status != ESP_OK) {
            ESP_LOGW(kTag, "Sensor disable failed; stop diagnostics: %s",
                     esp_err_to_name(disable_status));
            stopped_ = true;
            return;
        }
        if (read_status != ESP_OK || !std::isfinite(celsius) || celsius < kMinCelsius ||
            celsius > kMaxCelsius) {
            ESP_LOGW(kTag, "ESP32-C3 die sample unavailable (read=%s, range %d..%d C)",
                     esp_err_to_name(read_status), kMinCelsius, kMaxCelsius);
            return;
        }
        if (!has_peak_ || celsius > peak_celsius_) {
            peak_celsius_ = celsius;
            has_peak_ = true;
        }
        ESP_LOGI(kTag, "ESP32-C3 die=%.1f C, peak=%.1f C, uptime=%lu s, state=%s",
                 static_cast<double>(celsius), static_cast<double>(peak_celsius_),
                 static_cast<unsigned long>(now_ms / 1000), state);
    }

private:
    static constexpr const char* kTag = "C3DieTemp";
    static constexpr int kMinCelsius = 20;
    static constexpr int kMaxCelsius = 100;
    static constexpr uint32_t kIntervalMs = 10000;
    temperature_sensor_handle_t handle_ = nullptr;
    uint32_t last_sample_ms_ = 0;
    float peak_celsius_ = 0;
    bool sampled_ = false;
    bool has_peak_ = false;
    bool stopped_ = false;
};
