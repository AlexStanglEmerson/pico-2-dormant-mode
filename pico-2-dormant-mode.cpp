#include <stdio.h>
#include "pico/stdlib.h"
#include "pico/low_power.h"
#include "hardware/clocks.h"
#include "hardware/powman.h"

#define YEAR (((__DATE__[7] - '0') * 1000) + ((__DATE__[8] - '0') * 100) + ((__DATE__[9] - '0') * 10) + (__DATE__[10] - '0'))
#define GOOD_ENOUGH_TIMESTAMP ((uint64_t)YEAR * 365 * 24 * 60 * 60) // Roughly the number of seconds in a year

#define GPIO_WAKEUP_PIN 7
#define clocks_hw ((clocks_hw_t *)CLOCKS_BASE)

enum wakeup_source_t {
    WAKEUP_SOURCE_GPIO,
    WAKEUP_SOURCE_TIMER
};

static uint32_t ledFlashRateMillis = 100;


int main()
{
    low_power_start_aon_timer_at_time_ms(GOOD_ENOUGH_TIMESTAMP * 1000);

    // Initialize the LED GPIO as an output
    gpio_init(PICO_DEFAULT_LED_PIN);
    gpio_set_dir(PICO_DEFAULT_LED_PIN, GPIO_OUT);

    // Initialize a GPIO as an input for waking up from DORMANT mode
    gpio_init(GPIO_WAKEUP_PIN);
    gpio_set_dir(GPIO_WAKEUP_PIN, GPIO_IN);
    gpio_pull_down(GPIO_WAKEUP_PIN);

    while (true) {
        // Flash the LED for 3 seconds while the device is awake
        auto start_time = time_us_64();
        while ((time_us_64() - start_time) < 3000000) {
            gpio_put(PICO_DEFAULT_LED_PIN, 1);
            sleep_ms(ledFlashRateMillis);
            gpio_put(PICO_DEFAULT_LED_PIN, 0);
            sleep_ms(ledFlashRateMillis);
        }

        // Put the Pico to sleep until the GPIO wakeup pin goes high
        low_power_dormant_until_gpio_pin_state(GPIO_WAKEUP_PIN, true, true, DORMANT_CLOCK_SOURCE_LPOSC, nullptr);

        // // Put the Pico to sleep until the AON timer wakes up the Pico
        // low_power_dormant_for_ms(5000, DORMANT_CLOCK_SOURCE_LPOSC, nullptr);
    }
}
