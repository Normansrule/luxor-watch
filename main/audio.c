#include "audio.h"
#include "state.h"
#include <math.h>
#include <stdint.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/i2s_std.h"
#include "esp_log.h"

#define RATE 16000
#define FRAMES 256
#define NOTE_END {0,0}

typedef struct { uint16_t hz; uint16_t ms; } note_t;
typedef enum { ACMD_CHIME=1, ACMD_PLAY, ACMD_STOP } audio_cmd_type_t;
typedef struct { uint8_t type; uint8_t track; uint32_t generation; } audio_cmd_t;

/* Original short synth pieces supplied with LUXOR. No copyrighted audio assets. */
static const note_t track_pulse[] = {
    {440,150},{0,55},{554,150},{0,55},{659,180},{0,70},{554,150},{0,55},
    {494,150},{0,55},{622,150},{0,55},{740,220},{0,110},{659,160},{554,160},
    {494,180},{0,80},{440,260},{0,180}, NOTE_END
};
static const note_t track_neon[] = {
    {330,120},{392,120},{494,120},{659,240},{0,70},{587,120},{494,120},{392,220},{0,80},
    {349,120},{440,120},{523,120},{698,240},{0,70},{659,120},{523,120},{440,260},{0,180}, NOTE_END
};
static const note_t track_orbit[] = {
    {262,180},{392,180},{523,240},{0,80},{494,180},{392,180},{330,260},{0,80},
    {294,180},{440,180},{587,240},{0,80},{523,180},{440,180},{349,300},{0,220}, NOTE_END
};
static const note_t track_arcade[] = {
    {784,90},{659,90},{523,90},{659,90},{784,180},{0,60},{880,90},{784,90},{659,180},{0,60},
    {523,90},{587,90},{659,90},{784,90},{659,180},{0,60},{494,90},{523,90},{587,220},{0,180}, NOTE_END
};
static const note_t *tracks[LUXOR_MUSIC_TRACKS] = {track_pulse,track_neon,track_orbit,track_arcade};
static const char *track_names[LUXOR_MUSIC_TRACKS] = {"Pulse Grid","Neon Run","Slow Orbit","Arcade Spark"};

static i2s_chan_handle_t tx,rx;
static QueueHandle_t commands;
static bool ready;
static volatile bool mic_requested;
static volatile bool music_active;
static volatile uint8_t selected_track;
static volatile unsigned output_volume=42;
static volatile uint32_t generation=1;

static bool generation_ok(uint32_t g){ return g==generation; }

static void write_silence(void){
    int16_t samples[FRAMES*2]={0};
    for(int i=0;i<4;i++){size_t done=0;i2s_channel_write(tx,samples,sizeof(samples),&done,80);}
}

static bool play_tone(float hz,int ms,uint32_t gen){
    int16_t samples[FRAMES*2];
    const int total=RATE*ms/1000;
    if(hz<=1.0f){
        int waited=0;
        while(waited<ms && generation_ok(gen)){int d=(ms-waited)>20?20:(ms-waited);vTaskDelay(pdMS_TO_TICKS(d));waited+=d;}
        return generation_ok(gen);
    }
    unsigned vol=output_volume;if(vol>100)vol=100;
    float amp=900.0f + (float)vol*42.0f; /* conservative speaker amplitude */
    for(int at=0;at<total && generation_ok(gen);at+=FRAMES){
        int count=total-at;if(count>FRAMES)count=FRAMES;
        for(int j=0;j<count;j++){
            int i=at+j;
            float fade=fminf(1.f,fminf(i/130.f,(total-1-i)/130.f));
            int16_t v=(int16_t)(amp*fade*sinf(6.28318530718f*hz*i/RATE));
            samples[j*2]=v;samples[j*2+1]=v;
        }
        size_t bytes=0,wanted=(size_t)count*2*sizeof(int16_t);
        if(i2s_channel_write(tx,samples,wanted,&bytes,150)!=ESP_OK||bytes!=wanted)break;
    }
    return generation_ok(gen);
}

static void play_track(uint8_t track,uint32_t gen){
    if(track>=LUXOR_MUSIC_TRACKS)track=0;
    music_active=true;
    /* Loop until the user stops, switches tracks, enters standby, or powers off. */
    while(generation_ok(gen)){
        const note_t *n=tracks[track];
        for(unsigned i=0;n[i].ms && generation_ok(gen);i++){
            if(!play_tone((float)n[i].hz,n[i].ms,gen))break;
        }
        if(generation_ok(gen))vTaskDelay(pdMS_TO_TICKS(120));
    }
    music_active=false;
    write_silence();
}

static void play_chime(uint32_t gen){
    play_tone(880,95,gen);play_tone(1175,145,gen);write_silence();
}

static void speaker_task(void *arg){
    (void)arg;audio_cmd_t cmd;
    for(;;){
        if(xQueueReceive(commands,&cmd,portMAX_DELAY)!=pdTRUE)continue;
        if(cmd.type==ACMD_STOP){music_active=false;continue;}
        if(i2s_channel_enable(tx)!=ESP_OK)continue;
        if(cmd.type==ACMD_CHIME)play_chime(cmd.generation);
        else if(cmd.type==ACMD_PLAY)play_track(cmd.track,cmd.generation);
        i2s_channel_disable(tx);
    }
}

