#include "board.h"
#include "luxconf.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <sys/time.h>
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "driver/spi_master.h"
#include "driver/ledc.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_spd2010.h"
#include "esp_rom_sys.h"
#include "esp_log.h"

static i2c_master_bus_handle_t bus;
static i2c_master_dev_handle_t expander,imu,rtc,touch_dev;
static esp_lcd_panel_handle_t panel;
static bool touch_ready;
static adc_oneshot_unit_handle_t adc;
static adc_cali_handle_t adc_cal;
static lv_disp_drv_t disp_drv;
static lv_disp_draw_buf_t draw_buf;
static bool adc_cal_ok, rtc_ok;
static uint8_t imu_axis_map = LUX_IMU_SWAP_XY | LUX_IMU_INV_Y;
static bool backlight_off=true,touch_suppress,touch_wake,dark_tap_capture;
static bool raw_touch_down,ignore_current_tap;
static int16_t tap_start_x,tap_start_y,tap_last_x,tap_last_y;
static int64_t tap_down_us;
#define TAP_QUEUE_LEN 8
typedef struct { int16_t x,y; } tap_point_t;
static tap_point_t tap_queue[TAP_QUEUE_LEN];
static unsigned tap_head,tap_tail;
bool board_take_touch_wake(void){bool v=touch_wake;touch_wake=false;return v;}

/* Decides, at the moment a finger lands, whether this touch may count
 * towards the double-tap / triple-tap gestures.  The UI answers "only if it
 * landed on empty background", so tapping the same button twice is two
 * button presses -- never "go home". */
