#ifndef LED_STATUS_H
#define LED_STATUS_H

#include "freertos/FreeRTOS.h"
#include "led_strip.h"

typedef enum {
    LED_STATE_BOOT,
    LED_STATE_SCAN,
    LED_STATE_ATTACK,
    LED_STATE_IDLE
} led_state_t;

void led_status_init(void);
void led_status_set_state(led_state_t state);

#endif // LED_STATUS_H
