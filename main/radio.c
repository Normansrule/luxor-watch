#include "radio.h"
#include "state.h"
#include "protocol.h"
#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_netif_sntp.h"
#include "esp_wifi.h"
#include "esp_timer.h"
#include "esp_random.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/ble_uuid.h"
#include "host/ble_store.h"
#include "host/util/util.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

// Original custom protocol. UUID byte arrays are little-endian for NimBLE.
static const ble_uuid128_t service_uuid=BLE_UUID128_INIT(0x01,0x10,0x9b,0x9d,0x18,0xac,0x38,0x87,0x4e,0x47,0x3e,0x2b,0x10,0x00,0x5a,0xe7);
static const ble_uuid128_t telemetry_uuid=BLE_UUID128_INIT(0x02,0x10,0x9b,0x9d,0x18,0xac,0x38,0x87,0x4e,0x47,0x3e,0x2b,0x10,0x00,0x5a,0xe7);
static const ble_uuid128_t command_uuid=BLE_UUID128_INIT(0x03,0x10,0x9b,0x9d,0x18,0xac,0x38,0x87,0x4e,0x47,0x3e,0x2b,0x10,0x00,0x5a,0xe7);
static QueueHandle_t queue;
static bool wifi_started,sntp_started;
static volatile bool csi_on;
static volatile bool ble_ready,ble_allowed,subscribed;
static volatile uint16_t connection=BLE_HS_CONN_HANDLE_NONE;
static uint16_t telemetry_handle;
static uint8_t own_address_type;
static int64_t wifi_deadline,ble_deadline;
static uint8_t seen_addresses[24][7];
static size_t seen_count;
void ble_store_config_init(void);
static int gap_event(struct ble_gap_event *event,void *arg);
static void telemetry(uint8_t out[20]){
    watch_state_t s;
    state_read(&s);
    uint8_t flags = (s.imu_ok ? 1 : 0) | (s.wifi_connected ? 2 : 0) | (s.mic_on ? 4 : 0);
    rw_pack_telemetry(out, flags, s.battery_v, s.steps, s.imu);
}
static bool secured(uint16_t conn){
    struct ble_gap_conn_desc d;
    return ble_gap_conn_find(conn,&d)==0&&d.sec_state.encrypted&&d.sec_state.authenticated;
}
static int access_gatt(uint16_t conn,uint16_t attr,struct ble_gatt_access_ctxt *ctx,void *arg){
    (void)attr;(void)arg;
    if(!secured(conn))return BLE_ATT_ERR_INSUFFICIENT_AUTHEN;
    if(ctx->op==BLE_GATT_ACCESS_OP_READ_CHR&&ble_uuid_cmp(ctx->chr->uuid,&telemetry_uuid.u)==0){
        uint8_t b[20];telemetry(b);return os_mbuf_append(ctx->om,b,20)==0?0:BLE_ATT_ERR_INSUFFICIENT_RES;
    }
    if(ctx->op!=BLE_GATT_ACCESS_OP_WRITE_CHR||ble_uuid_cmp(ctx->chr->uuid,&command_uuid.u)!=0)return BLE_ATT_ERR_UNLIKELY;
    uint16_t n=OS_MBUF_PKTLEN(ctx->om);char input[121];
    if(n==0||n>120)return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    if(os_mbuf_copydata(ctx->om,0,n,input)!=0) {
        return BLE_ATT_ERR_UNLIKELY;
    }
    input[n]=0;
    rw_command_t parsed;
    if (!rw_parse_command((const uint8_t *)input, n, &parsed)) return BLE_ATT_ERR_UNLIKELY;
    if (parsed.kind == RW_COMMAND_MESSAGE) {
        state_notice(parsed.message);
        return 0;
    }
    if (parsed.kind == RW_COMMAND_TIME) {
        struct timeval tv = {.tv_sec = (time_t)parsed.unix_seconds};
        if (settimeofday(&tv, NULL) != 0) return BLE_ATT_ERR_UNLIKELY;
        STATE_LOCK(); g_state.time_synced = true; STATE_UNLOCK();
        return 0;
    }
    return BLE_ATT_ERR_UNLIKELY;
}
static const struct ble_gatt_svc_def services[]={
 {.type=BLE_GATT_SVC_TYPE_PRIMARY,.uuid=&service_uuid.u,.characteristics=(struct ble_gatt_chr_def[]){
  {.uuid=&telemetry_uuid.u,.access_cb=access_gatt,.val_handle=&telemetry_handle,
   .flags=BLE_GATT_CHR_F_READ|BLE_GATT_CHR_F_NOTIFY|BLE_GATT_CHR_F_READ_ENC|BLE_GATT_CHR_F_READ_AUTHEN},
  {.uuid=&command_uuid.u,.access_cb=access_gatt,
   .flags=BLE_GATT_CHR_F_WRITE|BLE_GATT_CHR_F_WRITE_ENC|BLE_GATT_CHR_F_WRITE_AUTHEN},
  {0}}}, {0}};
