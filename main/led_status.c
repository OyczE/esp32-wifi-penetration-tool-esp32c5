#include "led_status.h"
#include "esp_log.h"
#include "freertos/task.h"

static const char* TAG = "led_status";

static led_strip_handle_t led_strip;
static led_state_t current_state = LED_STATE_BOOT;

static void led_task(void *pv){
    uint8_t brightness;
    while(1){
        switch(current_state){
            case LED_STATE_BOOT:
                for(brightness=0; brightness<60 && current_state==LED_STATE_BOOT; brightness++){
                    led_strip_set_pixel(led_strip, 0, brightness, brightness/2, 0);
                    led_strip_refresh(led_strip);
                    vTaskDelay(pdMS_TO_TICKS(20));
                }
                for(; brightness>0 && current_state==LED_STATE_BOOT; brightness--){
                    led_strip_set_pixel(led_strip, 0, brightness, brightness/2, 0);
                    led_strip_refresh(led_strip);
                    vTaskDelay(pdMS_TO_TICKS(20));
                }
                break;
            case LED_STATE_SCAN:
                led_strip_set_pixel(led_strip, 0, 0, 64, 0);
                led_strip_refresh(led_strip);
                vTaskDelay(pdMS_TO_TICKS(100));
                led_strip_clear(led_strip);
                vTaskDelay(pdMS_TO_TICKS(100));
                break;
            case LED_STATE_ATTACK:
                for(int i=0;i<2 && current_state==LED_STATE_ATTACK;i++){
                    led_strip_set_pixel(led_strip, 0, 64, 0, 0);
                    led_strip_refresh(led_strip);
                    vTaskDelay(pdMS_TO_TICKS(80));
                    led_strip_clear(led_strip);
                    vTaskDelay(pdMS_TO_TICKS(80));
                }
                vTaskDelay(pdMS_TO_TICKS(200));
                break;
            default:
                led_strip_clear(led_strip);
                vTaskDelay(pdMS_TO_TICKS(200));
        }
    }
}

void led_status_init(){
    led_strip_config_t strip_config = {
        .strip_gpio_num = CONFIG_BLINK_GPIO,
        .max_leds = 1,
    };
#if CONFIG_BLINK_LED_STRIP_BACKEND_RMT
    led_strip_rmt_config_t rmt_config = {
        .resolution_hz = 10 * 1000 * 1000,
        .flags.with_dma = false,
    };
    ESP_ERROR_CHECK(led_strip_new_rmt_device(&strip_config, &rmt_config, &led_strip));
#else
    #error "Unsupported LED strip backend"
#endif
    led_strip_clear(led_strip);
    xTaskCreate(led_task, "led_task", 2048, NULL, 5, NULL);
}

void led_status_set_state(led_state_t state){
    current_state = state;
}