static bool (*tap_filter)(int16_t x,int16_t y);
void board_set_tap_filter(bool (*fn)(int16_t x,int16_t y)){tap_filter=fn;}
void board_set_dark_tap_capture(bool enable){dark_tap_capture=enable;}
static void queue_tap(int16_t x,int16_t y){
    unsigned next=(tap_head+1U)%TAP_QUEUE_LEN;
    if(next==tap_tail)tap_tail=(tap_tail+1U)%TAP_QUEUE_LEN;
    tap_queue[tap_head]=(tap_point_t){x,y};tap_head=next;
}
bool board_take_tap(int16_t *x,int16_t *y){
    if(tap_tail==tap_head)return false;
    tap_point_t p=tap_queue[tap_tail];
    tap_tail=(tap_tail+1U)%TAP_QUEUE_LEN;
    if(x) {
        *x=p.x;
    }
    if(y) {
        *y=p.y;
    }
    return true;
}
static const char *TAG="board";
static esp_err_t wr(i2c_master_dev_handle_t d,uint8_t r,uint8_t v){uint8_t b[]={r,v};return i2c_master_transmit(d,b,2,100);}
static esp_err_t rd(i2c_master_dev_handle_t d,uint8_t r,void *p,size_t n){return i2c_master_transmit_receive(d,&r,1,p,n,100);}
static esp_err_t add(uint8_t address,i2c_master_dev_handle_t *d){
    i2c_device_config_t c={.dev_addr_length=I2C_ADDR_BIT_LEN_7,.device_address=address,.scl_speed_hz=400000};
    return i2c_master_bus_add_device(bus,&c,d);
}
void board_power_hold(void){
    gpio_hold_dis(GPIO_NUM_7);
    gpio_config_t c={.pin_bit_mask=1ULL<<7,.mode=GPIO_MODE_OUTPUT};
    ESP_ERROR_CHECK(gpio_config(&c));gpio_set_level(GPIO_NUM_7,1);
    c.pin_bit_mask=(1ULL<<6)|(1ULL<<0);c.mode=GPIO_MODE_INPUT;c.pull_up_en=GPIO_PULLUP_ENABLE;
    ESP_ERROR_CHECK(gpio_config(&c));
}
bool board_pwr_pressed(void){return gpio_get_level(GPIO_NUM_6)==0;}
bool board_boot_pressed(void){return gpio_get_level(GPIO_NUM_0)==0;}
void board_shutdown(void){board_backlight(0);if(imu)wr(imu,0x08,0);gpio_set_level(GPIO_NUM_7,0);}
void board_backlight(unsigned p){
    if(p>100)p=100;
    backlight_off=(p==0);
    /* Perceptual curve: a linear duty cycle makes every setting above ~30%
       look identical and everything below it look off.  A gamma of 2.2 with a
       small floor spreads the slider evenly across what the eye actually sees. */
    unsigned duty=0;
    if(p){
        float f=powf((float)p/100.0f,2.2f);
        duty=(unsigned)(f*1023.0f+0.5f);
        if(duty<12)duty=12;            /* keep the lowest step visible */
    }
    ledc_set_duty(LEDC_LOW_SPEED_MODE,LEDC_CHANNEL_0,duty);
    ledc_update_duty(LEDC_LOW_SPEED_MODE,LEDC_CHANNEL_0);
}
esp_err_t board_init(void){
    ESP_LOGI(TAG,"board_init: I2C, port expander, backlight, battery sense");
    i2c_master_bus_config_t c={.i2c_port=I2C_NUM_0,.sda_io_num=GPIO_NUM_11,.scl_io_num=GPIO_NUM_10,
        .clk_source=I2C_CLK_SRC_DEFAULT,.glitch_ignore_cnt=7,.flags.enable_internal_pullup=true};
    esp_err_t e=i2c_new_master_bus(&c,&bus);
    if(e!=ESP_OK){ESP_LOGE(TAG,"I2C bus failed: %s",esp_err_to_name(e));return e;} /* genuinely fatal */

    /* The port expander drives LCD reset, so a failure here is serious, but it
       is still reported rather than aborted: a warm panel often survives it. */
    if(add(0x20,&expander)==ESP_OK){
        if(wr(expander,0x01,0x07)!=ESP_OK||wr(expander,0x03,0xF8)!=ESP_OK){
            ESP_LOGW(TAG,"port expander did not accept its configuration");
        }else{
            wr(expander,0x01,0x04);vTaskDelay(pdMS_TO_TICKS(50));   /* assert LCD + TP reset */
            wr(expander,0x01,0x07);vTaskDelay(pdMS_TO_TICKS(150));  /* release              */
        }
    }else{
        ESP_LOGW(TAG,"port expander absent at 0x20; panel reset may be unreliable");
    }

    if(add(0x51,&rtc)==ESP_OK)rtc_ok=true;
    else ESP_LOGW(TAG,"RTC absent at 0x51; time will come from Wi-Fi or Settings");

    ledc_timer_config_t t={.speed_mode=LEDC_LOW_SPEED_MODE,.duty_resolution=LEDC_TIMER_10_BIT,
        .timer_num=LEDC_TIMER_0,.freq_hz=5000,.clk_cfg=LEDC_AUTO_CLK};
    if(ledc_timer_config(&t)==ESP_OK){
        ledc_channel_config_t l={.gpio_num=5,.speed_mode=LEDC_LOW_SPEED_MODE,.channel=LEDC_CHANNEL_0,
            .timer_sel=LEDC_TIMER_0,.duty=0,.hpoint=0};
        if(ledc_channel_config(&l)!=ESP_OK)ESP_LOGE(TAG,"backlight channel failed");
        else ESP_LOGI(TAG,"backlight PWM ready on GPIO5");
    }else ESP_LOGE(TAG,"backlight timer failed");

    adc_oneshot_unit_init_cfg_t au={.unit_id=ADC_UNIT_1};
    if(adc_oneshot_new_unit(&au,&adc)==ESP_OK){
        adc_oneshot_chan_cfg_t ac={.atten=ADC_ATTEN_DB_12,.bitwidth=ADC_BITWIDTH_DEFAULT};
        if(adc_oneshot_config_channel(adc,ADC_CHANNEL_7,&ac)==ESP_OK){ // GPIO8
            adc_cali_curve_fitting_config_t cal={.unit_id=ADC_UNIT_1,.chan=ADC_CHANNEL_7,
                .atten=ADC_ATTEN_DB_12,.bitwidth=ADC_BITWIDTH_DEFAULT};
            adc_cal_ok=adc_cali_create_scheme_curve_fitting(&cal,&adc_cal)==ESP_OK;
        }
    }
    if(!adc_cal_ok)ESP_LOGW(TAG,"battery ADC unavailable; the gauge will read --");
    return ESP_OK;
}
bool board_rtc_present(void){return rtc_ok;}
bool board_touch_ok(void){return touch_ready;}
float board_battery_voltage(void){
    if(!adc_cal_ok)return -1; // Do not invent a voltage when calibration unavailable.
    int total=0;
    for(int i=0;i<16;i++){
        int raw=0,mv=0;
        if(adc_oneshot_read(adc,ADC_CHANNEL_7,&raw)!=ESP_OK)return -1;
        if(adc_cali_raw_to_voltage(adc_cal,raw,&mv)!=ESP_OK)return -1;
        total+=mv;
    }
    return (total/16.0f)*0.003f*(CONFIG_RW_ADC_SCALE_PERMILLE/1000.0f);
}
/* Percentage, smoothing and the "unknown" rule live in battery.c (host-tested);
   this file only measures volts. */
