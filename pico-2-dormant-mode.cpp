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

static void processor_deep_sleep(void) {
    // Enable deep sleep at the proc
#ifdef __riscv
    uint32_t bits = RVCSR_MSLEEP_POWERDOWN_BITS;
    if (!get_core_num()) {
        bits |= RVCSR_MSLEEP_DEEPSLEEP_BITS;
    }
    riscv_set_csr(RVCSR_MSLEEP_OFFSET, bits);
#else
    scb_hw->scr |= ARM_CPU_PREFIXED(SCR_SLEEPDEEP_BITS);
#endif
}

wakeup_source_t sleep_goto_dormant_until_pin_or_time(uint gpio_pin, bool edge, bool high, struct timespec *ts, aon_timer_alarm_handler_t aonTimerAlarmCallback) {
#pragma region From sleep_goto_dormant_until_pin()
    bool low = !high;
    bool level = !edge;

    // Configure the appropriate IRQ at IO bank 0
    assert(gpio_pin < NUM_BANK0_GPIOS);

    uint32_t event = 0;

    if (level && low) event = GPIO_IRQ_LEVEL_LOW;
    if (level && high) event = GPIO_IRQ_LEVEL_HIGH;
    if (edge && high) event = GPIO_IRQ_EDGE_RISE;
    if (edge && low) event = GPIO_IRQ_EDGE_FALL;

    gpio_init(gpio_pin);
    gpio_set_input_enabled(gpio_pin, true);
    gpio_acknowledge_irq(gpio_pin, event); // Added to clear out a possible pending interrupt from the GPIO pin that shouldn't trigger a wakeup yet
    gpio_set_dormant_irq_enabled(gpio_pin, event, true);
#pragma endregion

#pragma region From sleep_goto_dormant_until()
    // We should have already called the sleep_run_from_dormant_source function
    uint64_t restore_ms = powman_timer_get_ms();
    powman_timer_set_1khz_tick_source_lposc();
    powman_timer_set_ms(restore_ms);

    clocks_hw->sleep_en0 = CLOCKS_SLEEP_EN0_CLK_REF_POWMAN_BITS;
    clocks_hw->sleep_en1 = 0x0;

    // Set the AON timer to wake up the proc from dormant mode
    aon_timer_enable_alarm(ts, aonTimerAlarmCallback, true);

    stdio_flush();

    // Enable deep sleep at the proc
    processor_deep_sleep();
#pragma endregion

    rosc_set_dormant();
    // Execution stops here until woken up

    // Get the current time to determine whether the wakeup was timer or GPIO triggered
    struct timespec nowTs;
    aon_timer_get_time(&nowTs);

    // Disable the AON timer alarm so it doesn't fire in case GPIO was the wakeup source
    aon_timer_disable_alarm();

    // Check if the AON timer was the wakeup source
    bool aon_timer_wakeup = (nowTs.tv_sec >= ts->tv_sec) && (nowTs.tv_nsec >= ts->tv_nsec);

    // Always clean up the GPIO-related parts before returning the wakeup source
    gpio_acknowledge_irq(gpio_pin, event);
    gpio_set_dormant_irq_enabled(gpio_pin, event, false);
    gpio_set_input_enabled(gpio_pin, false);

    return aon_timer_wakeup ? WAKEUP_SOURCE_TIMER : WAKEUP_SOURCE_GPIO;
}

int main()
{
    struct timespec ts = { .tv_sec = GOOD_ENOUGH_TIMESTAMP, .tv_nsec = 0 };
    aon_timer_start(&ts);

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

        // Reconfigure the clock that's used in DORMANT mode
        sleep_run_from_lposc();

        // Sleep in DORMANT mode for 10 seconds or until the GPIO pin goes high, then wake up and repeat
        struct timespec tsAlarm;
        aon_timer_get_time(&tsAlarm);
        tsAlarm.tv_sec += 10;
        auto wakeupSource = sleep_goto_dormant_until_pin_or_time(GPIO_WAKEUP_PIN, true, true, &tsAlarm, nullptr);
        sleep_power_up();

        // If the wakeup source was the GPIO, set the LED flash rate to be faster to indicate a GPIO wakeup occurred
        // Otherwise set the LED flash rate to be slower to indicate a timer wakeup occurred
        if (wakeupSource == WAKEUP_SOURCE_GPIO) {
            ledFlashRateMillis = 100;
        } else {
            ledFlashRateMillis = 500;
        }
    }
}
