#include <stdio.h>
#include <stdlib.h>

#include "pico/stdlib.h"
#include "hardware/pio.h"
#include "hardware/clocks.h"
#include "ws2812.pio.h"
#include "tach.pio.h"

#define TACH_PIN 16
#define CYLINDERS 4
#define MAX_RPM 8000

#define PULSES_PER_REV 2

#define REDLINE_LED_NUM 2

#define clk_div 125
#define sys_hz SYS_CLK_HZ
#define pio_hz (int)(sys_hz / clk_div)
#define ns_per_count (int)(2000000000.0 / pio_hz)

// simple MIN / MAX helpers
#ifndef MIN
#define MIN(a,b) ((a) < (b) ? (a) : (b))
#endif
#ifndef MAX
#define MAX(a,b) ((a) > (b) ? (a) : (b))
#endif


struct Color {
    uint8_t r;
    uint8_t g;
    uint8_t b;
};

const struct Color GREEN = {0, 255, 0};
const struct Color BLUE = {0, 0, 255};
const struct Color YELLOW = {255, 255, 0};
const struct Color RED = {255, 0, 0};

const struct Color PALLETE[4] = {
    GREEN,
    BLUE,
    YELLOW,
    RED
};

const size_t NUM_SEGMENTS = sizeof(PALLETE) / sizeof(PALLETE[0]);

/**
 * NOTE:
 *  Take into consideration if your WS2812 is a RGB or RGBW variant.
 *
 *  If it is RGBW, you need to set IS_RGBW to true and provide 4 bytes per 
 *  pixel (Red, Green, Blue, White) and use urgbw_u32().
 *
 *  If it is RGB, set IS_RGBW to false and provide 3 bytes per pixel (Red,
 *  Green, Blue) and use urgb_u32().
 *
 *  When RGBW is used with urgb_u32(), the White channel will be ignored (off).
 *
 */
#define IS_RGBW false
#define NUM_PIXELS 16

#ifdef PICO_DEFAULT_WS2812_PIN
#define WS2812_PIN PICO_DEFAULT_WS2812_PIN
#else
// default to pin 2 if the board doesn't have a default WS2812 pin defined
#define WS2812_PIN 0
#endif

// Check the pin is compatible with the platform
#if WS2812_PIN >= NUM_BANK0_GPIOS
#error Attempting to use a pin>=32 on a platform that does not support it
#endif


volatile uint32_t last_pulse_time = 0;
volatile uint32_t delta_time_us = 0;


static inline void put_pixel(PIO pio, uint sm, uint32_t pixel_grb) {
    pio_sm_put_blocking(pio, sm, pixel_grb << 8u);
}

static inline uint32_t urgb_u32(uint8_t r, uint8_t g, uint8_t b) {
    return
            ((uint32_t) (r) << 8) |
            ((uint32_t) (g) << 16) |
            (uint32_t) (b);
}

static inline uint32_t urgbw_u32(uint8_t r, uint8_t g, uint8_t b, uint8_t w) {
    return
            ((uint32_t) (r) << 8) |
            ((uint32_t) (g) << 16) |
            ((uint32_t) (w) << 24) |
            (uint32_t) (b);
}


struct Color get_arbitrary_gradient(int value, float brightness, int max_value) {
    int val = MAX(0, MIN(value, max_value));

    int segment_width = max_value / NUM_SEGMENTS;

    int segment_index = val / segment_width;

    if (segment_index >= NUM_SEGMENTS - 1) {
        // return the last color from the pallete
        struct Color last_color = PALLETE[NUM_SEGMENTS - 1];
        return (struct Color){
            (uint8_t)(last_color.r * brightness),
            (uint8_t)(last_color.g * brightness),
            (uint8_t)(last_color.b * brightness)
        };
    };

    int local_val = val - (segment_index * segment_width);
    float t = (float)local_val / segment_width;

    struct Color c_start = PALLETE[segment_index];
    struct Color c_end = PALLETE[segment_index + 1];

    int r = (uint8_t)((c_start.r + (c_end.r - c_start.r) * t) * brightness);
    int g = (uint8_t)((c_start.g + (c_end.g - c_start.g) * t) * brightness);
    int b = (uint8_t)((c_start.b + (c_end.b - c_start.b) * t) * brightness);

