#include <stdio.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_attr.h"
#include "esp_sleep.h"
#include "nvs_flash.h"

#include "hardware.h"
#include "push_message.h"
#include "healthchecks_io.h"
#include "protocol_examples_common.h"
#include "7segMultiplex.h"
#include "esp_sntp.h"

#include "daylightCalc.h"


static void initialize_sntp(void);

/////////////////////////////////////////////////////////////////////////////////////////////////
#ifdef CONFIG_SNTP_TIME_SYNC_METHOD_CUSTOM
#error CONFIG_SNTP_TIME_SYNC_METHOD_CUSTOM not supported
#endif


/////////////////////////////////////////////////////////////////////////////////////////////////
void time_sync_notification_cb(struct timeval *tv)
{
	static uint8_t firstTime = 1;
    ESP_LOGI(__func__, "Notification of a time synchronization event");
    if ( firstTime ) notificationTask_sendMessage( 'S' );
    firstTime = 0;
    healthCheckTrigger();
}


/////////////////////////////////////////////////////////////////////////////////////////////////
static void initialize_sntp(void)
{
    ESP_LOGI(__func__, "Initializing SNTP");
    sntp_setoperatingmode(SNTP_OPMODE_POLL);
    sntp_setservername(0, "pool.ntp.org");
    sntp_set_time_sync_notification_cb(time_sync_notification_cb);
#ifdef CONFIG_SNTP_TIME_SYNC_METHOD_SMOOTH
    sntp_set_sync_mode(SNTP_SYNC_MODE_SMOOTH);
#endif
    sntp_init();
}


static uint8_t _doWeHaveAnIPAddr( void )
{
    tcpip_adapter_ip_info_t ipInfo;
    tcpip_adapter_get_ip_info(TCPIP_ADAPTER_IF_STA, &ipInfo);
    return ( 0 != ipInfo.ip.addr );
}


/////////////////////////////////////////////////////////////////////////////////////////////////
static void clockTask_workerFunction(void *p)
{
	static int currentDisplayDimming = -1;

	ESP_ERROR_CHECK( nvs_flash_init() );
    tcpip_adapter_init();
    ESP_ERROR_CHECK( esp_event_loop_create_default() );

    /* This helper function configures Wi-Fi or Ethernet, as selected in menuconfig.
     */
    ESP_ERROR_CHECK(myConnect());

    initialize_sntp();

    uint8_t firstTime = 1;
    TickType_t xLastWakeTime = xTaskGetTickCount();

    do {
        multiplex_setTime( "no AP" );
        vTaskDelay( pdMS_TO_TICKS( 1000UL ) );
    } while ( ! _doWeHaveAnIPAddr() );

    while(1) {
        time_t now;
        struct tm timeinfo;
        char strftime_buf[64];

        // update 'now' variable with current time
        time(&now);

        setenv("TZ", "CET-1CEST,M3.5.0/2,M10.5.0/3", 1);
        tzset();
        localtime_r(&now, &timeinfo);


        if ( ( timeinfo.tm_sec == 0 ) || (firstTime ) )
        {
        	if ( timeinfo.tm_year > (2019 - 1900) )
        	{
                int dimDisplayNow = isSunDown(48.1374300, 11.5754900, timeinfo);
                if ( currentDisplayDimming != dimDisplayNow )
                {
                	if ( dimDisplayNow )
                	{
                        notificationTask_sendMessage('D');
                	}
                	else
                	{
                        notificationTask_sendMessage('d');
                	}
                }
                currentDisplayDimming = dimDisplayNow;
#if 0
                strftime(strftime_buf, sizeof(strftime_buf), "%c", &timeinfo);
                ESP_LOGI(__func__, "it is %s", strftime_buf);
#endif

				// show it on 7-seg display
				strftime(strftime_buf, sizeof(strftime_buf), "%X", &timeinfo);
				if ( strftime_buf[0] == '0' ) strftime_buf[0] = ' ';
				strftime_buf[5] = '\0';
				if ( ! _doWeHaveAnIPAddr() )
				{
					snprintf( strftime_buf, sizeof(strftime_buf), "no:AP" );
				}
				multiplex_setTime( strftime_buf );

				// send ALIVE every day at 6:27
				if ( ( 6 == timeinfo.tm_hour ) && ( 27 == timeinfo.tm_min ) )
				{
					notificationTask_sendMessage('A');
				}


				if ( firstTime )
				{
					// wait a second so we catch the next minute for sure
					vTaskDelay(pdMS_TO_TICKS( 1000UL ));
				}
				else
				{
					// wait nearly a minute...
					vTaskDelayUntil( &xLastWakeTime, pdMS_TO_TICKS( 59UL * 1000UL ) );
				}

				firstTime = 0;
        	}
        }
        else
        {
        	// wait very short so we catch the minute exactly
            vTaskDelay(pdMS_TO_TICKS( 10UL ));
        }
    }
}


void clockTaskStart( int prio )
{
    xTaskCreate(clockTask_workerFunction, "clocktask", 8192, NULL, prio, NULL);
}
