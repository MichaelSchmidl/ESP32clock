 /* HTTPS GET Example using plain mbedTLS sockets
 *
 * Contacts the howsmyssl.com API via TLS v1.2 and reads a JSON
 * response.
 *
 * Adapted from the ssl_client1 example in mbedtls.
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

#include "mbedtls/platform.h"
#include "mbedtls/net.h"
#include "mbedtls/esp_debug.h"
#include "mbedtls/ssl.h"
#include "mbedtls/entropy.h"
#include "mbedtls/ctr_drbg.h"
#include "mbedtls/error.h"
#include "mbedtls/certs.h"

#include "push_message.h"
#include "clockTask.h"

#define NOTIFY_EVERY_N_MINUTES (24 * 60)

#define SECS_PER_DAY (60 * 60 * 24)

#define WEB_SERVER "api.pushover.net"
#define WEB_PORT   "443"
#define WEB_URL    "https://api.pushover.net/1/messages.json"
#define PUSH_TOKEN "abn493y7ofogu2m7mi4suckmtzmvq7"
#define PUSH_USER  "uih41aopihno8aqb1d5nifjm8x44bb"

static const char REQUEST_HEADER[] = "POST " WEB_URL " HTTP/1.0\r\n"
"Host: "WEB_SERVER"\r\n"
"Content-Type: application/x-www-form-urlencoded\r\n"
"Content-Length: %d\r\n"
"\r\n%s";

static const char CONTENT_HEADER[] = "token=" PUSH_TOKEN "&"
"user=" PUSH_USER "&"
"title=%s&"
"message=%s";


/* Root cert for pushover.com, taken from server_root_cert.pem
 
 The PEM file was extracted from the output of this command:
 openssl s_client -showcerts -connect www.pushover.com:443 </dev/null
 
 The CA root cert is the last cert given in the chain of certs.
 
 To embed it in the app binary, the PEM file is named
 in the component.mk COMPONENT_EMBED_TXTFILES variable.
 */
extern const uint8_t pushover_root_cert_pem_start[] asm("_binary_pushover_root_cert_pem_start");
extern const uint8_t pushover_root_cert_pem_end[]   asm("_binary_pushover_root_cert_pem_end");

QueueHandle_t hNotifyControlQueue = NULL;

