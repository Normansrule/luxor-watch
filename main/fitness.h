#pragma once
#include <stdint.h>
#include <stdbool.h>

typedef struct {
    uint32_t last_steps;
    uint32_t active_seconds;
    uint32_t last_step_seen_s;
    uint32_t goal_steps;
    uint16_t stride_cm;
} luxor_fitness_t;

void luxor_fitness_init(luxor_fitness_t *f, uint32_t steps, uint32_t active_seconds, uint32_t goal_steps, uint16_t stride_cm);
void luxor_fitness_tick(luxor_fitness_t *f, uint32_t steps, uint32_t now_s);
float luxor_fitness_distance_km(const luxor_fitness_t *f, uint32_t steps);
uint8_t luxor_fitness_goal_percent(const luxor_fitness_t *f, uint32_t steps);
