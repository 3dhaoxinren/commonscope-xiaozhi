#ifndef CS_PORT_H
#define CS_PORT_H
#include "../../shared/bridge.h"
/* All calls must run on SAME native worker; AT handler must enqueue only.
   Never pass UART IRQ buffers directly or hijack global AT output callback.
   b must be allocated once from native PSRAM and explicitly zeroed.
   cs_step returns1: wp->ip_visible=1; wp->ip_shown=now.
   Key must be debounced50ms; plus=2, minus=1 from confirmed gate_core.
   allowed=false: Wi-Fi gate, wallpaper gate, legacy confirmation, sleep,
   native shutdown, or UI not ready. Preserve keys==3 forget-WiFi handling.
   cs_poll consumes only8 uppercase hex characters from AT+CSNG=<nonce>.
   Register a NEW command; retain AT+APP and existing recovery commands.
   Do not bind addresses until matched on exact B07B/native image. */
#endif
