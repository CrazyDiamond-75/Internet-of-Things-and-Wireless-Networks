#include "button.h"

double random_interval_normal(double mu, double sd)
{
    // Sample 6 uniform random values and sum them up
    double sum = 0.0;
    for (int i = 0; i < 6; i++)
    {
        uint8_t base;
        sys_rand_get(&base, sizeof(base));
        sum += base / 255.0;
    }

    // Scale and shift to get the desired mean mu and standard deviation sd.
    return mu + M_SQRT2 * sd * (sum - 3.0);
}

double random_interval_bernoulli(double p, double dt_false, double dt_true)
{
    uint8_t base;
    sys_rand_get(&base, sizeof(base));

    return p < (base / 255.0) ? dt_true : dt_false;
}

double random_interval_uniform(double dt_min, double dt_max)
{
    uint8_t base;
    sys_rand_get(&base, sizeof(base));

    return dt_min + (base / 255.0) * (dt_max - dt_min);
}

double constant_interval(double dt)
{
    return dt;
}

double looped_interval(double *dts, int n)
{
    static int i = 0; // Keep value between invocations
    double dt = dts[i];
    i = (i + 1) % n; // Loop back to the start of the array
    return dt;
}

// Example value for purpose of testing.
double next_event_dt()
{
#if METHOD == 0
    return random_interval_normal(1000.0, 200.0);
#elif METHOD == 1
    return random_interval_bernoulli(0.7, 500.0, 1500.0);
#elif METHOD == 2
    return random_interval_uniform(500.0, 1500.0);
#elif METHOD == 3
    return constant_interval(1000.0);
#elif METHOD == 4
    double sequence[] = {500.0, 1000.0, 1500.0};
    return looped_interval(sequence, 3);
#endif
}

void button_thread(void (*on_event)())
{
    while (1)
    {
#if METHOD != 5
        k_msleep((int)next_event_dt());
        on_event();
#else
        // Stub for real button presses, not implemented.
        k_sleep(K_FOREVER); // To prevent busy waiting, because this is not implemented.
#endif
    }
}