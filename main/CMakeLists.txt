idf_component_register(
    SRCS "watch_main.c" "board.c" "luxconf.c" "audio.c" "radio.c" "motion.c"
         "state.c" "protocol.c" "fitness.c" "gestures.c" "battery.c" "ui.c"
         "fonts/lux_inter_14.c" "fonts/lux_inter_16.c" "fonts/lux_inter_20.c"
         "fonts/lux_inter_28.c" "fonts/lux_inter_48.c"
    INCLUDE_DIRS "."
    REQUIRES driver esp_lcd esp_adc esp_timer esp_wifi esp_netif esp_event
             nvs_flash bt lwip)