static bool flush_done(esp_lcd_panel_io_handle_t io,esp_lcd_panel_io_event_data_t *ed,void *ctx){
    (void)io;(void)ed;lv_disp_flush_ready((lv_disp_drv_t*)ctx);return false;
}
static void flush_cb(lv_disp_drv_t *d,const lv_area_t *a,lv_color_t *p){
    size_t n=(a->x2-a->x1+1)*(a->y2-a->y1+1);
#if !LV_COLOR_16_SWAP
    for(size_t i=0;i<n;i++)p[i].full=__builtin_bswap16(p[i].full);
#else
    (void)n;
#endif
    esp_err_t e=esp_lcd_panel_draw_bitmap(panel,a->x1,a->y1,a->x2+1,a->y2+1,p);
    if(e!=ESP_OK){ESP_LOGE(TAG,"Display transfer: %s",esp_err_to_name(e));lv_disp_flush_ready(d);}
}
static void round_cb(lv_disp_drv_t *d,lv_area_t *a){
    (void)d;
    // SPD2010 requires x_start and exclusive x_end to be multiples of four.
    // LVGL uses inclusive coordinates, hence x2 must end at 4*n + 3.
    a->x1 &= ~3;
    a->x2 |= 3;
    a->y1 &= ~1;
    a->y2 |= 1;
    if (a->x2 > 411) a->x2 = 411;
    if (a->y2 > 411) a->y2 = 411;
}
static esp_err_t touch_tx(const uint8_t *data,size_t len){
    if(!touch_dev||!data||len==0)return ESP_ERR_INVALID_ARG;
    return i2c_master_transmit(touch_dev,data,len,100);
}
static esp_err_t touch_rx(uint8_t *data,size_t len){
    if(!touch_dev||!data||len==0)return ESP_ERR_INVALID_ARG;
    return i2c_master_receive(touch_dev,data,len,100);
}
static esp_err_t touch_cmd4(uint8_t a,uint8_t b,uint8_t c,uint8_t d){
    uint8_t v[4]={a,b,c,d};
    esp_err_t e=touch_tx(v,sizeof(v));
    esp_rom_delay_us(200);
    return e;
}
static esp_err_t touch_probe(void){
    uint8_t cmd[2]={0x26,0x00};
    uint8_t fw[18]={0};
    esp_err_t e=touch_tx(cmd,sizeof(cmd));
    if(e!=ESP_OK)return e;
    esp_rom_delay_us(200);
    e=touch_rx(fw,sizeof(fw));
    if(e==ESP_OK){
        ESP_LOGI(TAG,"touch: SPD2010 responded at 0x53 (fw bytes %02x %02x %02x %02x)",fw[0],fw[1],fw[2],fw[3]);
    }
    return e;
}
static esp_err_t touch_drain_remaining(uint16_t n){
    uint8_t buf[32];
    unsigned loops=0;
    while(n&&loops++<8){
        size_t take=n>sizeof(buf)?sizeof(buf):n;
        uint8_t cmd[2]={0x00,0x03};
        esp_err_t e=touch_tx(cmd,sizeof(cmd));
        if(e!=ESP_OK)return e;
        esp_rom_delay_us(200);
        e=touch_rx(buf,take);
        if(e!=ESP_OK)return e;
        esp_rom_delay_us(200);
        n=(uint16_t)(n-take);
    }
    return n?ESP_ERR_INVALID_SIZE:ESP_OK;
}
static esp_err_t touch_poll(bool *pressed,uint16_t *x,uint16_t *y,uint8_t *strength){
    if(!pressed||!x||!y||!strength)return ESP_ERR_INVALID_ARG;
    *pressed=false;
    if(!touch_ready||!touch_dev)return ESP_ERR_INVALID_STATE;

    uint8_t cmd[2]={0x20,0x00};
    uint8_t st[4]={0};
    esp_err_t e=touch_tx(cmd,sizeof(cmd));
    if(e!=ESP_OK)return e;
    esp_rom_delay_us(200);
    e=touch_rx(st,sizeof(st));
    if(e!=ESP_OK)return e;
    esp_rom_delay_us(200);

    bool pt_exist=(st[0]&0x01)!=0;
    bool gesture=(st[0]&0x02)!=0;
    bool aux=(st[0]&0x08)!=0;
    bool cpu_run=(st[1]&0x08)!=0;
    bool tic_in_cpu=(st[1]&0x20)!=0;
    bool tic_in_bios=(st[1]&0x40)!=0;
    uint16_t read_len=(uint16_t)st[2]|((uint16_t)st[3]<<8);

    if(tic_in_bios){
        e=touch_cmd4(0x02,0x00,0x01,0x00);
        if(e==ESP_OK)e=touch_cmd4(0x04,0x00,0x01,0x00);
        return e;
    }
    if(tic_in_cpu){
        e=touch_cmd4(0x50,0x00,0x00,0x00);
        if(e==ESP_OK)e=touch_cmd4(0x46,0x00,0x00,0x00);
        if(e==ESP_OK)e=touch_cmd4(0x02,0x00,0x01,0x00);
        return e;
    }
    if(cpu_run&&read_len==0)return touch_cmd4(0x02,0x00,0x01,0x00);

    if(pt_exist||gesture){
        if(read_len<4||read_len>64){
            ESP_LOGW(TAG,"touch: ignoring invalid packet length %u",(unsigned)read_len);
            return ESP_ERR_INVALID_SIZE;
        }
        uint8_t data[64]={0};
        uint8_t hdp_cmd[2]={0x00,0x03};
        e=touch_tx(hdp_cmd,sizeof(hdp_cmd));
        if(e!=ESP_OK)return e;
        esp_rom_delay_us(200);
        e=touch_rx(data,read_len);
        if(e!=ESP_OK)return e;
        esp_rom_delay_us(200);

        if(pt_exist&&read_len>=10&&data[4]<=0x0A){
            uint8_t count=(uint8_t)((read_len-4)/6);
            if(count){
                uint16_t px=(uint16_t)(((data[7]&0xF0)<<4)|data[5]);
                uint16_t py=(uint16_t)(((data[7]&0x0F)<<8)|data[6]);
                if(px<412&&py<412){
                    *x=px;*y=py;*strength=data[8];*pressed=true;
                }
            }
        }

        uint8_t hs_cmd[2]={0xFC,0x02};
        uint8_t hs[8]={0};
        e=touch_tx(hs_cmd,sizeof(hs_cmd));
        if(e!=ESP_OK)return e;
        esp_rom_delay_us(200);
        e=touch_rx(hs,sizeof(hs));
        if(e!=ESP_OK)return e;
        esp_rom_delay_us(200);
        uint8_t hdp_status=hs[5];
        uint16_t next_len=(uint16_t)hs[2]|((uint16_t)hs[3]<<8);
        if(hdp_status==0x82){
            e=touch_cmd4(0x02,0x00,0x01,0x00);
        }else if(hdp_status==0x00&&next_len){
            e=touch_drain_remaining(next_len);
        }
        return e;
    }
    if(cpu_run&&aux)return touch_cmd4(0x02,0x00,0x01,0x00);
    return ESP_OK;
}
static void read_touch(lv_indev_drv_t *d,lv_indev_data_t *o){
    (void)d;
    static int64_t last_poll_us;
    static bool cached_have;
    static uint16_t cached_x,cached_y;
    static uint8_t cached_strength;
    int64_t now=esp_timer_get_time();
    if(now-last_poll_us>=5000){
        last_poll_us=now;
        bool have=false;uint16_t x=0,y=0;uint8_t strength=0;
        esp_err_t e=touch_poll(&have,&x,&y,&strength);
        if(e==ESP_OK){
            cached_have=have;cached_x=x;cached_y=y;cached_strength=strength;
        }else{
            static int64_t last_err_us;
            cached_have=false;
            if(now-last_err_us>2000000){
                last_err_us=now;
                ESP_LOGW(TAG,"touch poll: %s",esp_err_to_name(e));
            }
        }
    }
    (void)cached_strength;
    o->state=LV_INDEV_STATE_REL;
    bool have=cached_have;
    if(have){
        int16_t x=(int16_t)cached_x,y=(int16_t)cached_y;
        if(!raw_touch_down){
            raw_touch_down=true;tap_start_x=tap_last_x=x;tap_start_y=tap_last_y=y;tap_down_us=esp_timer_get_time();
            if(backlight_off) ignore_current_tap=!dark_tap_capture;   /* 1 tap only wakes */
            else ignore_current_tap=tap_filter&&!tap_filter(x,y);      /* taps on controls never count */
        }else{tap_last_x=x;tap_last_y=y;}
        if(backlight_off){
            touch_suppress=true;
            if(!dark_tap_capture)touch_wake=true;
        }
        if(touch_suppress)return;
        o->point.x=x;o->point.y=y;o->state=LV_INDEV_STATE_PR;
    }else{
        if(raw_touch_down){
            int64_t held=esp_timer_get_time()-tap_down_us;
            int dx=tap_last_x-tap_start_x,dy=tap_last_y-tap_start_y;
            if(!ignore_current_tap&&held>25000&&held<500000&&dx*dx+dy*dy<28*28)queue_tap(tap_last_x,tap_last_y);
        }
        raw_touch_down=false;ignore_current_tap=false;touch_suppress=false;
    }
}
static void tick(void *arg){(void)arg;lv_tick_inc(2);}
esp_err_t board_display_init(void){
    ESP_LOGI(TAG,"display: begin SPD2010 412x412 QSPI init");
    _Static_assert(sizeof(lv_color_t)==2,"Use LVGL 16-bit color");
    lv_init();
    spi_bus_config_t s=SPD2010_PANEL_BUS_QSPI_CONFIG(40,46,45,42,41,412*40*2);
    ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST,&s,SPI_DMA_CH_AUTO));
    ESP_LOGI(TAG,"display: QSPI bus ready (SCK40 D0=46 D1=45 D2=42 D3=41 CS=21)");
    esp_lcd_panel_io_handle_t io;
    esp_lcd_panel_io_spi_config_t ic=SPD2010_PANEL_IO_QSPI_CONFIG(21,flush_done,&disp_drv);
    ic.pclk_hz=40000000;ic.trans_queue_depth=2;
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)SPI2_HOST,&ic,&io));
    spd2010_vendor_config_t v={.flags.use_qspi_interface=1};
    esp_lcd_panel_dev_config_t pc={.reset_gpio_num=-1,.rgb_ele_order=LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel=16,.data_endian=LCD_RGB_DATA_ENDIAN_BIG,.vendor_config=&v};
    ESP_ERROR_CHECK(esp_lcd_new_panel_spd2010(io,&pc,&panel));
    ESP_ERROR_CHECK(esp_lcd_panel_reset(panel));
    ESP_LOGI(TAG,"display: panel reset complete");
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel));
    ESP_LOGI(TAG,"display: panel init complete");
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel,true));
    ESP_LOGI(TAG,"display: panel display ON command sent");
    /* ESP-IDF 5.5's esp_lcd I2C RX path rejects the SPD2010 touch driver's
       zero-command read transactions.  Use the native I2C master API directly
       for the SPD2010 touch protocol instead.  This also keeps touch failure
       non-fatal so a working LCD can still show Luxor. */
    i2c_device_config_t touch_cfg={.dev_addr_length=I2C_ADDR_BIT_LEN_7,.device_address=0x53,.scl_speed_hz=100000};
    esp_err_t touch_err=i2c_master_bus_add_device(bus,&touch_cfg,&touch_dev);
    if(touch_err==ESP_OK)touch_err=touch_probe();
    touch_ready=(touch_err==ESP_OK);
    if(touch_ready)ESP_LOGI(TAG,"touch: direct SPD2010 I2C driver ready");
    else ESP_LOGW(TAG,"touch unavailable (%s); continuing with display",esp_err_to_name(touch_err));
    lv_color_t *b1=heap_caps_malloc(412*40*2,MALLOC_CAP_DMA|MALLOC_CAP_INTERNAL);
    lv_color_t *b2=heap_caps_malloc(412*40*2,MALLOC_CAP_DMA|MALLOC_CAP_INTERNAL);
    if(!b1||!b2)return ESP_ERR_NO_MEM;
    lv_disp_draw_buf_init(&draw_buf,b1,b2,412*40);
    lv_disp_drv_init(&disp_drv);disp_drv.hor_res=412;disp_drv.ver_res=412;
    disp_drv.flush_cb=flush_cb;disp_drv.rounder_cb=round_cb;disp_drv.draw_buf=&draw_buf;
    if(!lv_disp_drv_register(&disp_drv))return ESP_FAIL;
    if(touch_ready){
        static lv_indev_drv_t in;
        lv_indev_drv_init(&in);
        in.type=LV_INDEV_TYPE_POINTER;
        in.read_cb=read_touch;
        lv_indev_drv_register(&in);
    }
    const esp_timer_create_args_t ta={.callback=tick,.name="lv_tick"};esp_timer_handle_t timer;
    ESP_ERROR_CHECK(esp_timer_create(&ta,&timer));ESP_ERROR_CHECK(esp_timer_start_periodic(timer,2000));
    board_backlight(80);
    ESP_LOGI(TAG,"display: LVGL registered; backlight forced to 80%% for boot");
    return ESP_OK;
}
esp_err_t board_imu_init(void){
    uint8_t id=0;
    for(int a=0x6A;a<=0x6B;a++){
        if(add(a,&imu)!=ESP_OK)continue;
        if(rd(imu,0,&id,1)==ESP_OK&&id==0x05)break;
        i2c_master_bus_rm_device(imu);imu=NULL;
    }
    if(!imu)return ESP_ERR_NOT_FOUND;
    /* No ESP_ERROR_CHECK here: a sensor that NAKs one register must not take
       the whole watch down with it. */
    esp_err_t e=ESP_OK;
    e|=wr(imu,0x08,0x00);
    e|=wr(imu,0x02,0x40); // auto increment, little endian, oscillator on
    e|=wr(imu,0x03,0x27); // +/-8 g,      ~62.5 Hz in 6DOF mode
    e|=wr(imu,0x04,0x57); // +/-512 dps, ~62.5 Hz in 6DOF mode
    e|=wr(imu,0x06,0x77); // both low-pass filters on, about 13.4% of ODR
    e|=wr(imu,0x08,0x03); // accelerometer + gyroscope enabled
    vTaskDelay(pdMS_TO_TICKS(200));
    if(e!=ESP_OK){ESP_LOGW(TAG,"IMU configured with errors; motion may be degraded");return ESP_FAIL;}
    ESP_LOGI(TAG,"IMU ready (QMI8658, axis map 0x%X)",imu_axis_map);
    return ESP_OK;
}
void board_imu_set_enabled(bool enabled){if(imu)wr(imu,0x08,enabled?0x03:0x00);}
void board_imu_set_map(uint8_t map){imu_axis_map=map&LUX_IMU_MAP_MASK;}
uint8_t board_imu_get_map(void){return imu_axis_map;}

