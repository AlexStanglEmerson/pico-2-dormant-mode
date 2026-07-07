#ifndef _PICO_LOW_POWER_EDIT_H_
#define _PICO_LOW_POWER_EDIT_H_

#include "pico/low_power.h"
#include "hardware/clocks.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    WAKEUP_SOURCE_GPIO,
    WAKEUP_SOURCE_TIMER,
    WAKEUP_SOURCE_DID_NOT_GO_DORMANT,
    WAKEUP_SOURCE_COUNT
} wakeup_source_t;

wakeup_source_t low_power_dormant_until_gpio_pin_state_or_for_ms(uint gpio_pin, bool edge, bool high, uint32_t ms,
                                       dormant_clock_source_t dormant_clock_source,
                                       const clock_dest_bitset_t *keep_enabled);

#ifdef __cplusplus
}
#endif

#endif
