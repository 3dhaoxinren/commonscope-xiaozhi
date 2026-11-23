#pragma once
#include "../../shared/bridge.h"
typedef struct {
 CsBridge bridge;
 unsigned raw,stable; uint32_t raw_since;
} CommonScopePtt;
/* Must bind these to VERIFIED native ABI before linking a flash image. */
uint32_t cs_port_lock(void);
void cs_port_unlock(uint32_t token);
void cs_port_uart_response(const char *bytes,unsigned length);
/* ctx allocated/zeroed once. Call in worker every25ms.
   blocked must include all prompts, poweroff and screen sleep states. */
int commonscope_ptt_step(CommonScopePtt*,uint32_t now,unsigned raw_keys,int blocked);
/* Native AT+CSNG handler forwards exactly one8-char arg (no arbitrary AT).
   Format and bounds validated before touching bridge state. */
int commonscope_ptt_query(CommonScopePtt*,uint32_t now,const char *arg,unsigned length);
