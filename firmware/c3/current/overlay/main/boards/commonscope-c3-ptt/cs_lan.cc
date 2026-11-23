#include "cs_lan.h"
#include "maintenance_ui.h"
#include "follow_vad.h"
#include "application.h"
#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <memory>
#include <new>
#include "esp_app_desc.h"
#include "esp_http_client.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_ota_ops.h"
#include "esp_random.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/sockets.h"
#include "nvs_flash.h"
#include "psa/crypto.h"
namespace cs_lan {
namespace {
constexpr int DiscoveryPort = 44731, HttpsPort = 44732, N = 96;
constexpr char Lane[] = "-cs3lan";
struct Row {
    uint32_t ms, value;
    uint16_t event;
};
Row ring[N]{};
unsigned seq = 0, fill = 0;
portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;
std::atomic<unsigned> hint{Off};
void cancel_hold(){ unsigned h=hint.load(); if(h==Hold1||h==Hold2) hint.compare_exchange_strong(h,Off); }
std::atomic<bool> active{false}, saw_pcm{false};
std::atomic<bool> saw_follow{false};
std::atomic<bool> weather{false}, saw_wake{false}, saw_submit{false};
uint32_t ms() { return uint32_t(esp_timer_get_time() / 1000); }
struct Pair {
    char ca[1152]{}, token[65]{}, id[13]{};
    uint8_t pub[65]{};
};
struct Job {
    Pair pair;
    char host[16]{}, sha[65]{};
    size_t size = 0;
    char body[3584]{};
};
int hv(char c) { return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1; }
bool decode(const char* p, uint8_t* out, size_t n) {
    for (size_t i = 0; i < n; i++) {
        int a = hv(p[2 * i]), b = hv(p[2 * i + 1]);
        if (a < 0 || b < 0)
            return false;
        out[i] = (a << 4) | b;
    }
    return true;
}
void hex(const uint8_t* p, size_t n, char* b) {
    const char* h = "0123456789abcdef";
    for (size_t i = 0; i < n; i++) {
        b[2 * i] = h[p[i] >> 4];
        b[2 * i + 1] = h[p[i] & 15];
    }
    b[2 * n] = 0;
}
bool pairing(Pair& p) {
    if (nvs_flash_init_partition("maint") != ESP_OK)
        return false;
    nvs_handle_t h;
    if (nvs_open_from_partition("maint", "pair", NVS_READONLY, &h) != ESP_OK)
        return false;
    size_t ca = sizeof(p.ca), tok = sizeof(p.token), pk = sizeof(p.pub);
    bool ok = nvs_get_str(h, "ca", p.ca, &ca) == ESP_OK &&
              nvs_get_str(h, "token", p.token, &tok) == ESP_OK && tok == 65 &&
              nvs_get_blob(h, "signpub", p.pub, &pk) == ESP_OK && pk == 65 && p.pub[0] == 4;
    nvs_close(h);
    uint8_t mac[6];
    if (esp_efuse_mac_get_default(mac) != ESP_OK)
        return false;
    hex(mac, 6, p.id);
    return ok;
}
// Discovery identifies a candidate PC only. TLS authenticates it before any log or token is sent.
bool discover(Job& j) {
    esp_netif_ip_info_t ip{};
    auto ni = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (!ni || esp_netif_get_ip_info(ni, &ip) != ESP_OK || !ip.ip.addr || !ip.netmask.addr)
        return false;
    int fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (fd < 0)
        return false;
    int on = 1;
    timeval tv{1, 0};
    setsockopt(fd, SOL_SOCKET, SO_BROADCAST, &on, sizeof(on));
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    sockaddr_in dest{};
    dest.sin_family = AF_INET;
    dest.sin_port = htons(DiscoveryPort);
    dest.sin_addr.s_addr = ip.ip.addr | ~ip.netmask.addr;
    char q[64], reply[64], expected[64];
    unsigned nonce = esp_random();
    snprintf(q, sizeof(q), "CS3F %s %08x", j.pair.id, nonce);
    snprintf(expected, sizeof(expected), "CS3P %s %08x", j.pair.id, nonce);
    bool ok = false;
    for (int i = 0; i < 12 && !ok; i++) {
        sendto(fd, q, strlen(q), 0, (sockaddr*)&dest, sizeof(dest));
        sockaddr_in peer{};
        socklen_t n = sizeof(peer);
        int len = recvfrom(fd, reply, sizeof(reply), 0, (sockaddr*)&peer, &n);
        if (len != (int)strlen(expected) || memcmp(reply, expected, len) ||
            peer.sin_port != htons(DiscoveryPort) ||
            (peer.sin_addr.s_addr & ip.netmask.addr) != (ip.ip.addr & ip.netmask.addr))
            continue;
        ok = inet_ntop(AF_INET, &peer.sin_addr, j.host, sizeof(j.host)) != nullptr;
    }
    close(fd);
    return ok;
}
struct Http {
    esp_http_client_handle_t h = nullptr;
    ~Http() {
        if (h)
            esp_http_client_cleanup(h);
    }
};
bool connect(Http& c, const Job& j, const char* path, const char* post, int& status, int64_t& len) {
    char url[144], auth[76];
    snprintf(url, sizeof(url), "https://%s:%d%s", j.host, HttpsPort, path);
    esp_http_client_config_t cfg{};
    cfg.url = url;
    cfg.cert_pem = j.pair.ca;
    cfg.common_name = "cs-maint.local";
    cfg.timeout_ms = 6000;
    cfg.disable_auto_redirect = true;
    cfg.buffer_size = 1536;
    cfg.buffer_size_tx = 1024;
    c.h = esp_http_client_init(&cfg);
    if (!c.h)
        return false;
    snprintf(auth, sizeof(auth), "Bearer %s", j.pair.token);
    if (esp_http_client_set_header(c.h, "Authorization", auth) != ESP_OK ||
        esp_http_client_set_header(c.h, "X-CS-Device", j.pair.id) != ESP_OK)
        return false;
    const int n = post ? (int)strlen(post) : 0;
    if (post) {
        if (esp_http_client_set_method(c.h, HTTP_METHOD_POST) != ESP_OK ||
            esp_http_client_set_header(c.h, "Content-Type", "application/json") != ESP_OK)
            return false;
    }
    if (esp_http_client_open(c.h, n) != ESP_OK)
        return false;
    for (int off = 0; off < n;) {
        int k = esp_http_client_write(c.h, post + off, n - off);
        if (k <= 0)
            return false;
        off += k;
    }
    len = esp_http_client_fetch_headers(c.h);
    status = esp_http_client_get_status_code(c.h);
    return len >= 0 && !esp_http_client_is_chunked_response(c.h);
}
bool read_exact(Http& c, char* dst, int len) {
    for (int at = 0; at < len;) {
        int k = esp_http_client_read(c.h, dst + at, len - at);
        if (k <= 0)
            return false;
        at += k;
    }
    return esp_http_client_is_complete_data_received(c.h);
}
bool manifest(Job& j, char* s, size_t len) {
    char id[13] = {}, sha[65] = {}, sig[129] = {};
    unsigned size = 0;
    int end = 0;
    if (sscanf(s, "CS3LAN1\n%12[0-9a-f]\n%u\n%64[0-9a-f]\n%128[0-9a-f]\n%n", id, &size, sha, sig,
               &end) != 4 ||
        end != (int)len || strcmp(id, j.pair.id) || strlen(sha) != 64 || strlen(sig) != 128 ||
        size < 1024 || size > 0x1d0000)
        return false;
    char canonical[128];
    int clen = snprintf(canonical, sizeof(canonical), "CS3LAN1\n%s\n%u\n%s\n", id, size, sha);
    if (len != (size_t)(clen + 129) || memcmp(canonical, s, clen) || s[len - 1] != '\n')
        return false;
    uint8_t signature[64];
    if (!decode(sig, signature, 64) || psa_crypto_init() != PSA_SUCCESS)
        return false;
    psa_key_attributes_t attr = PSA_KEY_ATTRIBUTES_INIT;
    psa_set_key_usage_flags(&attr, PSA_KEY_USAGE_VERIFY_MESSAGE);
    psa_set_key_algorithm(&attr, PSA_ALG_ECDSA(PSA_ALG_SHA_256));
    psa_set_key_type(&attr, PSA_KEY_TYPE_ECC_PUBLIC_KEY(PSA_ECC_FAMILY_SECP_R1));
    psa_set_key_bits(&attr, 256);
    mbedtls_svc_key_id_t pub{};
    auto st = psa_import_key(&attr, j.pair.pub, 65, &pub);
    psa_reset_key_attributes(&attr);
    if (st != PSA_SUCCESS)
        return false;
    st = psa_verify_message(pub, PSA_ALG_ECDSA(PSA_ALG_SHA_256), (const uint8_t*)canonical, clen,
                            signature, 64);
    psa_destroy_key(pub);
    if (st != PSA_SUCCESS)
        return false;
    j.size = size;
    memcpy(j.sha, sha, 65);
    return true;
}
bool stream(Job& j) {
    auto run = esp_ota_get_running_partition();
    auto dst = esp_ota_get_next_update_partition(nullptr);
    if (!run || !dst || run->address == dst->address || j.size > dst->size || dst->size != 0x1d0000)
        return false;
    esp_ota_img_states_t state;
    if (esp_ota_get_state_partition(run, &state) == ESP_OK && state == ESP_OTA_IMG_PENDING_VERIFY)
        return false;
    char path[90];
    snprintf(path, sizeof(path), "/v1/image/%s", j.sha);
    Http c;
    int status = 0;
    int64_t len = 0;
    if (!connect(c, j, path, nullptr, status, len) || status != 200 || len != (int64_t)j.size)
        return false;
    hint.store(Download);
    esp_ota_handle_t handle = 0;
    if (esp_ota_begin(dst, j.size, &handle) != ESP_OK)
        return false;
    psa_hash_operation_t op = PSA_HASH_OPERATION_INIT;
    bool ok = psa_hash_setup(&op, PSA_ALG_SHA_256) == PSA_SUCCESS;
    size_t count = 0;
    uint32_t start = ms();
    while (ok && count < j.size && ms() - start < 180000) {
        int n = esp_http_client_read(c.h, j.body, std::min<size_t>(sizeof(j.body), j.size - count));
        ok = n > 0 && psa_hash_update(&op, (uint8_t*)j.body, n) == PSA_SUCCESS &&
             esp_ota_write(handle, j.body, n) == ESP_OK;
        if (ok) {
            count += n;
            hint.store(WriteHint(count,j.size));
        }
    }
    if (ok && count == j.size) hint.store(Verify);
    uint8_t sum[32], expected[32];
    size_t sz = 0;
    ok = ok && count == j.size && esp_http_client_is_complete_data_received(c.h) &&
         psa_hash_finish(&op, sum, sizeof(sum), &sz) == PSA_SUCCESS && sz == 32 &&
         decode(j.sha, expected, 32) && memcmp(sum, expected, 32) == 0;
    psa_hash_abort(&op);
    if (!ok) {
        esp_ota_abort(handle);
        return false;
    }
    if (esp_ota_end(handle) != ESP_OK)
        return false;
    esp_app_desc_t desc{};
    if (esp_ota_get_partition_description(dst, &desc) != ESP_OK ||
        strncmp(desc.project_name, "xiaozhi", sizeof(desc.project_name)) ||
        !memchr(desc.version, 0, sizeof(desc.version)) || !strstr(desc.version, Lane))
        return false;
    return esp_ota_set_boot_partition(dst) == ESP_OK;
}
void snapshot(Job& j) {
    Row local[N];
    unsigned n;
    portENTER_CRITICAL(&lock);
    n = fill;
    for (unsigned i = 0; i < n; i++)
        local[i] = ring[(seq - n + i) % N];
    portEXIT_CRITICAL(&lock);
    auto part = esp_ota_get_running_partition();
    esp_ota_img_states_t st;
    int img = esp_ota_get_state_partition(part, &st) == ESP_OK ? (int)st : -1;
    char elf[65];
    hex(esp_app_get_description()->app_elf_sha256, 32, elf);
    size_t k = snprintf(j.body, sizeof(j.body),
                        "{\"version\":\"%s\",\"elf_sha256\":\"%s\",\"slot\":\"%s\",\"image_state\":"
                        "%d,\"uptime_ms\":%lu,\"heap\":%lu,\"events\":[",
                        esp_app_get_description()->version, elf, part->label, img,
                        (unsigned long)ms(), (unsigned long)esp_get_free_heap_size());
    for (unsigned i = 0; i < n && k + 46 < sizeof(j.body); i++)
        k += snprintf(j.body + k, sizeof(j.body) - k, "%s[%lu,%u,%lu]", i ? "," : "",
                      (unsigned long)local[i].ms, (unsigned)local[i].event,
                      (unsigned long)local[i].value);
    snprintf(j.body + k, sizeof(j.body) - k, "]}");
}
int session(Job& j) {
    for (int i = 0; i < 70 && weather.load(); ++i)
        vTaskDelay(pdMS_TO_TICKS(100));
    if (weather.load())
        return 9;
    if (!pairing(j.pair))
        return 1;
    if (!discover(j))
        return 2;
    hint.store(Contact);
    snapshot(j);
    {
        Http c;
        int status;
        int64_t len;
        if (!connect(c, j, "/v1/logs", j.body, status, len))
            return 3;
        if (status == 204 && len == 0)
            return 0;
        if (status != 200 || len < 1 || len > 256 || !read_exact(c, j.body, (int)len))
            return 4;
        j.body[len] = 0;
        if (!manifest(j, j.body, len))
            return 5;
    }
    if (!stream(j))
        return 6;
    note(Verified);
    hint.store(Restart);
    {
        Http c;
        int status;
        int64_t len;
        connect(c, j, "/v1/staged", "{\"staged\":true}", status, len);
    }
    return 7;
}
void worker(void*) {
    int rc;
    {
        std::unique_ptr<Job> j(new (std::nothrow) Job());
        rc = j ? session(*j) : 8;
    }
    if (rc == 7) {
        vTaskDelay(pdMS_TO_TICKS(1800));
        esp_restart();
    }
    hint.store(rc ? unsigned(Failure)+rc : unsigned(Done));
    if (rc)
        note(Error, rc);
    active.store(false);
    Application::GetInstance().Schedule([] {
        auto& app = Application::GetInstance();
        if (!active.load() && app.GetDeviceState() == kDeviceStateIdle)
            app.GetAudioService().EnableWakeWordDetection(true);
    });
    vTaskDelete(nullptr);
}
void launch_worker() {
    Application::GetInstance().Schedule([] {
        auto& app = Application::GetInstance();
        if (app.GetDeviceState() != kDeviceStateIdle) {
            hint.store(Failure+9);
            active.store(false);
            return;
        }
        app.GetAudioService().EnableWakeWordDetection(false);
        app.GetAudioService().ReleaseWakeWordResources();
        if (xTaskCreate(worker, "cs-lan", 8192, nullptr, 3, nullptr) != pdPASS) {
            active.store(false);
            hint.store(Failure+8);
            note(Error, 8);
            app.GetAudioService().EnableWakeWordDetection(true);
        }
    });
}
}  // namespace
unsigned panel_hint() { return hint.load(); }
void path_seen(unsigned v) {
    if (v == 5) saw_wake.store(true);
    if (v == 6) saw_submit.store(true);
    if (v == 7) saw_follow.store(true);
    note(Key, v); // Existing v06.3 numeric log readers remain compatible.
}
void note(Event e, uint32_t v) {
    const uint32_t t = ms();
    portENTER_CRITICAL(&lock);
    ring[seq % N] = {t, v, (uint16_t)e};
    seq++;
    if (fill < N)
        fill++;
    portEXIT_CRITICAL(&lock);
}
void mic(const int16_t* pcm, int n) {
    cs_fvad::pcm(pcm,n);
    static uint32_t last = 0, peak = 0;
    uint32_t now = ms();
    if (n > 0) {
        saw_pcm.store(true);
        for (int i = 0; i < n; i++)
            peak = std::max(peak, (uint32_t)abs((int)pcm[i]));
    }
    if (now - last >= 1000) {
        note(n > 0 ? MicPeak : MicTimeout, peak);
        peak = 0;
        last = now;
    }
}
void sent(bool ok) {
    static uint32_t last = 0, n = 0, bad = 0;
    n += ok;
    bad += !ok;
    uint32_t now = ms();
    if (now - last >= 1000) {
        if (n)
            note(AudioSent, n);
        if (bad)
            note(AudioSendFail, bad);
        n = bad = 0;
        last = now;
    }
}
bool busy() { return active.load(); }
bool try_weather() {
    weather.store(true);
    if (!active.load())
        return true;
    weather.store(false);
    return false;
}
void end_weather() { weather.store(false); }
void tick(uint32_t now, bool linked, bool idle, bool fresh, unsigned key, unsigned state) {
    static bool begin = true, pending = false, armed = false, fired = false;
    static uint32_t since = 0, last_key = 0, old = ~0u;
    if (begin) {
        begin = false;
        note(Boot, (uint32_t)esp_reset_reason());
        esp_ota_img_states_t st;
        pending = esp_ota_get_state_partition(esp_ota_get_running_partition(), &st) == ESP_OK &&
                  st == ESP_OTA_IMG_PENDING_VERIFY;
    }
    if (old != state) {
        note(State, state);
        old = state;
    }
    if (pending) {
        if (now > 10000 && linked && saw_pcm.load() && saw_wake.load() && saw_submit.load() && saw_follow.load() &&
            esp_ota_mark_app_valid_cancel_rollback() == ESP_OK) {
            pending = false;
            note(BootAccepted);
        } else if (now > 300000) {
            pending = false;
            note(Rollback);
            esp_ota_mark_app_invalid_rollback_and_reboot();
        }
    }
    if (!linked || !idle || (armed && now - last_key > 300)) {
        cancel_hold();
        armed = false;
        fired = true;
    }
    if (!fresh)
        return;
    last_key = now;
    if (key != 1) {
        cancel_hold();
        armed = false;
        fired = key != 0;
        return;
    }
    if (!idle || !linked || fired)
        return;
    if (!armed) {
        armed = true;
        since = now;
        return;
    }
    if (!active.load() && now-since>=700u && now-since<3000u) hint.store(now-since<2000u ? Hold2 : Hold1);
    if (now - since >= 3000) {
        fired = true;
        armed = false;
        if (active.exchange(true))
            return;
        hint.store(Search);
        note(Window);
        launch_worker();
    }
}
}  // namespace cs_lan

extern "C" bool cs_maintenance_busy() { return cs_lan::busy(); }
extern "C" void cs_voice_path_seen(unsigned v) { cs_lan::path_seen(v); }
extern "C" void cs_observe_audio_send(bool ok) { cs_lan::sent(ok); }
