#include "fitness.h"
#include <string.h>

void luxor_fitness_init(luxor_fitness_t *f, uint32_t steps, uint32_t active_seconds, uint32_t goal_steps, uint16_t stride_cm){
    memset(f,0,sizeof(*f));
    f->last_steps=steps;
    f->active_seconds=active_seconds;
    f->goal_steps=goal_steps?goal_steps:8000;
    f->stride_cm=stride_cm?stride_cm:75;
}
void luxor_fitness_tick(luxor_fitness_t *f, uint32_t steps, uint32_t now_s){
    if(!f)return;
    if(steps!=f->last_steps){
        f->last_step_seen_s=now_s;
        f->last_steps=steps;
    }
    if(f->last_step_seen_s && now_s-f->last_step_seen_s<=3)f->active_seconds++;
}
float luxor_fitness_distance_km(const luxor_fitness_t *f, uint32_t steps){
    if(!f)return 0;
    return (steps*(float)f->stride_cm)/100000.0f;
}
uint8_t luxor_fitness_goal_percent(const luxor_fitness_t *f, uint32_t steps){
    if(!f||!f->goal_steps)return 0;
    uint32_t p=(steps*100U)/f->goal_steps;
    return p>100?100:(uint8_t)p;
}