    struct Color output_color = {r, g, b};
    return output_color;
}

int map_range(int x, int in_min, int in_max, int out_min, int out_max) {
    return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}

uint32_t pack_rgb(const struct Color color) {
    return urgb_u32(color.r, color.g, color.b);
}

void set_rpm(int rpm, PIO pio, uint sm, uint len, float brightness) {
    int num_leds = NUM_PIXELS - REDLINE_LED_NUM;
    int target_led_num = map_range(rpm, 0, MAX_RPM, 0, num_leds);

    for (int i = 0; i < NUM_PIXELS; i++) {
        if (i < target_led_num) {
            int target_val = i * (MAX_RPM - 1) / (num_leds - 1);
            struct Color rgb = get_arbitrary_gradient(target_val, 0.1, MAX_RPM);
            uint32_t packed_color = pack_rgb(rgb);
            put_pixel(pio, sm, packed_color);
        } else if (i >= num_leds){
            // set these to red
            uint32_t packed_color = pack_rgb((struct Color){
                (uint8_t)(RED.r * brightness),
                (uint8_t)(RED.g * brightness),
                (uint8_t)(RED.b * brightness)
            });
            put_pixel(pio, sm, packed_color);
        } else {
            struct Color rgb = {0, 0, 0};
            uint32_t packed_color = pack_rgb(rgb);
            put_pixel(pio, sm, packed_color);
        }

    }
}



int main() {
    //set_sys_clock_48();
    stdio_init_all();
    printf("WS2812 Smoke Test, using pin %d\n", WS2812_PIN);

    // todo get free sm
    PIO pio_0;
    uint sm_0;
    uint offset_0;

    // This will find a free pio and state machine for our program and load it for us
    // We use pio_claim_free_sm_and_add_program_for_gpio_range (for_gpio_range variant)
    // so we will get a PIO instance suitable for addressing gpios >= 32 if needed and supported by the hardware
    bool success = pio_claim_free_sm_and_add_program_for_gpio_range(&ws2812_program, &pio_0, &sm_0, &offset_0, WS2812_PIN, 1, true);
    hard_assert(success);

    ws2812_program_init(pio_0, sm_0, offset_0, WS2812_PIN, 800000, IS_RGBW);

    // get a free state machine for the tach pio program
    PIO pio_1;
    uint sm_1;
    uint offset_1;

    success = pio_claim_free_sm_and_add_program_for_gpio_range(&tach_program, &pio_1, &sm_1, &offset_1, TACH_PIN, 1, true);
    hard_assert(success);
    
    tach_program_init(pio_1, sm_1, offset_1, TACH_PIN, clk_div);

    // startup sequence
    int rpm = 0;
    bool sweep_up = true;
    while (1) {
        if (sweep_up) {
            rpm += 20;
            if (rpm >= 8000) {
                sweep_up = false;
            }
        } else {
            rpm -= 20;
            if (rpm <= 0) {
                break;
            }
        }

        set_rpm(rpm, pio_0, sm_0, NUM_PIXELS, 0.1);
        sleep_us(50);
        sleep_ms(1);
    }

    
    while (true) {
        if (pio_sm_get_rx_fifo_level(pio_1, sm_1)) {
            uint32_t raw_value = pio_sm_get(pio_1, sm_1);
            uint32_t cycles_spent = 0xFFFFFFFF - raw_value;

            // The PIO loops take exactly 2 clock cycles per count decrement
            float total_clock_cycles = (float)cycles_spent * 2.0f;
            
            // Convert clock cycles directly into total wave period in nanoseconds
            float total_period_ns = total_clock_cycles * ns_per_count;

            if (total_period_ns > 0.0f) {
                // RPM = (60 billion ns / total period of 1 revolution)
                // Divide by PULSES_PER_REV to account for your engine/sensor setup
                float rpm = (60000000000LL / total_period_ns) / PULSES_PER_REV;
                printf("RPM: %.2f\n", rpm);
            }
            stdio_flush();
        }
        sleep_ms(10);
    }

    // This will free resources and unload our program
    pio_remove_program_and_unclaim_sm(&ws2812_program, pio_0, sm_0, offset_0);
    pio_remove_program_and_unclaim_sm(&tach_program, pio_1, sm_1, offset_1);
}