/**
 * @file main.c
 * @author risinek (risinek@gmail.com)
 * @date 2021-04-03
 * @copyright Copyright (c) 2021
 * 
 * @brief Main file used to setup ESP32 into initial state
 * 
 * Starts management AP and webserver  
 */
#include "esp_heap_caps.h"
#include "esp_psram.h"

#include <stdio.h>
#include "nvs_flash.h"


#define LOG_LOCAL_LEVEL ESP_LOG_VERBOSE
#include "esp_log.h"
#include "esp_event.h"

#include "attack.h"
#include "wifi_controller.h"
#include "webserver.h"

#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "st7789.h"
#include "driver/gptimer.h"
#include "utime.h"
#include "esp_timer.h"

#include "global.h"


static const char* TAG = "main";

#define LV_TICK_PERIOD_MS 1

SemaphoreHandle_t xGuiSemaphore;

TaskHandle_t gui_task_Handle;
TaskHandle_t server_task_Handle;

#define STACK_SIZE 4000

char * globalData[MAX_STRINGS] = {0};  
int globalDataCount = 0;
int framesPerSecond = 0;
//portMUX_TYPE dataMutex = portMUX_INITIALIZER_UNLOCKED;

#define SSID_MAX_LENGTH 36

void gui_task(void *arg) {
    lv_init();

    lv_display_t *display = lv_display_create(MY_DISP_HOR_RES, MY_DISP_VER_RES);
    lv_display_set_resolution(display, MY_DISP_HOR_RES, MY_DISP_VER_RES);

    static lv_color_t buf1[DISP_BUF_SIZE / 10];
    static lv_color_t buf2[DISP_BUF_SIZE / 10];
    lv_display_set_buffers(display, buf1, buf2, sizeof(buf1), LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(display, st7789_flush);
    
    static lv_style_t st;
    lv_style_init(&st);
    lv_style_set_text_font(&st, &lv_font_montserrat_14);

    lv_obj_t * screen = lv_scr_act();
    lv_obj_set_style_bg_color(screen, lv_color_black(), 0);
    lv_style_set_bg_color(&st, lv_color_black());
    lv_style_set_text_color(&st, lv_color_white()); 


    lv_obj_t *labels[15];

    for (size_t i = 0; i < 15; i++) {
        labels[i] = lv_label_create(lv_scr_act()); 
        lv_label_set_text(labels[i], ""); 
        lv_obj_set_pos(labels[i], 35, 15 * i);
        lv_obj_add_style(labels[i], &st, 0);
    }

    while (1) {
        vTaskDelay(10 / portTICK_PERIOD_MS);

        if (pdTRUE == xSemaphoreTake(xGuiSemaphore, 50 / portTICK_PERIOD_MS)) {
            if (globalDataCount == 0) {
                lv_label_set_text(labels[0], "AP: ManagementAP");
                lv_label_set_text(labels[1], "Pass: mgmtadmin");
                lv_label_set_text(labels[2], "IP: 192.168.4.1");
            } else {
                char buffer[32];
                sprintf(buffer, "Frames per second: %d", framesPerSecond);
                lv_label_set_text(labels[0], buffer);
                lv_label_set_text(labels[1], "SSIDs attacked:");
                for (int j=0; j<globalDataCount; j++) {
                    lv_label_set_text(labels[j+2], globalData[j]);
                }
            }

            lv_task_handler();  
            lv_refr_now(NULL);
            xSemaphoreGive(xGuiSemaphore);
        }
    }
}


void webserver_task(void *arg) {
    webserver_run();
    vTaskDelete(NULL); // Usunięcie zadania po zakończeniu działania serwera
}

void app_main(void) {
    ESP_LOGD(TAG, "app_main started");

    esp_err_t ret1 = esp_psram_init();
    if (ret1 == ESP_OK) {
        size_t psram_size = esp_psram_get_size();
        ESP_LOGW(TAG, "PSRAM size: %d bytes\n", psram_size);
    } else {
        ESP_LOGW(TAG, "PSRAM not detected\n");
    }

    void* ptr = heap_caps_malloc(1024, MALLOC_CAP_SPIRAM);
    if (ptr != NULL) {
        ESP_LOGW(TAG, "Malloc from PSRAM succeeded\n");
        heap_caps_free(ptr);
    } else {
        ESP_LOGW(TAG, "Malloc from PSRAM failed\n");
    }

    ESP_ERROR_CHECK(esp_event_loop_create_default());
    ESP_ERROR_CHECK(nvs_flash_init());
    wifictl_mgmt_ap_start();
    attack_init();

    xGuiSemaphore = xSemaphoreCreateMutex();
    spi_display_init();
    st7789_init();

    xTaskCreate(gui_task,       "gui",       STACK_SIZE, NULL, 3, &gui_task_Handle);
    xTaskCreate(webserver_task, "webserver", STACK_SIZE, NULL, 5, &server_task_Handle);

    vTaskDelay(1000 / portTICK_PERIOD_MS);

    ESP_LOGW(TAG, "esp_get_free_heap_size(): %u bytes\n", esp_get_free_heap_size());
    size_t free_size = heap_caps_get_free_size(MALLOC_CAP_DEFAULT);
    size_t largest_block = heap_caps_get_largest_free_block(MALLOC_CAP_DEFAULT);
    ESP_LOGW(TAG, "heap_caps_get_largest_free_block(MALLOC_CAP_DEFAULT): %d bytes\n", largest_block);
    ESP_LOGW(TAG, "heap_caps_get_free_size(MALLOC_CAP_DEFAULT): %d bytes\n", free_size);
    ESP_LOGW(TAG, "heap_caps_get_free_size(MALLOC_CAP_8BIT): %u bytes", heap_caps_get_free_size(MALLOC_CAP_8BIT));

}

