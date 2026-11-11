/**
 * @file captive_portal.c
 * @brief DNS responder task + mDNS. See captive_portal.h.
 *
 * Dependencies: lwip sockets, mdns, dns_reply. Threading: one task (dns), mDNS runs its own task.
 */
#include "captive_portal.h"

#include <string.h>

#include "dns_reply.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/sockets.h"
#include "mdns.h"

static const char *TAG = "captive";

/** UDP port of DNS. */
#define DNS_PORT 53
/** DNS task stack size in bytes (receive buffer lives on it). */
#define DNS_TASK_STACK 4096
/** DNS task priority (low: it must never disturb HID forwarding). */
#define DNS_TASK_PRIO 3
/** Interface key of the default SoftAP netif. */
#define AP_NETIF_KEY "WIFI_AP_DEF"
/** HTTP port advertised through mDNS. */
#define HTTP_PORT 80

/**
 * @brief DNS responder loop.
 *
 * @param arg Unused.
 */
static void dns_task(void *arg) {
    (void)arg;
    /* The AP IP is fixed (192.168.4.1) but read from the netif so a future change needs no edit here. */
    esp_netif_ip_info_t info = {0};
    esp_netif_t *netif = esp_netif_get_handle_from_ifkey(AP_NETIF_KEY);
    if (netif == NULL || esp_netif_get_ip_info(netif, &info) != ESP_OK) {
        ESP_LOGE(TAG, "no AP netif; DNS responder not started");
        vTaskDelete(NULL);
        return;
    }
    uint8_t ip[4];
    memcpy(ip, &info.ip.addr, 4); /* lwIP stores addresses in network byte order */

    const int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    struct sockaddr_in addr = {
        .sin_family = AF_INET,
        .sin_port = htons(DNS_PORT),
        .sin_addr.s_addr = htonl(INADDR_ANY),
    };
    if (sock < 0 || bind(sock, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        ESP_LOGE(TAG, "cannot bind DNS socket");
        if (sock >= 0) {
            close(sock);
        }
        vTaskDelete(NULL);
        return;
    }

    uint8_t rx[DNS_MAX_MESSAGE];
    uint8_t tx[DNS_MAX_MESSAGE + 16];
    for (;;) {
        struct sockaddr_in from;
        socklen_t from_len = sizeof(from);
        const int n = recvfrom(sock, rx, sizeof(rx), 0, (struct sockaddr *)&from, &from_len);
        if (n <= 0) {
            continue;
        }
        const size_t out = dns_build_reply(rx, (size_t)n, ip, tx, sizeof(tx));
        if (out > 0) {
            sendto(sock, tx, out, 0, (struct sockaddr *)&from, from_len);
        }
    }
}

/** @copydoc captive_portal_start */
esp_err_t captive_portal_start(void) {
    ESP_RETURN_ON_ERROR(mdns_init(), TAG, "mdns init");
    ESP_RETURN_ON_ERROR(mdns_hostname_set(CAPTIVE_PORTAL_HOSTNAME), TAG, "mdns hostname");
    ESP_RETURN_ON_ERROR(mdns_instance_name_set("HubBLE"), TAG, "mdns instance");
    ESP_RETURN_ON_ERROR(mdns_service_add(NULL, "_http", "_tcp", HTTP_PORT, NULL, 0), TAG, "mdns service");
    BaseType_t ok = xTaskCreate(dns_task, "dns", DNS_TASK_STACK, NULL, DNS_TASK_PRIO, NULL);
    ESP_RETURN_ON_FALSE(ok == pdPASS, ESP_ERR_NO_MEM, TAG, "dns task");
    return ESP_OK;
}
