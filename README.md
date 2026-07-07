# Pico 2 DORMANT Mode
This repository was used to determine how to put a Raspberry Pi Pico 2 into DORMANT mode and then wake it up using either a GPIO pin or the AON timer. The provided functions in the SDK only allow waking up from one of the two sources where this allows for either to wake it up.

The code has only been tested with the Pico 2 and not the original Pico. However, the code copied from the low_power.c/.h files (from the Pico SDK) still includes the Pico 1 code, so it _might_ work with the original Pico.
