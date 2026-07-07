#include "low_power_edit.h"

#include "pico/stdio.h"
#include "pico/stdlib.h"
#include "pico/runtime_init.h"
#include "hardware/pll.h"
#include "hardware/rosc.h"
#include "hardware/sync.h"
#include "hardware/xosc.h"

/**
 * Significant parts of the code were taken from the official Pico SDK which requires the following license notice:
 *
 *  Copyright 2020 (c) 2020 Raspberry Pi (Trading) Ltd.
 *
 *  Redistribution and use in source and binary forms, with or without modification, are permitted provided that the
 *  following conditions are met:
 *
 *  1. Redistributions of source code must retain the above copyright notice, this list of conditions and the following
 *  disclaimer.
 *
 *  2. Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the following
 *  disclaimer in the documentation and/or other materials provided with the distribution.
 *
 *  3. Neither the name of the copyright holder nor the names of its contributors may be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 *    THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES,
 *    INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 *    DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 *    SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 *    SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 *    WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 *    THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#pragma region Copied Exactly
static uint32_t interrupt_flags;
// volatile bool event_happened;

static void replace_null_enable_values(const clock_dest_bitset_t *keep_enabled,
                                       clock_dest_bitset_t *local_keep_enabled) {
    if (keep_enabled) {
        *local_keep_enabled = *keep_enabled;
    } else {
        // default to keep nothing on
        *local_keep_enabled = clock_dest_bitset_none();
    }
}

// ------------------------------------------------------------------------------------------------------
// todo these probably belong in h/w clocks as some sort of registered thing, but leave them private here
//      for now
static void prepare_for_clock_gating(void) {
    // particularly for UART we want nothing left to clock out
    stdio_flush();
}

static void prepare_for_clock_switch(void) {
    // particularly for UART we want nothing left to clock out
    prepare_for_clock_gating();

#if LIB_TINYUSB_DEVICE
    tud_was_inited = tud_inited();
    if (tud_was_inited) tud_deinit(0);
#endif

#if LIB_TINYUSB_HOST
    tuh_was_inited = tuh_inited();
    if (tuh_was_inited) tuh_deinit(0);
#endif

#if LIB_PICO_STDIO_USB
    // deinit USB
    stdio_usb_deinit();
#endif

    // disable interrupts
    interrupt_flags = save_and_disable_interrupts();
}

// In order to go into dormant mode we need to be running from a stoppable clock source:
// either the xosc or rosc with no PLLs running. This means we disable the USB and ADC clocks
// and all PLLs
static void low_power_setup_clocks_for_dormant(dormant_clock_source_t dormant_source) {
    prepare_for_clock_switch();

    uint clk_ref_src_hz;
    uint32_t clk_ref_src;
    uint clk_sys_src_hz;
    uint32_t clk_sys_src;
    uint32_t clk_sys_aux_src;
    switch (dormant_source) {
        case DORMANT_CLOCK_SOURCE_XOSC:
            clk_ref_src_hz = XOSC_HZ;
            clk_ref_src = CLOCKS_CLK_REF_CTRL_SRC_VALUE_XOSC_CLKSRC;
            clk_sys_src_hz = clk_ref_src_hz;
            clk_sys_src = CLOCKS_CLK_SYS_CTRL_SRC_VALUE_CLK_REF;
            clk_sys_aux_src = 0;
            break;
        case DORMANT_CLOCK_SOURCE_ROSC:
            clk_ref_src_hz = rosc_measure_freq_khz() * KHZ;
            clk_ref_src = CLOCKS_CLK_REF_CTRL_SRC_VALUE_ROSC_CLKSRC_PH;
            clk_sys_src_hz = clk_ref_src_hz;
            clk_sys_src = CLOCKS_CLK_SYS_CTRL_SRC_VALUE_CLK_REF;
            clk_sys_aux_src = 0;
            break;
#if PICO_RP2040
        case DORMANT_CLOCK_SOURCE_RTC:
            clk_ref_src_hz = rosc_measure_freq_khz() * KHZ;
            clk_ref_src = CLOCKS_CLK_REF_CTRL_SRC_VALUE_ROSC_CLKSRC_PH;
            clk_sys_src_hz = clk_ref_src_hz;
            clk_sys_src = CLOCKS_CLK_SYS_CTRL_SRC_VALUE_CLK_REF;
            clk_sys_aux_src = 0;
            break;
#else
        case DORMANT_CLOCK_SOURCE_LPOSC:
            clk_ref_src_hz = 32 * KHZ;
            clk_ref_src = CLOCKS_CLK_REF_CTRL_SRC_VALUE_LPOSC_CLKSRC;
            clk_sys_src_hz = rosc_measure_freq_khz() * KHZ;
            clk_sys_src = CLOCKS_CLK_SYS_CTRL_SRC_VALUE_CLKSRC_CLK_SYS_AUX;
            clk_sys_aux_src = CLOCKS_CLK_SYS_CTRL_AUXSRC_VALUE_ROSC_CLKSRC;
            break;
#endif
        default:
            hard_assert(false);
            __builtin_unreachable();
    }

    clock_configure_undivided(clk_ref,
                              clk_ref_src,
                              0,
                              clk_ref_src_hz);

    // CLK SYS = CLK_REF
    clock_configure_undivided(clk_sys,
                    clk_sys_src,
                    clk_sys_aux_src,
                    clk_sys_src_hz);


    // CLK ADC = 0MHz
    clock_stop(clk_adc);
    clock_stop(clk_usb);
#if HAS_HSTX
    clock_stop(clk_hstx);
#endif

#if HAS_RP2040_RTC
    // RTC should already be configured to run from the external source
#endif

    // CLK PERI = clk_sys. Used as reference clock for Peripherals. No dividers so just select and enable
    clock_configure(clk_peri,
                    0,
                    CLOCKS_CLK_PERI_CTRL_AUXSRC_VALUE_CLK_SYS,
                    clk_sys_src_hz,
                    clk_sys_src_hz);

    pll_deinit(pll_sys);
    pll_deinit(pll_usb);

    // Assuming both xosc and rosc are running at the moment
    if (dormant_source == DORMANT_CLOCK_SOURCE_XOSC) {
        // Safe to disable rosc
        rosc_disable();
#if PICO_RP2040
    } else if (dormant_source == DORMANT_CLOCK_SOURCE_RTC) {
        // Run RTC directly from XOSC
    #if (XOSC_HZ % RTC_CLOCK_FREQ_HZ == 0)
        // this doesn't pull in 64 bit arithmetic
        clock_configure_int_divider(clk_rtc,
                        0, // No GLMUX
                        CLOCKS_CLK_RTC_CTRL_AUXSRC_VALUE_XOSC_CLKSRC,
                        XOSC_HZ,
                        XOSC_HZ / RTC_CLOCK_FREQ_HZ);

    #else
        clock_configure(clk_rtc,
                        0, // No GLMUX
                        CLOCKS_CLK_RTC_CTRL_AUXSRC_VALUE_XOSC_CLKSRC,
                        XOSC_HZ,
                        RTC_CLOCK_FREQ_HZ);

    #endif
#endif
    } else {
        // Safe to disable xosc
        xosc_disable();
    }
}

static void low_power_enable_processor_deep_sleep(void) {
    // Enable deep sleep at the proc
#ifdef __riscv
    uint32_t bits = RVCSR_MSLEEP_POWERDOWN_BITS;
    if (!get_core_num()) {
        // see errata RP2350-E4
        bits |= RVCSR_MSLEEP_DEEPSLEEP_BITS;
    }
    riscv_set_csr(RVCSR_MSLEEP_OFFSET, bits);
#else
    scb_hw->scr |= ARM_CPU_PREFIXED(SCR_SLEEPDEEP_BITS);
#endif
}

static bool is_timestamp_from_aon_timer(absolute_time_t timestamp) {
    return to_us_since_boot(timestamp) % 1000 == 0;
}

static void low_power_go_dormant(dormant_clock_source_t dormant_clock_source) {
    valid_params_if(PICO_LOW_POWER,
        dormant_clock_source == DORMANT_CLOCK_SOURCE_XOSC || dormant_clock_source == DORMANT_CLOCK_SOURCE_ROSC
    #if PICO_RP2040
        || dormant_clock_source == DORMANT_CLOCK_SOURCE_RTC
    #else
        || dormant_clock_source == DORMANT_CLOCK_SOURCE_LPOSC
    #endif
    );

    if (dormant_clock_source == DORMANT_CLOCK_SOURCE_XOSC) {
        xosc_dormant();
    } else {
        rosc_set_dormant();
    }
}

static void post_clock_gating(void) {
    // restore all clocks in sleep mode, to prevent other __wfi from causing issues
    clock_dest_bitset_t all = clock_dest_bitset_all();
    clock_gate_sleep_en(&all);
}

static void post_clock_switch(void) {
    // restore interrupts
    restore_interrupts_from_disabled(interrupt_flags);

#if LIB_TINYUSB_DEVICE
    if (tud_was_inited) tud_init(0);
#endif

#if LIB_TINYUSB_HOST
    if (tuh_was_inited) tuh_init(0);
#endif

#if LIB_PICO_STDIO_USB
    // reinit USB
    stdio_usb_init();
#endif
}

//To be called after waking up from sleep/dormant mode to restore system clocks properly
static void low_power_wake_from_dormant(void) {
    //Re-enable the ring oscillator, which will essentially kickstart the proc
    rosc_restart();

    post_clock_gating();

    //Restore all inactive clocks
    runtime_init_clocks();
    post_clock_switch();
}
#pragma endregion

#pragma region Custom Function
wakeup_source_t low_power_dormant_until_gpio_pin_state_or_for_ms(uint gpio_pin, bool edge, bool high, uint32_t ms,
                                       dormant_clock_source_t dormant_clock_source,
                                       const clock_dest_bitset_t *keep_enabled) {
    // This function combines code from low_power_dormant_until_gpio_pin_state(), low_power_dormant_for_ms(), and low_power_dormant_until_aon_timer()
    // Start by running setup for the clocks and AON timer
    if (gpio_pin >= NUM_BANK0_GPIOS)
    {
        return WAKEUP_SOURCE_DID_NOT_GO_DORMANT;
    }

    low_power_start_aon_timer();
    absolute_time_t until = aon_timer_make_timeout_time_ms(ms);

    // Run some of the checks from low_power_dormant_until_aon_timer()
    if (!aon_timer_is_running()) {
        return WAKEUP_SOURCE_DID_NOT_GO_DORMANT;
    }

    if (!is_timestamp_from_aon_timer(until)) {
        return WAKEUP_SOURCE_DID_NOT_GO_DORMANT;
    }

    if (to_ms_64_since_boot(aon_timer_get_absolute_time()) + PICO_LOW_POWER_MIN_DORMANT_TIME_MS > to_ms_64_since_boot(until)) {
        // Prevent race condition where the timer fires before we can go dormant
        // by setting a minimum time for dormant
        return WAKEUP_SOURCE_DID_NOT_GO_DORMANT;
    }

    clock_dest_bitset_t local_keep_enabled;
    replace_null_enable_values(keep_enabled, &local_keep_enabled);

#if PICO_RP2040
    if (dormant_clock_source != DORMANT_CLOCK_SOURCE_RTC) {
        if (dormant_rtc_src_hz == 0) {
            return WAKEUP_SOURCE_DID_NOT_GO_DORMANT;
        }
        // The RTC must be run from an external source, since the dormant source will be inactive
        if (!rtc_run_from_external_source(dormant_rtc_src_hz, dormant_rtc_gpio_pin)) {
            return WAKEUP_SOURCE_DID_NOT_GO_DORMANT;
        }
    }
    clock_dest_bitset_add(&local_keep_enabled, CLK_DEST_RTC_RTC);
#elif PICO_RP2350
    if (dormant_clock_source == DORMANT_CLOCK_SOURCE_LPOSC)
        powman_timer_set_1khz_tick_source_lposc();
    else
        return WAKEUP_SOURCE_DID_NOT_GO_DORMANT;

    clock_dest_bitset_add(&local_keep_enabled, CLK_DEST_REF_POWMAN);
#else
    #error Unknown processor
#endif

    // Setup the clocks for DORMANT mode
    low_power_setup_clocks_for_dormant(dormant_clock_source);

    // Configure the GPIO event and IRQ
    uint32_t event = 0;

    if (edge) {
        event = high ? GPIO_IRQ_EDGE_RISE : GPIO_IRQ_EDGE_FALL;
    } else { // level
        event = high ? GPIO_IRQ_LEVEL_HIGH : GPIO_IRQ_LEVEL_LOW;
    }

    gpio_set_input_enabled(gpio_pin, true);
    gpio_set_dormant_irq_enabled(gpio_pin, event, true);

    // Configure the AON timer wakeup event
    struct timespec ts;
    us_to_timespec(to_us_since_boot(until), &ts);
    // event_happened = false;
    aon_timer_enable_alarm(&ts, NULL, true);

    // Gate clocks and then go to sleep
    prepare_for_clock_gating();
    clock_gate_sleep_en(&local_keep_enabled);

    low_power_enable_processor_deep_sleep();

    low_power_go_dormant(dormant_clock_source);
    // Execution pauses here until a GPIO event occurs or the AON timer expires

    // Grab a timestamp to determine which event caused the wakeup
    absolute_time_t wakeup_time = aon_timer_get_absolute_time();

    // Disable the AON timer alarm so it doesn't fire in case GPIO was the wakeup source
    aon_timer_disable_alarm();

    // Clear the IRQ so we can go back to dormant mode again if we want
    gpio_acknowledge_irq(gpio_pin, event);
    gpio_set_dormant_irq_enabled(gpio_pin, event, false);
    gpio_set_input_enabled(gpio_pin, false);

    // Restore the system clocks and wake up from dormant mode
    low_power_wake_from_dormant();

#if PICO_RP2350
    if (dormant_clock_source == DORMANT_CLOCK_SOURCE_LPOSC)
        powman_timer_set_1khz_tick_source_xosc();
#endif

    bool aon_timer_wakeup = to_ms_64_since_boot(wakeup_time) >= to_ms_64_since_boot(until);
    return aon_timer_wakeup ? WAKEUP_SOURCE_TIMER : WAKEUP_SOURCE_GPIO;
}
#pragma endregion
