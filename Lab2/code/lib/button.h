#include <zephyr.h>
#include <random/rand32.h>

/* Compiler parameter to select the method for determining the dt until the next button press.
    0: Normal interval
    1: Bernoulli interval
    2: Uniform interval
    3: Constant interval
    4: Looped sequence interval
    5: Stub for real button presses*/
#define METHOD 0

/* Button is pressed at a dt sampled from approximated normal distribution with expected value mu and standard deviation sd.
 * Internally uses an irwin-hall distribution which relies on the central limit theorem. */
double random_interval_normal(double mu, double sd);

/* Button is pressed at a dt sampled from bernoulli distribution.
 * takes dt_false if sample is false, takes dt_true if sample is true. */
double random_interval_bernoulli(double p, double dt_false, double dt_true);

/* Button is pressed at a dt sampled from uniform distribution between dt_min and dt_max. */
double random_interval_uniform(double dt_min, double dt_max);

/* Presses the button every dt ms. */
double constant_interval(double dt);

/* Presses the button at intervals defined by sequence of dts. */
double looped_interval(double *dts, int n);

/* Calculates time until next button press based on METHOD parameter. */
double next_press_dt();

/* Function run by main thread after initialization. */
void button_thread(void (*on_press)());