/* Swap, then invert.  Eight of the sixteen bit patterns are distinct rotations
   and the rest are mirrors; all of them are reachable from the Motion screen,
   so no soldering orientation can leave the watch stuck. */
static void map_vec(const float in[3],float out[3],uint8_t map){
    float t0=in[0],t1=in[1],t2=in[2];
    if(map&LUX_IMU_SWAP_XY){float tmp=t0;t0=t1;t1=tmp;}
    out[0]=(map&LUX_IMU_INV_X)?-t0:t0;
    out[1]=(map&LUX_IMU_INV_Y)?-t1:t1;
    out[2]=(map&LUX_IMU_INV_Z)?-t2:t2;
}
static float map_det(uint8_t map){
    float d=(map&LUX_IMU_SWAP_XY)?-1.0f:1.0f;
    if(map&LUX_IMU_INV_X)d=-d;
    if(map&LUX_IMU_INV_Y)d=-d;
    if(map&LUX_IMU_INV_Z)d=-d;
    return d;
}
bool board_imu_read(imu_sample_t *out,float raw_out[6]){
    if(!imu)return false;
    uint8_t status=0,b[12];
    if(rd(imu,0x2e,&status,1)!=ESP_OK||(status&3)!=3)return false;
    if(rd(imu,0x35,b,sizeof(b))!=ESP_OK)return false;
    float raw[6];
    for(int i=0;i<6;i++){
        int16_t v=(int16_t)((uint16_t)b[2*i]|((uint16_t)b[2*i+1]<<8));
        raw[i]=v/(i<3?4096.0f:64.0f);   // +/-8 g and +/-512 dps full scale
    }
    if(raw_out)memcpy(raw_out,raw,sizeof(raw));
    if(out){
        float a[3],g[3];
        map_vec(raw,a,imu_axis_map);
        map_vec(raw+3,g,imu_axis_map);
        /* Angular rate is a pseudovector: a mirrored map must also flip its
           sense, or the yaw reading would spin the wrong way. */
        float det=map_det(imu_axis_map);
        out->ax=a[0];out->ay=a[1];out->az=a[2];
        out->gx=g[0]*det;out->gy=g[1]*det;out->gz=g[2]*det;
    }
    return true;
}
static int unbcd(uint8_t v){return 10*(v>>4)+(v&15);}
static uint8_t bcd(int v){return ((v/10)<<4)|(v%10);}
// Howard Hinnant-style civil date conversion, independent of local TZ/timegm availability.
static int64_t days_from_civil(int y,unsigned m,unsigned d){
    y-=m<=2;int era=(y>=0?y:y-399)/400;unsigned yo=(unsigned)(y-era*400);
    unsigned doy=(153*(m>2?m-3:m+9)+2)/5+d-1;unsigned doe=yo*365+yo/4-yo/100+doy;
    return (int64_t)era*146097+doe-719468;
}
bool board_rtc_read(time_t *utc){
    if(!rtc||!utc)return false;
    uint8_t b[7];if(rd(rtc,4,b,7)!=ESP_OK||(b[0]&0x80))return false;
    int sec=unbcd(b[0]&0x7f),min=unbcd(b[1]&0x7f),hour=unbcd(b[2]&0x3f);
    int day=unbcd(b[3]&0x3f),month=unbcd(b[5]&0x1f),year=2000+unbcd(b[6]);
    if(sec>59||min>59||hour>23||day<1||day>31||month<1||month>12||year<2024)return false;
    *utc=(time_t)(days_from_civil(year,month,day)*86400+hour*3600+min*60+sec);return true;
}
bool board_rtc_write(time_t utc){
    if(!rtc)return false;
    struct tm t;gmtime_r(&utc,&t);if(t.tm_year<124||t.tm_year>199)return false;
    uint8_t b[]={4,bcd(t.tm_sec),bcd(t.tm_min),bcd(t.tm_hour),bcd(t.tm_mday),bcd(t.tm_wday),bcd(t.tm_mon+1),bcd(t.tm_year-100)};
    if(wr(rtc,0,0x20)!=ESP_OK)return false; // stop RTC while replacing date/time
    esp_err_t e=i2c_master_transmit(rtc,b,sizeof(b),100);esp_err_t restart=wr(rtc,0,0);
    return e==ESP_OK&&restart==ESP_OK;
}