static void advertise(void){
    if(!ble_ready||!ble_allowed||connection!=BLE_HS_CONN_HANDLE_NONE||ble_gap_adv_active())return;
    // UUID + flags fit 31 bytes; name goes in the scan-response packet.
    struct ble_hs_adv_fields f={0};f.flags=BLE_HS_ADV_F_DISC_GEN|BLE_HS_ADV_F_BREDR_UNSUP;
    f.uuids128=(ble_uuid128_t*)&service_uuid;f.num_uuids128=1;f.uuids128_is_complete=1;
    if(ble_gap_adv_set_fields(&f)!=0)return;
    struct ble_hs_adv_fields response={0};response.name=(uint8_t*)"Luxor";
    response.name_len=strlen("Luxor");response.name_is_complete=1;ble_gap_adv_rsp_set_fields(&response);
    struct ble_gap_adv_params p={.conn_mode=BLE_GAP_CONN_MODE_UND,.disc_mode=BLE_GAP_DISC_MODE_GEN,
        .itvl_min=320,.itvl_max=640};
    if(ble_gap_adv_start(own_address_type,NULL,BLE_HS_FOREVER,&p,gap_event,NULL)!=0)state_radio_status("BLE advertising failed");
}
static void ble_sync(void){
    if(ble_hs_util_ensure_addr(0)!=0)return;
    if(ble_hs_id_infer_auto(0,&own_address_type)!=0)return;
    ble_ready=true; // Deliberately do not advertise at boot.
}
static void ble_reset(int reason){(void)reason;ble_ready=false;connection=BLE_HS_CONN_HANDLE_NONE;subscribed=false;
    STATE_LOCK();g_state.ble_connected=false;STATE_UNLOCK();}
