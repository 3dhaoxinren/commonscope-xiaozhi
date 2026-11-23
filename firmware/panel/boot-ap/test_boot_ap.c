#include <assert.h>
#include <stdio.h>
#include "panel_boot_ap.h"

typedef struct {
    bool driver_up, ap_up, dhcp_up, accepts_start;
    unsigned calls;
} Fake;

static bool network_ready(void *ctx) { return ((Fake *)ctx)->driver_up; }
static bool ap_ready(void *ctx) {
    Fake *fake = ctx;
    return fake->ap_up && fake->dhcp_up;
}
static bool request_start(void *ctx) {
    Fake *fake = ctx;
    ++fake->calls;
    return fake->accepts_start;
}

int main(void) {
    Fake f = {0};
    PanelBootAp s;
    const PanelBootApPort port = {&f, network_ready, ap_ready, request_start};
    const PanelBootApPort incomplete = {&f, network_ready, ap_ready, 0};
    panel_boot_ap_init(&s);

    assert(panel_boot_ap_step(&s, &incomplete, 3) == PANEL_BOOT_AP_BAD_PORT);
    assert(panel_boot_ap_step(0, &port, 3) == PANEL_BOOT_AP_BAD_PORT);
    assert(f.calls == 0);

    assert(panel_boot_ap_step(&s, &port, 10) == PANEL_BOOT_AP_WAIT_NETWORK);
    assert(f.calls == 0);

    f = (Fake){.driver_up = true, .ap_up = true, .dhcp_up = true};
    assert(panel_boot_ap_step(&s, &port, 12) == PANEL_BOOT_AP_READY);
    assert(f.calls == 0);

    panel_boot_ap_init(&s);
    f = (Fake){.driver_up = true, .accepts_start = true};
    assert(panel_boot_ap_step(&s, &port, 20) == PANEL_BOOT_AP_WAIT_READY);
    f.ap_up = true;
    assert(panel_boot_ap_step(&s, &port, 21) == PANEL_BOOT_AP_WAIT_READY);
    assert(f.calls == 1);
    f.dhcp_up = true;
    assert(panel_boot_ap_step(&s, &port, 22) == PANEL_BOOT_AP_READY);
    f.ap_up = false;
    assert(panel_boot_ap_step(&s, &port, 99999) == PANEL_BOOT_AP_READY);
    assert(f.calls == 1);

    panel_boot_ap_init(&s);
    f = (Fake){.driver_up = true};
    for (unsigned n = 0; n < PANEL_BOOT_AP_MAX_REQUESTS; ++n) {
        const uint32_t ms = n * PANEL_BOOT_AP_RETRY_MS;
        assert(panel_boot_ap_step(&s, &port, ms) == PANEL_BOOT_AP_START_REJECTED);
        assert(panel_boot_ap_step(&s, &port, ms + PANEL_BOOT_AP_RETRY_MS - 1)
               == PANEL_BOOT_AP_WAIT_READY);
        assert(f.calls == n + 1);
    }
    const uint32_t deadline = PANEL_BOOT_AP_MAX_REQUESTS * PANEL_BOOT_AP_RETRY_MS;
    assert(panel_boot_ap_step(&s, &port, deadline) == PANEL_BOOT_AP_EXHAUSTED);
    assert(f.calls == PANEL_BOOT_AP_MAX_REQUESTS);

    f.ap_up = f.dhcp_up = true;
    assert(panel_boot_ap_step(&s, &port, deadline + 1) == PANEL_BOOT_AP_READY);
    assert(f.calls == PANEL_BOOT_AP_MAX_REQUESTS);

    panel_boot_ap_init(&s);
    f = (Fake){.driver_up = true, .accepts_start = true};
    const uint32_t near_wrap = UINT32_MAX - 15u;
    assert(panel_boot_ap_step(&s, &port, near_wrap) == PANEL_BOOT_AP_WAIT_READY);
    assert(panel_boot_ap_step(&s, &port, near_wrap + PANEL_BOOT_AP_RETRY_MS - 1)
           == PANEL_BOOT_AP_WAIT_READY);
    assert(f.calls == 1);
    assert(panel_boot_ap_step(&s, &port, near_wrap + PANEL_BOOT_AP_RETRY_MS)
           == PANEL_BOOT_AP_WAIT_READY);
    assert(f.calls == 2);

    puts("PASS 7 panel boot AP host scenarios (no device I/O)");
}
