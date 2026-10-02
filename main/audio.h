#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#define LUXOR_MUSIC_TRACKS 4

esp_err_t audio_init(void);
void audio_chime(void);
void audio_set_mic(bool enabled);

void audio_set_volume(unsigned percent);
unsigned audio_get_volume(void);
void audio_music_previous(void);
void audio_music_toggle(void);
void audio_music_next(void);
void audio_music_stop(void);
bool audio_music_playing(void);
uint8_t audio_music_track(void);
const char *audio_music_track_name(uint8_t track);
