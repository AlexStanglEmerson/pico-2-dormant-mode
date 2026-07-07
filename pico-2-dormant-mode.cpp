#include "pico/stdlib.h"
#include "low_power_edit.h"

#define GPIO_WAKEUP_PIN 7

static uint32_t ledFlashRateMillis = 100;

int main()
{
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

        // Put the Pico to sleep until the GPIO wakeup pin goes high or the AON timer wakes up the Pico
        auto wakeup_source = low_power_dormant_until_gpio_pin_state_or_for_ms(GPIO_WAKEUP_PIN, true, true, 10000, DORMANT_CLOCK_SOURCE_LPOSC, nullptr);

        // A GPIO wakeup should flash the LED fast, a timer wakeup should flash the LED slow, and if the Pico didn't go dormant, flash the LED fast enough that it looks almost constant
        if (wakeup_source == WAKEUP_SOURCE_GPIO) {
            ledFlashRateMillis = 100;
        } else if (wakeup_source == WAKEUP_SOURCE_TIMER) {
            ledFlashRateMillis = 500;
        } else {
            ledFlashRateMillis = 10;
        }
    }
}
