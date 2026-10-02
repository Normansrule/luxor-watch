#include "state.h"
#include <stdio.h>
#include <string.h>
watch_state_t g_state={.mic_dbfs=-96.0f,.wifi_rssi=-127};
portMUX_TYPE g_state_mux=portMUX_INITIALIZER_UNLOCKED;
void state_read(watch_state_t *out){ STATE_LOCK(); *out=g_state; STATE_UNLOCK(); }
void state_notice(const char *s){STATE_LOCK();snprintf(g_state.notice,sizeof(g_state.notice),"%s",s);g_state.notification_pending=true;STATE_UNLOCK();}
void state_radio_status(const char *s){STATE_LOCK();snprintf(g_state.radio_status,sizeof(g_state.radio_status),"%s",s);STATE_UNLOCK();}
