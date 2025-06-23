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
#include <string.h>
#include <ctype.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/uart.h"
#include "nvs_flash.h"

#define LOG_LOCAL_LEVEL ESP_LOG_VERBOSE
#include "esp_log.h"
#include "esp_event.h"

#include "attack.h"
#include "attack_dos.h"
#include "attack_handshake.h"
#include "attack_pmkid.h"
#include "wifi_controller.h"
#include "webserver.h"
#include "led_status.h"

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


#define CLI_UART_BUF_SIZE 128
#define CLI_UART_PORT UART_NUM_0

static volatile bool scan_running = false;
static TaskHandle_t scan_task_handle = NULL;

static const char *get_band_from_channel(uint8_t ch){
    if(ch >= 1 && ch <= 14){
        return "2.4G";
    } else if(ch >= 36 && ch <= 165){
        return "5G";
    } else if(ch >= 1 && ch <= 233){
        return "6G";
    }
    return "?";
}

static void print_ap_list_flipper_band(void){
    const wifictl_ap_records_t *records = wifictl_get_ap_records();
    for(int i = 0; i < records->count; i++){
        const wifi_ap_record_t *rec = &records->records[i];
        const char* enc = "OPEN";
        switch(rec->authmode){
            case WIFI_AUTH_WPA_PSK:
                enc = "WPA";
                break;
            case WIFI_AUTH_WPA2_PSK:
            case WIFI_AUTH_WPA_WPA2_PSK:
                enc = "WPA2";
                break;
#ifdef WIFI_AUTH_WPA3_PSK
            case WIFI_AUTH_WPA3_PSK:
                enc = "WPA3";
                break;
#endif
#ifdef WIFI_AUTH_WPA2_WPA3_PSK
            case WIFI_AUTH_WPA2_WPA3_PSK:
                enc = "WPA2/WPA3";
                break;
#endif
            default:
                break;
        }

        printf("[%d] SSID:%s RSSI:%d Ch:%d BSSID:%02x:%02x:%02x:%02x:%02x:%02x %s %s\n",
               i,
               rec->ssid[0] ? (char *)rec->ssid : "<hidden>",
               rec->rssi,
               rec->primary,
               rec->bssid[0], rec->bssid[1], rec->bssid[2],
               rec->bssid[3], rec->bssid[4], rec->bssid[5],
               get_band_from_channel(rec->primary), enc);
    }
}

static void scan_loop_task(void *pv){
    led_status_set_state(LED_STATE_SCAN);
    while(scan_running){
        wifictl_scan_nearby_aps();
        if(!scan_running){
            break;
        }
        print_ap_list_flipper_band();
        vTaskDelay(pdMS_TO_TICKS(1700));
    }
    led_status_set_state(LED_STATE_IDLE);
    scan_task_handle = NULL;
    vTaskDelete(NULL);
}

static void cli_start_attack(int *ids, int count){
    if(count <= 0){
        printf("No AP indices specified.\n");
        return;
    }

    attack_request_t *req = malloc(sizeof(attack_request_t));
    if(!req){
        printf("Failed to allocate memory for attack request.\n");
        return;
    }

    *req = (attack_request_t){
        .type = ATTACK_TYPE_DOS,
        .method = ATTACK_DOS_METHOD_BROADCAST,
        .timeout = 0,
        .num_aps = count
    };

    for(int i=0;i<count && i<10;i++){
        req->ap_ids[i] = ids[i];
    }

    esp_err_t err = esp_event_post(WEBSERVER_EVENTS, WEBSERVER_EVENT_ATTACK_REQUEST,
                                    req, sizeof(*req), portMAX_DELAY);
    free(req);
    if(err == ESP_OK){
        printf("Attack started on %d AP(s).\n", count);
        led_status_set_state(LED_STATE_ATTACK);
    }else{
        printf("Failed to start attack: %s\n", esp_err_to_name(err));
    }
}

static void cli_stop_attack(void){
    const attack_status_t *status = attack_get_status();
    if(status->state != RUNNING){
        printf("No attack running.\n");
        return;
    }

    switch(status->type){
        case ATTACK_TYPE_DOS:
            attack_dos_stop();
            break;
        case ATTACK_TYPE_HANDSHAKE:
            attack_handshake_stop();
            break;
        case ATTACK_TYPE_PMKID:
            attack_pmkid_stop();
            break;
        default:
            break;
    }
    attack_update_status(FINISHED);
    esp_event_post(WEBSERVER_EVENTS, WEBSERVER_EVENT_ATTACK_RESET, NULL, 0, portMAX_DELAY);
    wifictl_mgmt_ap_start();
    led_status_set_state(LED_STATE_IDLE);
    printf("Attack stopped.\n");
}

static void sanitize_command(char *dst, const uint8_t *src, size_t maxlen){
    size_t pos = 0;
    for(size_t i = 0; src[i] && pos < maxlen - 1; ++i){
        if(isalnum(src[i]) || src[i] == '_' || src[i] == '-' ||
           src[i] == '.' || isspace((unsigned char)src[i])){
            dst[pos++] = src[i];
        } else if(src[i] == ',' || src[i] == ':' || src[i] == ';'){
            /* Treat common separators as spaces so "attack 1,2" works */
            dst[pos++] = ' ';
        }
    }
    dst[pos] = '\0';
}