static void microphone_task(void *arg){
    (void)arg;int32_t samples[FRAMES*2];bool enabled=false;
    for(;;){
        bool requested=mic_requested;
        if(requested!=enabled){
            esp_err_t e=requested?i2s_channel_enable(rx):i2s_channel_disable(rx);
            if(e==ESP_OK)enabled=requested;
            STATE_LOCK();g_state.mic_on=enabled;if(!enabled)g_state.mic_dbfs=-96;STATE_UNLOCK();
        }
        if(!enabled){vTaskDelay(pdMS_TO_TICKS(100));continue;}
        size_t bytes=0;if(i2s_channel_read(rx,samples,sizeof(samples),&bytes,100)!=ESP_OK)continue;
        size_t n=bytes/(2*sizeof(int32_t));if(n<8)continue;
        double sum=0,sumsq=0;
        for(size_t i=0;i<n;i++){double x=samples[2*i]/2147483648.0;sum+=x;sumsq+=x*x;}
        double mean=sum/n,var=fmax(0.0,sumsq/n-mean*mean);
        float db=(float)(10.0*log10(fmax(var,1e-10)));
        STATE_LOCK();g_state.mic_dbfs=fmaxf(-96.f,fminf(0.f,db));STATE_UNLOCK();
    }
}

esp_err_t audio_init(void){
    i2s_chan_config_t c=I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0,I2S_ROLE_MASTER);
    c.dma_desc_num=6;c.dma_frame_num=FRAMES;
    ESP_ERROR_CHECK(i2s_new_channel(&c,&tx,NULL));
    i2s_std_config_t t={.clk_cfg=I2S_STD_CLK_DEFAULT_CONFIG(RATE),
        .slot_cfg=I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT,I2S_SLOT_MODE_STEREO),
        .gpio_cfg={.mclk=I2S_GPIO_UNUSED,.bclk=GPIO_NUM_48,.ws=GPIO_NUM_38,.dout=GPIO_NUM_47,.din=I2S_GPIO_UNUSED}};
    ESP_ERROR_CHECK(i2s_channel_init_std_mode(tx,&t));
    c.id=I2S_NUM_1;ESP_ERROR_CHECK(i2s_new_channel(&c,NULL,&rx));
    i2s_std_config_t r={.clk_cfg=I2S_STD_CLK_DEFAULT_CONFIG(RATE),
        .slot_cfg=I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_32BIT,I2S_SLOT_MODE_STEREO),
        .gpio_cfg={.mclk=I2S_GPIO_UNUSED,.bclk=GPIO_NUM_15,.ws=GPIO_NUM_2,.dout=I2S_GPIO_UNUSED,.din=GPIO_NUM_39}};
    ESP_ERROR_CHECK(i2s_channel_init_std_mode(rx,&r));
    commands=xQueueCreate(8,sizeof(audio_cmd_t));if(!commands)return ESP_ERR_NO_MEM;
    if(xTaskCreate(speaker_task,"speaker",4608,NULL,4,NULL)!=pdPASS)return ESP_ERR_NO_MEM;
    if(xTaskCreate(microphone_task,"mic",6144,NULL,3,NULL)!=pdPASS)return ESP_ERR_NO_MEM;
    ready=true;return ESP_OK;
}

static void send_cmd(audio_cmd_type_t type,uint8_t track){
    if(!ready)return;
    audio_cmd_t c={.type=(uint8_t)type,.track=track,.generation=generation};
    xQueueSend(commands,&c,0);
}

void audio_chime(void){if(!music_active)send_cmd(ACMD_CHIME,0);}
void audio_set_mic(bool enabled){mic_requested=enabled;}
void audio_set_volume(unsigned percent){output_volume=percent>100?100:percent;}
unsigned audio_get_volume(void){return output_volume;}

void audio_music_previous(void){
    selected_track=(uint8_t)((selected_track+LUXOR_MUSIC_TRACKS-1)%LUXOR_MUSIC_TRACKS);
    if(music_active){generation++;send_cmd(ACMD_PLAY,selected_track);}
}
void audio_music_next(void){
    selected_track=(uint8_t)((selected_track+1)%LUXOR_MUSIC_TRACKS);
    if(music_active){generation++;send_cmd(ACMD_PLAY,selected_track);}
}
void audio_music_toggle(void){
    generation++;
    if(music_active)send_cmd(ACMD_STOP,selected_track);else send_cmd(ACMD_PLAY,selected_track);
}
void audio_music_stop(void){generation++;send_cmd(ACMD_STOP,selected_track);}
bool audio_music_playing(void){return music_active;}
uint8_t audio_music_track(void){return selected_track;}
const char *audio_music_track_name(uint8_t track){return track_names[track%LUXOR_MUSIC_TRACKS];}
