#include "web/captive_portal.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/sockets.h"
#include "esp_log.h"

#include <string.h>

#define TAG      "captive_dns"
#define DNS_PORT 53
#define BUF_LEN  512

/* All DNS queries are answered with the AP gateway IP. */
static const uint8_t AP_IP[4] = {192, 168, 4, 1};

static void dns_task(void *arg) {
    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0) {
        ESP_LOGE(TAG, "socket() failed");
        vTaskDelete(NULL);
        return;
    }

    struct sockaddr_in addr = {
        .sin_family      = AF_INET,
        .sin_port        = htons(DNS_PORT),
        .sin_addr.s_addr = htonl(INADDR_ANY),
    };
    if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        ESP_LOGE(TAG, "bind() failed on port %d", DNS_PORT);
        close(sock);
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "Captive DNS ready on UDP:%d → 192.168.4.1", DNS_PORT);

    uint8_t buf[BUF_LEN];
    struct sockaddr_in client;
    socklen_t client_len = sizeof(client);

    while (1) {
        int len = recvfrom(sock, buf, sizeof(buf), 0,
                           (struct sockaddr *)&client, &client_len);
        if (len < 12) continue;   /* too short for a valid DNS header */

        /* Reuse the query buffer as the response.
           Flags: QR=1 (response), AA=1 (authoritative), RCODE=0 (no error). */
        buf[2] = 0x84;
        buf[3] = 0x00;
        /* ANCOUNT = 1, NSCOUNT = 0, ARCOUNT = 0 */
        buf[6] = 0x00; buf[7] = 0x01;
        buf[8] = 0x00; buf[9] = 0x00;
        buf[10] = 0x00; buf[11] = 0x00;

        /* Skip past the question section to find where to append the answer. */
        uint8_t *p   = buf + 12;
        uint8_t *end = buf + len;
        while (p < end && *p != 0) p += *p + 1;  /* skip DNS name labels */
        if (p >= end) continue;
        p++;       /* null label terminator */
        p += 4;    /* QTYPE (2) + QCLASS (2) */

        if ((p + 16) > (buf + BUF_LEN)) continue;  /* overflow guard */

        /* Append A-record answer:
             NAME     0xC00C – pointer to question name at offset 12
             TYPE     A  (0x0001)
             CLASS    IN (0x0001)
             TTL      60 s
             RDLENGTH 4
             RDATA    192.168.4.1                  */
        *p++ = 0xC0; *p++ = 0x0C;
        *p++ = 0x00; *p++ = 0x01;
        *p++ = 0x00; *p++ = 0x01;
        *p++ = 0x00; *p++ = 0x00; *p++ = 0x00; *p++ = 0x3C;
        *p++ = 0x00; *p++ = 0x04;
        memcpy(p, AP_IP, 4); p += 4;

        sendto(sock, buf, p - buf, 0,
               (struct sockaddr *)&client, client_len);
    }

    close(sock);
    vTaskDelete(NULL);
}

void captive_portal_dns_start(void) {
    xTaskCreate(dns_task, "captive_dns", 4096, NULL, 5, NULL);
}