static void cli_task(void *pv){
    uint8_t data[CLI_UART_BUF_SIZE];
    char command[CLI_UART_BUF_SIZE];
    int pos = 0;
    printf("\nESP32 CLI ready. Type 'help'.\n> ");
    fflush(stdout);

    while(1){
        int len = uart_read_bytes(CLI_UART_PORT, &data[pos], 1, 20 / portTICK_PERIOD_MS);
        if(len > 0){
            uint8_t ch = data[pos];
            if(ch == '\r' || ch == '\n'){
                data[pos] = 0;
                sanitize_command(command, data, CLI_UART_BUF_SIZE);

                if(strlen(command) >= 3){
                    if(strcmp(command, "scan") == 0){
                        if(!scan_running){
                            scan_running = true;
                            xTaskCreate(scan_loop_task, "scan_loop", 4096, NULL, 5, &scan_task_handle);
                            led_status_set_state(LED_STATE_SCAN);
                            printf("Continuous scan started.\n");
                        } else {
                            printf("Scan already running.\n");
                        }
                    } else if(strcmp(command, "scanstop") == 0){
                        scan_running = false;
                        led_status_set_state(LED_STATE_IDLE);
                        printf("Scan stopped.\n");
                    } else if(strcmp(command, "attackstop") == 0){
                        cli_stop_attack();
                        attack_dos_stop();
                        printf("Attack stopped.\n");
                    } else if(strncmp(command, "attack", 6) == 0){
                        int ids[10];
                        int count = 0;
                        char *ptr = command + 6;
                        while(*ptr && count < 10){
                            while(*ptr && isspace((unsigned char)*ptr)) ptr++;
                            if(!*ptr) break;
                            ids[count++] = atoi(ptr);
                            while(*ptr && !isspace((unsigned char)*ptr)) ptr++;
                        }
                        cli_start_attack(ids, count);
                    } else if(strcmp(command, "reboot") == 0){
                        printf("Rebooting...\n");
                        fflush(stdout);
                        esp_restart();
                    } else if(strcmp(command, "help") == 0){
                        printf("Available commands:\n");
                        printf("  scan     - Continuous AP scan\n");
                        printf("  scanstop - Stop AP scan\n");
                        printf("  attack N [M ...] - Attack AP indexes\n");
                        printf("  attackstop - Stop running attack\n");
                        printf("  reboot   - Restart ESP32\n");
                        printf("  help     - Show this help\n");
                    } else {
                        printf("Unknown command: '%s'. Type 'help' for list of commands.\n", command);
                    }
                }
                pos = 0;
                printf("> ");
                fflush(stdout);
                continue;
            }
            if(ch >= 32 && ch < 127 && pos < CLI_UART_BUF_SIZE - 1){
                uart_write_bytes(CLI_UART_PORT, (const char *)&ch, 1);
                pos++;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}

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
        lv_obj_set_pos(labels[i],35, 15 * i);
        lv_obj_add_style(labels[i], &st, 0);
    }

    while (1) {
        vTaskDelay(500 / portTICK_PERIOD_MS);
        if (pdTRUE == xSemaphoreTake(xGuiSemaphore, 50 / portTICK_PERIOD_MS)) {
            if (globalDataCount == 0) {
                lv_label_set_text(labels[0], "AP: Livebox");
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
    led_status_init();

    // esp_err_t ret1 = esp_psram_init();
    // if (ret1 == ESP_OK) {
    //     size_t psram_size = esp_psram_get_size();
    //     ESP_LOGW(TAG, "PSRAM size: %d bytes\n", psram_size);
    // } else {
    //     ESP_LOGW(TAG, "PSRAM not detected\n");
    // }

    // void* ptr = heap_caps_malloc(1024, MALLOC_CAP_SPIRAM);
    // if (ptr != NULL) {
    //     ESP_LOGW(TAG, "Malloc from PSRAM succeeded\n");
    //     heap_caps_free(ptr);
    // } else {
    //     ESP_LOGW(TAG, "Malloc from PSRAM failed\n");
    // }


    //ESP_LOGW("MEM", "Free heap: %d bytes", esp_get_free_heap_size());
    //ESP_LOGW("MEM", "Free 8-bit heap: %d bytes", heap_caps_get_free_size(MALLOC_CAP_8BIT));

    ESP_ERROR_CHECK(esp_event_loop_create_default());

    const uart_config_t uart_config = {
        .baud_rate = 115200,
        .data_bits = UART_DATA_8_BITS,
        .parity    = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
    };
    uart_driver_install(CLI_UART_PORT, 2048, 0, 0, NULL, 0);
    uart_param_config(CLI_UART_PORT, &uart_config);
    uart_set_pin(CLI_UART_PORT, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);

    wifictl_mgmt_ap_start();
    attack_init();

    xGuiSemaphore = xSemaphoreCreateMutex();
    spi_display_init();
    st7789_init();

    xTaskCreate(gui_task,       "gui",       STACK_SIZE, NULL, 1, &gui_task_Handle);
    xTaskCreate(webserver_task, "webserver", STACK_SIZE, NULL, 5, &server_task_Handle);
    xTaskCreate(cli_task, "cli_task", 4096, NULL, 5, NULL);

    led_status_set_state(LED_STATE_IDLE);

    vTaskDelay(1000 / portTICK_PERIOD_MS);

    //ESP_LOGW(TAG, "esp_get_free_heap_size(): %u bytes\n", esp_get_free_heap_size());
    //size_t free_size = heap_caps_get_free_size(MALLOC_CAP_DEFAULT);
    //size_t largest_block = heap_caps_get_largest_free_block(MALLOC_CAP_DEFAULT);
    //ESP_LOGW(TAG, "heap_caps_get_largest_free_block(MALLOC_CAP_DEFAULT): %d bytes\n", largest_block);
    //ESP_LOGW(TAG, "heap_caps_get_free_size(MALLOC_CAP_DEFAULT): %d bytes\n", free_size);
    //ESP_LOGW(TAG, "heap_caps_get_free_size(MALLOC_CAP_8BIT): %u bytes", heap_caps_get_free_size(MALLOC_CAP_8BIT));

}
