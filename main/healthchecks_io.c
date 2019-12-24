/* HTTPS GET Example using plain mbedTLS sockets
 *
 * Contacts the howsmyssl.com API via TLS v1.2 and reads a JSON
 * response.
 *
 * Original Copyright (C) 2006-2016, ARM Limited, All Rights Reserved, Apache 2.0 License.
 * Additions Copyright (C) Copyright 2015-2016 Espressif Systems (Shanghai) PTE LTD, Apache 2.0 License.
 *
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_system.h"

#include "lwip/err.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include "lwip/netdb.h"
#include "lwip/dns.h"

#include "push_message.h"

#define DELAY_AFTER_ERROR ((30* 1000) / portTICK_PERIOD_MS)

#define WEB_SERVER "hc-ping.com"
#define WEB_PORT   "80"
#define WEB_URL    "http://hc-ping.com/5a4bbed5-162e-49e9-9c33-cd901a80a690"

static const char REQUEST[] = "GET " WEB_URL " HTTP/1.0\r\n"
"Host: "WEB_SERVER"\r\n"
"User-Agent: esp-idf/1.0 esp32\r\n"
"\r\n";


QueueHandle_t hHealthCheckControlQueue = NULL;
TimerHandle_t xHealthCheckTimer = NULL;

static void healthcheck_task(void *pvParameters)
{
    const struct addrinfo hints = {
        .ai_family = AF_INET,
        .ai_socktype = SOCK_STREAM,
    };
    struct addrinfo *res;
    struct in_addr *addr;
    int s, r;
    char recv_buf[64];

    vTaskDelay(pdMS_TO_TICKS( 10UL * 1000UL)); // wait a little bit so we are connected to the WiFi

    while( 1 )
    {
        // wait for next healthcheck trigger comming from the timer
        ESP_LOGI(__func__, "waiting for trigger");
        uint8_t recv = '?';
        xQueueReceive( hHealthCheckControlQueue,
                       &recv,
                       portMAX_DELAY );
        ESP_LOGI(__func__, "got new trigger");

        int err = getaddrinfo(WEB_SERVER, "80", &hints, &res);
        
        if(err != 0 || res == NULL) {
            ESP_LOGE(__func__, "DNS lookup failed err=%d res=%p", err, res);
            vTaskDelay(DELAY_AFTER_ERROR);
            continue;
        }
        
        /* Code to print the resolved IP.
         Note: inet_ntoa is non-reentrant, look at ipaddr_ntoa_r for "real" code */
        addr = &((struct sockaddr_in *)res->ai_addr)->sin_addr;
        ESP_LOGI(__func__, "DNS lookup succeeded. IP=%s", inet_ntoa(*addr));
        
        s = socket(res->ai_family, res->ai_socktype, 0);
        if(s < 0) {
            ESP_LOGE(__func__, "... Failed to allocate socket.");
            freeaddrinfo(res);
            vTaskDelay(DELAY_AFTER_ERROR);
            continue;
        }
        ESP_LOGI(__func__, "... allocated socket");
        
        if(connect(s, res->ai_addr, res->ai_addrlen) != 0) {
            ESP_LOGE(__func__, "... socket connect failed errno=%d", errno);
            close(s);
            freeaddrinfo(res);
            vTaskDelay(DELAY_AFTER_ERROR);
            continue;
        }
        
        ESP_LOGI(__func__, "... connected");
        freeaddrinfo(res);
        
        if (write(s, REQUEST, strlen(REQUEST)) < 0) {
            ESP_LOGE(__func__, "... socket send failed");
            close(s);
            vTaskDelay(DELAY_AFTER_ERROR);
            continue;
        }
        ESP_LOGI(__func__, "... socket send success");
        
        struct timeval receiving_timeout;
        receiving_timeout.tv_sec = 5;
        receiving_timeout.tv_usec = 0;
        if (setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &receiving_timeout,
                       sizeof(receiving_timeout)) < 0) {
            ESP_LOGE(__func__, "... failed to set socket receiving timeout");
            close(s);
            vTaskDelay(DELAY_AFTER_ERROR);
            continue;
        }
        ESP_LOGI(__func__, "... set socket receiving timeout success");
        
        /* Read HTTP response */
        do {
            bzero(recv_buf, sizeof(recv_buf));
            r = read(s, recv_buf, sizeof(recv_buf)-1);
            for(int i = 0; i < r; i++) {
                putchar(recv_buf[i]);
            }
        } while(r > 0);
        
        ESP_LOGI(__func__, "... done reading from socket. Last read return=%d errno=%d\r\n", r, errno);
        close(s);
    }
}


void healthCheckTrigger( void )
{
    uint8_t message = 'T';
    xQueueSend( hHealthCheckControlQueue,
               &message,
               pdMS_TO_TICKS(5) );
}


void healthchecksTaskStart(int prio)
{
    hHealthCheckControlQueue = xQueueCreate( 5, sizeof ( uint8_t ) );
    if ( NULL == hHealthCheckControlQueue )
    {
        ESP_LOGE(__func__, "hHealthCheckControlQueue create failed");
        return;
    }
    vQueueAddToRegistry( hHealthCheckControlQueue, "HealthChecksCtrl" );
    
    xTaskCreate(&healthcheck_task, "healthcheck_task", 8192, NULL, prio, NULL);
}

