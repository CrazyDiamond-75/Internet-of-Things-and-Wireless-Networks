#include <zephyr.h>
#include <random/rand32.h> // Took me a while to find this file...

/*
    0: Random temperature
    1: Constant temperature
    2: Random walk temperature
*/
#define METHOD 0

// Sets a random internal temperature.
void set_temperature_random(double *temperature);

// Sets a constant internal temperature.
void set_temperature_constant(double *temperature);

// Sets a random walk internal temperature.
void set_temperature_random_walk(double *temperature);

// Updates temperature based on some method.
void update_temperature(double *temperature);