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
        printf("[%d] Band: %s RSSI: %d Ch: %d BSSID: %02x:%02x:%02x:%02x:%02x:%02x ESSID: %s\n",
               i,
               get_band_from_channel(rec->primary),
               rec->rssi,
               rec->primary,
               rec->bssid[0], rec->bssid[1], rec->bssid[2],
               rec->bssid[3], rec->bssid[4], rec->bssid[5],
               rec->ssid[0] ? (char *)rec->ssid : "<hidden>");
    }
}

static void scan_loop_task(void *pv){
    while(scan_running){
        wifictl_scan_nearby_aps();
        if(!scan_running){
            break;
        }
        print_ap_list_flipper_band();
        vTaskDelay(pdMS_TO_TICKS(1700));
    }
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
    printf("Attack stopped.\n");
}

static void sanitize_command(char *dst, const uint8_t *src, size_t maxlen){
    size_t pos = 0;
    for(size_t i = 0; src[i] && pos < maxlen - 1; ++i){
        if(isalnum(src[i]) || src[i] == '_' || src[i] == '-' ||
           src[i] == '.' || isspace((unsigned char)src[i])){
            dst[pos++] = src[i];
        } else if(src[i] == ',' || src[i] == ':' || src[i] == ';'){
            /* Treat common separators as spaces so "atack 1,2" works */
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
                            printf("Continuous scan started.\n");
                        } else {
                            printf("Scan already running.\n");
                        }
                    } else if(strcmp(command, "scanstop") == 0){
                        scan_running = false;
                        printf("Scan stopped.\n");
                    } else if(strcmp(command, "atackstop") == 0){
                        cli_stop_attack();
                        attack_dos_stop();
                        printf("Atack stopped.\n");
                    } else if(strncmp(command, "atack", 5) == 0){
                        int ids[10];
                        int count = 0;
                        char *ptr = command + 5;
                        while(*ptr && count < 10){
                            while(*ptr && isspace((unsigned char)*ptr)) ptr++;
                            if(!*ptr) break;
                            ids[count++] = atoi(ptr);
                            while(*ptr && !isspace((unsigned char)*ptr)) ptr++;
                        }
                        cli_start_attack(ids, count);
                    } else if(strcmp(command, "help") == 0){
                        printf("Available commands:\n");
                        printf("  scan     - Continuous AP scan\n");
                        printf("  scanstop - Stop AP scan\n");
                        printf("  atack N [M ...] - Attack AP indexes\n");
                        printf("  atackstop - Stop running attack\n");
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
    // Initialize NVS to store Wi-Fi calibration data
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
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