static void notification_task(void *pvParameters)
{
    static time_t start_time = 0;
    static uint8_t recv = '?';

    char buf[512];
    int ret, flags, len;
    
    mbedtls_entropy_context entropy;
    mbedtls_ctr_drbg_context ctr_drbg;
    mbedtls_ssl_context ssl;
    mbedtls_x509_crt cacert;
    mbedtls_ssl_config conf;
    mbedtls_net_context server_fd;
    
    mbedtls_ssl_init(&ssl);
    mbedtls_x509_crt_init(&cacert);
    mbedtls_ctr_drbg_init(&ctr_drbg);
    ESP_LOGI(__func__, "Seeding the random number generator");
    
    mbedtls_ssl_config_init(&conf);
    
    mbedtls_entropy_init(&entropy);
    if((ret = mbedtls_ctr_drbg_seed(&ctr_drbg, mbedtls_entropy_func, &entropy,
                                    NULL, 0)) != 0)
    {
        ESP_LOGE(__func__, "mbedtls_ctr_drbg_seed returned %d", ret);
        abort();
    }
    
    ESP_LOGI(__func__, "Loading the CA root certificate...");
    
    ret = mbedtls_x509_crt_parse(&cacert, pushover_root_cert_pem_start,
                                 pushover_root_cert_pem_end - pushover_root_cert_pem_start);
    
    if(ret < 0)
    {
        ESP_LOGE(__func__, "mbedtls_x509_crt_parse returned -0x%x\n\n", -ret);
        abort();
    }
    
    ESP_LOGI(__func__, "Setting hostname for TLS session...");
    
    /* Hostname set here should match CN in server certificate */
    if((ret = mbedtls_ssl_set_hostname(&ssl, WEB_SERVER)) != 0)
    {
        ESP_LOGE(__func__, "mbedtls_ssl_set_hostname returned -0x%x", -ret);
        abort();
    }
    
    ESP_LOGI(__func__, "Setting up the SSL/TLS structure...");
    
    if((ret = mbedtls_ssl_config_defaults(&conf,
                                          MBEDTLS_SSL_IS_CLIENT,
                                          MBEDTLS_SSL_TRANSPORT_STREAM,
                                          MBEDTLS_SSL_PRESET_DEFAULT)) != 0)
    {
        ESP_LOGE(__func__, "mbedtls_ssl_config_defaults returned %d", ret);
        goto exit;
    }
    
    /* MBEDTLS_SSL_VERIFY_OPTIONAL is bad for security, in this example it will print
     a warning if CA verification fails but it will continue to connect.
     
     You should consider using MBEDTLS_SSL_VERIFY_REQUIRED in your own code.
     */
    mbedtls_ssl_conf_authmode(&conf, MBEDTLS_SSL_VERIFY_OPTIONAL);
    mbedtls_ssl_conf_ca_chain(&conf, &cacert, NULL);
    mbedtls_ssl_conf_rng(&conf, mbedtls_ctr_drbg_random, &ctr_drbg);
#ifdef CONFIG_MBEDTLS_DEBUG
    mbedtls_esp_enable_debug_log(&conf, 4);
#endif
    
    if ((ret = mbedtls_ssl_setup(&ssl, &conf)) != 0)
    {
        ESP_LOGE(__func__, "mbedtls_ssl_setup returned -0x%x\n\n", -ret);
        goto exit;
    }

    while(1) {
        ESP_LOGI(__func__, "wait for a message to send");
        xQueueReceive( hNotifyControlQueue,
                       &recv,
                       portMAX_DELAY );
        ESP_LOGI(__func__, "got new message to send");

        mbedtls_net_init(&server_fd);
        
        ESP_LOGI(__func__, "Connecting to %s:%s...", WEB_SERVER, WEB_PORT);
        
        if ((ret = mbedtls_net_connect(&server_fd, WEB_SERVER,
                                       WEB_PORT, MBEDTLS_NET_PROTO_TCP)) != 0)
        {
            ESP_LOGE(__func__, "mbedtls_net_connect returned -%x", -ret);
            goto exit;
        }
        
        ESP_LOGI(__func__, "Connected.");
        
        mbedtls_ssl_set_bio(&ssl, &server_fd, mbedtls_net_send, mbedtls_net_recv, NULL);
        
        ESP_LOGI(__func__, "Performing the SSL/TLS handshake...");
        
        while ((ret = mbedtls_ssl_handshake(&ssl)) != 0)
        {
            if (ret != MBEDTLS_ERR_SSL_WANT_READ && ret != MBEDTLS_ERR_SSL_WANT_WRITE)
            {
                ESP_LOGE(__func__, "mbedtls_ssl_handshake returned -0x%x", -ret);
                goto exit;
            }
        }
        
        ESP_LOGI(__func__, "Verifying peer X.509 certificate...");
        
        if ((flags = mbedtls_ssl_get_verify_result(&ssl)) != 0)
        {
            /* In real life, we probably want to close connection if ret != 0 */
            ESP_LOGW(__func__, "Failed to verify peer certificate!");
            bzero(buf, sizeof(buf));
            mbedtls_x509_crt_verify_info(buf, sizeof(buf), "  ! ", flags);
            ESP_LOGW(__func__, "verification info: %s", buf);
        }
        else {
            ESP_LOGI(__func__, "Certificate verified.");
        }
        
        ESP_LOGI(__func__, "creating message text...");
        time_t now;
        struct tm timeinfo;
        char strftime_buf[80];
        time(&now);
        if (0 == start_time) start_time = now;
        setenv("TZ", "CET-1CEST,M3.5.0/2,M10.5.0/3", 1);
        tzset();
        localtime_r(&now, &timeinfo);
        strftime(strftime_buf, sizeof(strftime_buf), "%X", &timeinfo);
        strftime_buf[5] = '\0';

        tcpip_adapter_ip_info_t ipInfo;
        char ip_add[20];
        // IP address.
        tcpip_adapter_get_ip_info(TCPIP_ADAPTER_IF_STA, &ipInfo);
        snprintf(ip_add, sizeof(ip_add), "%d.%d.%d.%d",
        		 (ipInfo.ip.addr >>  0) & 0x0FF,
        		 (ipInfo.ip.addr >>  8) & 0x0FF,
        		 (ipInfo.ip.addr >> 16) & 0x0FF,
        		 (ipInfo.ip.addr >> 24) & 0x0FF );

        char *szHostname;
        tcpip_adapter_get_hostname(TCPIP_ADAPTER_IF_STA, (const char**)&szHostname);

        char msg_infotext[500];
        time_t up_time = (now -start_time);
        if (0 == up_time)
        {
            snprintf(msg_infotext, sizeof(msg_infotext),
            		 "Firmware %s %s started @ %s",
					 __DATE__,
					 __TIME__,
					 strftime_buf);
        }
        else if (SECS_PER_DAY < up_time)
        {
            snprintf(msg_infotext, sizeof(msg_infotext),
                    "%s (%s)[%s], %d free, up %d days",
                    szHostname,
					ip_add,
					strftime_buf,
					heap_caps_get_free_size(MALLOC_CAP_8BIT),
                    (int)(up_time / SECS_PER_DAY));
        }
        else{
            snprintf(msg_infotext, sizeof(msg_infotext),
                    "%s (%s) [%s], %d free, up %d:%02d:%02d",
                    szHostname,
					ip_add,
					strftime_buf,
					heap_caps_get_free_size(MALLOC_CAP_8BIT),
                    (int)(up_time / (60*60)),
                    (int)((up_time % (60*60)) / 60),
                    (int)(up_time % 60));
        }
        char msg_titletext[50];
        switch( recv )
        {
           case 'A':
        	   snprintf( msg_titletext, sizeof(msg_titletext),
        			     "ESP32clock alive");
        	   break;
           case 'S':
        	   snprintf( msg_titletext, sizeof(msg_titletext),
        			     "ESP32clock started");
        	   break;
           case 'D':
        	   snprintf( msg_titletext, sizeof(msg_titletext),
        			     "ESP32clock dimmed");
        	   break;
           case 'd':
        	   snprintf( msg_titletext, sizeof(msg_titletext),
        			     "ESP32clock fullbright");
        	   break;
           default:
        	   snprintf( msg_titletext, sizeof(msg_titletext),
        			     "ESP32clock <%c>", recv);
        	   break;
        }

        ESP_LOGI(__func__, "replace blanks with plus sign...");
        char *p = NULL;
        while (NULL != (p = strchr(msg_infotext, ' ')))
        {
            *p = '+';
        }
        p = NULL;
        while (NULL != (p = strchr(msg_titletext, ' ')))
        {
            *p = '+';
        }

        char msg_content[1024];
        sprintf(msg_content, CONTENT_HEADER, msg_titletext, msg_infotext);
        char msg_payload[2048];
        sprintf(msg_payload, REQUEST_HEADER, strlen(msg_content), msg_content);
        ESP_LOGI(__func__, "Writing HTTP request %s...", msg_payload);
        while((ret = mbedtls_ssl_write(&ssl, (const unsigned char *)msg_payload, strlen(msg_payload))) <= 0)
        {
            if(ret != MBEDTLS_ERR_SSL_WANT_READ && ret != MBEDTLS_ERR_SSL_WANT_WRITE)
            {
                ESP_LOGE(__func__, "mbedtls_ssl_write returned -0x%x", -ret);
                goto exit;
            }
        }
        
        len = ret;
        ESP_LOGI(__func__, "%d bytes written", len);
        ESP_LOGI(__func__, "Reading HTTP response...");
        
        do
        {
            len = sizeof(buf) - 1;
            bzero(buf, sizeof(buf));
            ret = mbedtls_ssl_read(&ssl, (unsigned char *)buf, len);
            
            if(ret == MBEDTLS_ERR_SSL_WANT_READ || ret == MBEDTLS_ERR_SSL_WANT_WRITE)
                continue;
            
            if(ret == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY) {
                ret = 0;
                break;
            }
            
            if(ret < 0)
            {
                ESP_LOGE(__func__, "mbedtls_ssl_read returned -0x%x", -ret);
                break;
            }
            
            if(ret == 0)
            {
                ESP_LOGI(__func__, "connection closed");
                break;
            }
            
            len = ret;
            ESP_LOGI(__func__, "%d bytes read", len);
            /* Print response directly to stdout as it is read */
            for(int i = 0; i < len; i++) {
                putchar(buf[i]);
            }
        } while(1);
        
        mbedtls_ssl_close_notify(&ssl);
        
    exit:
        mbedtls_ssl_session_reset(&ssl);
        mbedtls_net_free(&server_fd);
        
        if(ret != 0)
        {
            mbedtls_strerror(ret, buf, 100);
            ESP_LOGE(__func__, "Last error was: -0x%x - %s", -ret, buf);
        }
        
        ESP_LOGI(__func__, "wait until next notification is due...");
    }
}


void notificationTask_sendMessage( uint8_t message )
{
    xQueueSend( hNotifyControlQueue,
               &message,
               pdMS_TO_TICKS(5) );
}


void notificationTaskStart(int prio)
{
    hNotifyControlQueue = xQueueCreate( 5, sizeof ( uint8_t ) );
    if ( NULL == hNotifyControlQueue )
    {
        ESP_LOGE(__func__, "hNotifyControlQueue create failed");
        return;
    }
    vQueueAddToRegistry( hNotifyControlQueue, "NOTIFYctrl" );
    
    xTaskCreate(&notification_task, "notification_task", 16000, NULL, prio, NULL);
}

