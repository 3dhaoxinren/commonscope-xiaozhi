#ifndef COMMONSCOPE_PANEL_BOOT_AP_H
#define COMMONSCOPE_PANEL_BOOT_AP_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PANEL_BOOT_AP_RETRY_MS 3000u
#define PANEL_BOOT_AP_MAX_REQUESTS 3u

typedef enum {
    PANEL_BOOT_AP_BAD_PORT,
    PANEL_BOOT_AP_WAIT_NETWORK,
    PANEL_BOOT_AP_WAIT_READY,
    PANEL_BOOT_AP_START_REJECTED,
    PANEL_BOOT_AP_READY,
    PANEL_BOOT_AP_EXHAUSTED
} PanelBootApStatus;

typedef struct {
    void *ctx;
    bool (*network_ready)(void *ctx);
    bool (*ap_ready)(void *ctx);
    bool (*request_start)(void *ctx);
} PanelBootApPort;

typedef struct {
    uint32_t last_request_ms;
    uint8_t requests;
    bool ready;
} PanelBootAp;

/* Initialize once per boot (or use a zero-initialized static instance). */
void panel_boot_ap_init(PanelBootAp *state);
/* Call from one task, not an ISR, only in a startup mode that permits AP.
 * Supply a monotonic 32-bit millisecond clock. The callbacks must not block.
 * ap_ready means AP + local network interface + DHCP service are ready.
 * request_start uses the board's existing profile; already starting is accepted.
 * Once READY, this helper does not restart a deliberately stopped AP. */
PanelBootApStatus panel_boot_ap_step(PanelBootAp *state,
                                    const PanelBootApPort *port,
                                    uint32_t now_ms);

#ifdef __cplusplus
}
#endif
#endif
