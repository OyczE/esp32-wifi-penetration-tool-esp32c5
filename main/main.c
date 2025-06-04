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

#include <stdio.h>
#include <string.h>
#include <ctype.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/uart.h"

#define LOG_LOCAL_LEVEL ESP_LOG_VERBOSE
#include "esp_log.h"
#include "esp_event.h"

#include "attack.h"
#include "wifi_controller.h"
#include "webserver.h"

static const char* TAG = "main";

#define CLI_UART_BUF_SIZE 128
#define CLI_UART_PORT UART_NUM_0

static volatile bool scanap_running = false;
static TaskHandle_t scanap_task_handle = NULL;

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
        printf("Band: %s RSSI: %d Ch: %d BSSID: %02x:%02x:%02x:%02x:%02x:%02x ESSID: %s\n",
               get_band_from_channel(rec->primary),
               rec->rssi,
               rec->primary,
               rec->bssid[0], rec->bssid[1], rec->bssid[2],
               rec->bssid[3], rec->bssid[4], rec->bssid[5],
               rec->ssid[0] ? (char *)rec->ssid : "<hidden>");
    }
}

static void scanap_loop_task(void *pv){
    while(scanap_running){
        wifictl_scan_nearby_aps();
        print_ap_list_flipper_band();
        vTaskDelay(pdMS_TO_TICKS(1700));
    }
    scanap_task_handle = NULL;
    vTaskDelete(NULL);
}

static void sanitize_command(char *dst, const uint8_t *src, size_t maxlen){
    size_t pos = 0;
    for(size_t i = 0; src[i] && pos < maxlen - 1; ++i){
        if(isalnum(src[i]) || src[i] == '_' || src[i] == '-' || src[i] == '.'){
            dst[pos++] = src[i];
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
                    if(strcmp(command, "scanap") == 0){
                        if(!scanap_running){
                            scanap_running = true;
                            xTaskCreate(scanap_loop_task, "scanap_loop", 4096, NULL, 5, &scanap_task_handle);
                            printf("Continuous scan started (scanap).\n");
                        } else {
                            printf("Scan already running.\n");
                        }
                    } else if(strcmp(command, "stopscan") == 0){
                        scanap_running = false;
                        printf("Scan stopped.\n");
                    } else if(strcmp(command, "help") == 0){
                        printf("Available commands:\n");
                        printf("  scanap   - Continuous AP scan\n");
                        printf("  stopscan - Stop AP scan\n");
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

void app_main(void)
{
    ESP_LOGD(TAG, "app_main started");
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
    webserver_run();

    xTaskCreate(cli_task, "cli_task", 4096, NULL, 5, NULL);
}
