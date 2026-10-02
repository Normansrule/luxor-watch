#include <assert.h>
#include <stdio.h>
#include <math.h>
#include "../main/fitness.h"
int main(void){
    luxor_fitness_t f;luxor_fitness_init(&f,1000,0,8000,75);
    assert(luxor_fitness_goal_percent(&f,4000)==50);
    assert(fabsf(luxor_fitness_distance_km(&f,1000)-0.75f)<0.001f);
    luxor_fitness_tick(&f,1001,10);luxor_fitness_tick(&f,1001,11);
    assert(f.active_seconds==2);
    printf("ALL FITNESS CHECKS PASSED\n");
    return 0;
}