static void ble_host(void *arg){(void)arg;nimble_port_run();nimble_port_freertos_deinit();}
static int gap_event(struct ble_gap_event *e,void *arg){
    (void)arg;
    switch(e->type){
    case BLE_GAP_EVENT_CONNECT:
        if(e->connect.status==0){connection=e->connect.conn_handle;ble_gap_security_initiate(connection);
            STATE_LOCK();g_state.ble_connected=true;STATE_UNLOCK();}
        else {
            advertise();
        }
        break;
    case BLE_GAP_EVENT_DISCONNECT:
        connection=BLE_HS_CONN_HANDLE_NONE;subscribed=false;
        STATE_LOCK();g_state.ble_connected=false;STATE_UNLOCK();advertise();break;
    case BLE_GAP_EVENT_ADV_COMPLETE:advertise();break;
    case BLE_GAP_EVENT_SUBSCRIBE:if(e->subscribe.attr_handle==telemetry_handle)subscribed=e->subscribe.cur_notify;break;
    case BLE_GAP_EVENT_PASSKEY_ACTION:{
        struct ble_sm_io io={0};io.action=e->passkey.params.action;
        if(io.action==BLE_SM_IOACT_DISP){watch_state_t s;state_read(&s);io.passkey=s.pairing_pin;
            ble_sm_inject_io(e->passkey.conn_handle,&io);state_radio_status("Enter watch PIN on phone");}
        else ble_gap_terminate(e->passkey.conn_handle,BLE_ERR_REM_USER_CONN_TERM);
        break;}
    case BLE_GAP_EVENT_ENC_CHANGE:
        if(e->enc_change.status!=0||!secured(e->enc_change.conn_handle))
            ble_gap_terminate(e->enc_change.conn_handle,BLE_ERR_REM_USER_CONN_TERM);
        break;
    case BLE_GAP_EVENT_REPEAT_PAIRING:return BLE_GAP_REPEAT_PAIRING_IGNORE; // Never silently replace an existing bond.
    case BLE_GAP_EVENT_DISC:{
        struct ble_hs_adv_fields f={0};if(ble_hs_adv_parse_fields(&f,e->disc.data,e->disc.length_data)!=0)break;
        uint8_t key[7];key[0]=e->disc.addr.type;memcpy(key+1,e->disc.addr.val,6);
        char name[25]="Unnamed advertiser";
        if(f.name&&f.name_len){
            size_t n=f.name_len<24?f.name_len:24;
            for(size_t i=0;i<n;i++) {
                name[i]=(f.name[i]>=32&&f.name[i]<127)?f.name[i]:'?';
            }
            name[n]=0;
        }
        STATE_LOCK();bool known=false;
        for(size_t i=0;i<seen_count;i++)if(memcmp(seen_addresses[i],key,7)==0){known=true;break;}
        if(!known&&seen_count<24){
            memcpy(seen_addresses[seen_count++],key,7);g_state.ble_count=seen_count;
            if(seen_count<=5){size_t at=strlen(g_state.ble_lines);
                snprintf(g_state.ble_lines+at,sizeof(g_state.ble_lines)-at,"%s %d dBm\n",name,e->disc.rssi);}
        }STATE_UNLOCK();break;}
    default:break;
    }
    return 0;
}
static void sntp_synced(struct timeval *tv){(void)tv;STATE_LOCK();g_state.time_synced=true;STATE_UNLOCK();state_radio_status("Time synchronized");}
static void wifi_event(void *arg,esp_event_base_t base,int32_t event,void *data){
    (void)arg;(void)data;
    if(base==IP_EVENT&&event==IP_EVENT_STA_GOT_IP){
        wifi_ap_record_t ap;int rssi=-127;if(esp_wifi_sta_get_ap_info(&ap)==ESP_OK)rssi=ap.rssi;
        STATE_LOCK();g_state.wifi_connected=true;g_state.wifi_rssi=rssi;STATE_UNLOCK();
        if(!sntp_started){esp_sntp_config_t c=ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");c.sync_cb=sntp_synced;
            sntp_started=esp_netif_sntp_init(&c)==ESP_OK;}
        else esp_netif_sntp_start();
        state_radio_status("Wi-Fi connected; syncing time");
    }
    if(base==WIFI_EVENT&&event==WIFI_EVENT_STA_DISCONNECTED){
        STATE_LOCK();g_state.wifi_connected=false;g_state.wifi_rssi=-127;STATE_UNLOCK();
        state_radio_status("Wi-Fi disconnected");
    }
}
#ifdef CONFIG_RW_CSI_EXPERIMENT
static void receive_csi(void *ctx,wifi_csi_info_t *info){
    (void)ctx;if(!info||!info->buf||!csi_on)return;
    size_t start=info->first_word_invalid?4:0;if(info->len<=start)return;
    double sum=0;size_t n=0;
    for(size_t i=start;i+1<info->len;i+=2){double im=info->buf[i],re=info->buf[i+1];sum+=re*re+im*im;n++;}
    if(n){float rms=(float)sqrt(sum/n);STATE_LOCK();g_state.csi_frames++;g_state.csi_rms=rms;STATE_UNLOCK();}
}
#endif
static bool start_wifi(void){
    if(!wifi_started){esp_err_t e=esp_wifi_start();if(e!=ESP_OK){state_radio_status("Wi-Fi start failed");return false;}wifi_started=true;}
    wifi_deadline=esp_timer_get_time()+90000000LL;return true;
}
static void stop_wifi(void){
#ifdef CONFIG_RW_CSI_EXPERIMENT
    if(csi_on)esp_wifi_set_csi(false);
#endif
    csi_on=false;if(sntp_started){esp_netif_sntp_deinit();sntp_started=false;}
    if(wifi_started){esp_wifi_disconnect();esp_wifi_stop();wifi_started=false;}
    STATE_LOCK();g_state.wifi_connected=false;g_state.wifi_rssi=-127;STATE_UNLOCK();
}
static void stop_ble(void){
    ble_allowed=false;
    if(ble_ready){ble_gap_disc_cancel();ble_gap_adv_stop();if(connection!=BLE_HS_CONN_HANDLE_NONE)
        ble_gap_terminate(connection,BLE_ERR_REM_USER_CONN_TERM);}
    STATE_LOCK();g_state.ble_on=false;g_state.ble_count=0;g_state.ble_lines[0]=0;seen_count=0;memset(seen_addresses,0,sizeof(seen_addresses));STATE_UNLOCK();
}
static void scan_wifi(void){
    if(!start_wifi()) {
        return;
    }
    state_radio_status("Passive Wi-Fi scan...");
    wifi_scan_config_t cfg={.scan_type=WIFI_SCAN_TYPE_PASSIVE,.scan_time.passive=120,.show_hidden=false};
    if(esp_wifi_scan_start(&cfg,true)!=ESP_OK){state_radio_status("Wi-Fi scan failed");return;}
    uint16_t total=0,n=20;wifi_ap_record_t ap[20];esp_wifi_scan_get_ap_num(&total);
    if(esp_wifi_scan_get_ap_records(&n,ap)!=ESP_OK){state_radio_status("Wi-Fi results unavailable");return;}
    char text[320]={0};for(unsigned i=0;i<n&&i<5;i++){
        char name[23];size_t k=0;for(;k<22&&ap[i].ssid[k];k++)name[k]=(ap[i].ssid[k]>=32&&ap[i].ssid[k]<127)?ap[i].ssid[k]:'?';name[k]=0;
        size_t at=strlen(text);snprintf(text+at,sizeof(text)-at,"%s  ch%u %d\n",name,ap[i].primary,ap[i].rssi);
    }
    STATE_LOCK();g_state.wifi_count=total;snprintf(g_state.wifi_lines,sizeof(g_state.wifi_lines),"%s",text);STATE_UNLOCK();
    state_radio_status("Wi-Fi scan complete (dBm)");
}
static void work(void *arg){
    (void)arg;radio_cmd_t cmd;int64_t next_notify=0;
    for(;;){
        if(xQueueReceive(queue,&cmd,pdMS_TO_TICKS(100))==pdTRUE){
            switch(cmd){
            case RADIO_WIFI_SCAN:scan_wifi();break;
            case RADIO_WIFI_TIME:{
                if(!CONFIG_RW_WIFI_SSID[0]){state_radio_status("Set Wi-Fi in menuconfig first");break;}
                if(!start_wifi()) {
                    break;
                }
                wifi_config_t c={0};
                size_t ssid_len=strlen(CONFIG_RW_WIFI_SSID), pass_len=strlen(CONFIG_RW_WIFI_PASSWORD);
                if(ssid_len>sizeof(c.sta.ssid)||pass_len>sizeof(c.sta.password)){
                    state_radio_status("SSID / password too long");break;
                }
                memcpy(c.sta.ssid,CONFIG_RW_WIFI_SSID,ssid_len);
                memcpy(c.sta.password,CONFIG_RW_WIFI_PASSWORD,pass_len);
                c.sta.threshold.authmode=WIFI_AUTH_WPA2_PSK;c.sta.pmf_cfg.capable=true;
                if(esp_wifi_set_config(WIFI_IF_STA,&c)!=ESP_OK||esp_wifi_connect()!=ESP_OK) {
                    state_radio_status("Wi-Fi connect failed");
                } else {
                    state_radio_status("Joining configured network...");
                }
                break;}
            case RADIO_WIFI_OFF:stop_wifi();state_radio_status("Wi-Fi off");break;
            case RADIO_BLE_ON:
                if(!ble_ready){state_radio_status("BLE not ready");break;}
                if(connection==BLE_HS_CONN_HANDLE_NONE){uint32_t pin=100000+esp_random()%900000;
                    STATE_LOCK();g_state.pairing_pin=pin;STATE_UNLOCK();}
                ble_allowed=true;ble_deadline=esp_timer_get_time()+300000000LL;
                STATE_LOCK();g_state.ble_on=true;STATE_UNLOCK();advertise();state_radio_status("BLE session: 5 minutes");break;
            case RADIO_BLE_SCAN:{
                if(!ble_ready||!ble_allowed){state_radio_status("Enable BLE session first");break;}
                ble_gap_disc_cancel();STATE_LOCK();seen_count=0;g_state.ble_count=0;g_state.ble_lines[0]=0;STATE_UNLOCK();
                struct ble_gap_disc_params p={.itvl=160,.window=80,.passive=1,.filter_duplicates=1};
                int rc=ble_gap_disc(own_address_type,8000,&p,gap_event,NULL);
                state_radio_status(rc==0?"Passive BLE scan: 8 seconds":"BLE scan failed");break;}
            case RADIO_BLE_OFF:stop_ble();state_radio_status("BLE off");break;
            case RADIO_ERASE_BONDS:
                stop_ble();vTaskDelay(pdMS_TO_TICKS(150));
                if(ble_store_clear()==0) {
                    state_radio_status("Bonds cleared; forget on phone too");
                } else {
                    state_radio_status("Bond clear failed");
                }
                break;
            case RADIO_CSI_TOGGLE:
#ifdef CONFIG_RW_CSI_EXPERIMENT
            {watch_state_t s;state_read(&s);if(!s.wifi_connected){state_radio_status("Connect to your AP for CSI");break;}
                bool enabled=!csi_on;
                if(enabled){
                    wifi_csi_config_t cc={.lltf_en=true,.htltf_en=true,.stbc_htltf2_en=true,.ltf_merge_en=true,.channel_filter_en=true};
                    if(esp_wifi_set_csi_config(&cc)!=ESP_OK||esp_wifi_set_csi_rx_cb(receive_csi,NULL)!=ESP_OK){state_radio_status("CSI configuration failed");break;}
                }
                if(esp_wifi_set_csi(enabled)==ESP_OK){csi_on=enabled;state_radio_status(enabled?"CSI on: needs incoming packets":"CSI off");}break;}
#else
                state_radio_status("CSI disabled in menuconfig");break;
#endif
            case RADIO_ALL_OFF:stop_wifi();stop_ble();state_radio_status("Radios off");break;
            }
        }
        int64_t now=esp_timer_get_time();
        if(wifi_started&&now>wifi_deadline)stop_wifi();
        if(ble_allowed&&now>ble_deadline)stop_ble();
        if(ble_allowed)advertise();
        if(now>=next_notify){next_notify=now+1000000;
            uint16_t c=connection;
            if(c!=BLE_HS_CONN_HANDLE_NONE&&subscribed&&secured(c)){
                uint8_t data[20];telemetry(data);struct os_mbuf *m=ble_hs_mbuf_from_flat(data,20);
                if(m)ble_gatts_notify_custom(c,telemetry_handle,m);
            }
        }
    }
}
esp_err_t radio_init(void){
    ESP_ERROR_CHECK(esp_netif_init());ESP_ERROR_CHECK(esp_event_loop_create_default());
    if(!esp_netif_create_default_wifi_sta())return ESP_ERR_NO_MEM;
    wifi_init_config_t c=WIFI_INIT_CONFIG_DEFAULT();ESP_ERROR_CHECK(esp_wifi_init(&c));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_MIN_MODEM));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT,ESP_EVENT_ANY_ID,wifi_event,NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT,IP_EVENT_STA_GOT_IP,wifi_event,NULL));
    ESP_ERROR_CHECK(nimble_port_init());ble_hs_cfg.sync_cb=ble_sync;ble_hs_cfg.reset_cb=ble_reset;
    ble_hs_cfg.sm_io_cap=BLE_HS_IO_DISPLAY_ONLY;ble_hs_cfg.sm_bonding=1;ble_hs_cfg.sm_mitm=1;ble_hs_cfg.sm_sc=1;
    ble_hs_cfg.sm_our_key_dist=BLE_SM_PAIR_KEY_DIST_ENC|BLE_SM_PAIR_KEY_DIST_ID;
    ble_hs_cfg.sm_their_key_dist=BLE_SM_PAIR_KEY_DIST_ENC|BLE_SM_PAIR_KEY_DIST_ID;
    ble_hs_cfg.store_status_cb=ble_store_util_status_rr;
    ble_svc_gap_init();ble_svc_gatt_init();
    if(ble_gatts_count_cfg(services)!=0||ble_gatts_add_svcs(services)!=0)return ESP_FAIL;
    ble_svc_gap_device_name_set("Luxor");ble_store_config_init();
    nimble_port_freertos_init(ble_host);
    queue=xQueueCreate(8,sizeof(radio_cmd_t));if(!queue)return ESP_ERR_NO_MEM;
    if(xTaskCreate(work,"radio",7168,NULL,4,NULL)!=pdPASS)return ESP_ERR_NO_MEM;
    return ESP_OK;
}
void radio_command(radio_cmd_t c){if(queue&&xQueueSend(queue,&c,0)!=pdTRUE)state_radio_status("Radio busy; try again");}
