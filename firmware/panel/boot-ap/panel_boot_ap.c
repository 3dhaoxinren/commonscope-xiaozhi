#include "panel_boot_ap.h"

void panel_boot_ap_init(PanelBootAp *state) {
    if (state) {
        *state = (PanelBootAp){0};
    }
}

PanelBootApStatus panel_boot_ap_step(PanelBootAp *state,
                                    const PanelBootApPort *port,
                                    uint32_t now_ms) {
    if (!state || !port || !port->network_ready || !port->ap_ready ||
        !port->request_start) {
        return PANEL_BOOT_AP_BAD_PORT;
    }
    if (state->ready) {
        return PANEL_BOOT_AP_READY;
    }
    if (!port->network_ready(port->ctx)) {
        return PANEL_BOOT_AP_WAIT_NETWORK;
    }
    if (port->ap_ready(port->ctx)) {
        state->ready = true;
        return PANEL_BOOT_AP_READY;
    }
    if (state->requests &&
        (uint32_t)(now_ms - state->last_request_ms) < PANEL_BOOT_AP_RETRY_MS) {
        return PANEL_BOOT_AP_WAIT_READY;
    }
    if (state->requests >= PANEL_BOOT_AP_MAX_REQUESTS) {
        return PANEL_BOOT_AP_EXHAUSTED;
    }

    ++state->requests;
    state->last_request_ms = now_ms;
    return port->request_start(port->ctx) ? PANEL_BOOT_AP_WAIT_READY :
                                            PANEL_BOOT_AP_START_REJECTED;
}
