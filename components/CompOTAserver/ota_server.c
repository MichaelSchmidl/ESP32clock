//https://github.com/yanbe/esp-idf-ota-template/blob/master/components/ota_server/ota_server.c
	
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_event.h"
#include <esp_system.h>
#include <nvs_flash.h>

#include "ota_server.h"
#include "esp_wifi.h"
#include "esp_system.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "lwip/sockets.h"

#include "7segMultiplex.h"
#include "push_message.h"

/*socket*/
static int connect_socket = 0;

static int get_socket_error_code(int socket)
{
    int result;
    u32_t optlen = sizeof(int);
	
    int err = getsockopt(socket, SOL_SOCKET, SO_ERROR, &result, &optlen);
	
    if (err == -1) 
    {
        ESP_LOGE(__func__, "getsockopt failed:%s", strerror(err));
        return -1;
    }
    return result;
}

static int show_socket_error_reason(const char *str, int socket)
{
    int err = get_socket_error_code(socket);

    if (err != 0) 
    {
        ESP_LOGW(__func__, "%s socket error %d %s", str, err, strerror(err));
    }

    return err;
}

static esp_err_t create_tcp_server()
{
    ESP_LOGI(__func__, "server socket....port=%d", OTA_LISTEN_PORT);
    int server_socket = 0;
    struct sockaddr_in server_addr;
    server_socket = socket(AF_INET, SOCK_STREAM, 0);
	
    if (server_socket < 0) 
    {
        show_socket_error_reason("create_server", server_socket);
        return ESP_FAIL;
    }

    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(OTA_LISTEN_PORT);
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(server_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) 
    {
        show_socket_error_reason("bind_server", server_socket);
        close(server_socket);
        return ESP_FAIL;
    }
	
    if (listen(server_socket, 5) < 0) 
    {
        show_socket_error_reason("listen_server", server_socket);
        close(server_socket);
        return ESP_FAIL;
    }
	
    struct sockaddr_in client_addr;
    unsigned int socklen = sizeof(client_addr);
    connect_socket = accept(server_socket, (struct sockaddr *)&client_addr, &socklen);
	
    if (connect_socket < 0) 
    {
        show_socket_error_reason("accept_server", connect_socket);
        close(server_socket);
        return ESP_FAIL;
    }
    /*connection established，now can send/recv*/
    ESP_LOGI(__func__, "tcp connection established!");
    return ESP_OK;
}


void ota_server_task(void *param)
{
    vTaskDelay(pdMS_TO_TICKS( 15 * 1000UL )); // wait for TCPIP up and running before creating the OTA task

    ESP_ERROR_CHECK( create_tcp_server() );
	
    const esp_partition_t *update_partition = esp_ota_get_next_update_partition(NULL);

    ESP_LOGI(__func__, "Writing to partition subtype %d at offset 0x%x",	update_partition->subtype, update_partition->address);

	//https://docs.espressif.com/projects/esp-idf/en/latest/api-reference/system/log.html
	// I dont want to see all the Log esp_image stuff while its flashing it
	esp_log_level_set("esp_image", ESP_LOG_ERROR);           // set all components to ERROR level  ESP_LOG_NONE


    int recv_len;
    char ota_buff[OTA_BUFF_SIZE] = {0};
    bool is_req_body_started = false;
    int content_length = -1;
    int content_received = 0;

    esp_ota_handle_t ota_handle; 

	uint8_t percent_loaded = 255;
	uint8_t last_percent_loaded = 255;

    do {
        recv_len = recv(connect_socket, ota_buff, OTA_BUFF_SIZE, 0);
	    
        if (recv_len > 0) 
        {
            if (!is_req_body_started) 
            {
                const char *content_length_start = "Content-Length: ";
                char *content_length_start_p = strstr(ota_buff, content_length_start) + strlen(content_length_start);
                sscanf(content_length_start_p, "%d", &content_length);
                ESP_LOGI(__func__, "Detected content length: %d", content_length);
                ESP_ERROR_CHECK( esp_ota_begin(update_partition, OTA_SIZE_UNKNOWN, &ota_handle) );
                const char *header_end = "\r\n\r\n";
                char *body_start_p = strstr(ota_buff, header_end) + strlen(header_end);
                int body_part_len = recv_len - (body_start_p - ota_buff);
                esp_ota_write(ota_handle, body_start_p, body_part_len);
                content_received += body_part_len;
                is_req_body_started = true;
            }
	        else 
	        {
                esp_ota_write(ota_handle, ota_buff, recv_len);
                content_received += recv_len;

		        percent_loaded = (content_received * 100 ) / content_length;
		        if ( last_percent_loaded != percent_loaded )
		        {
		        	ESP_LOGI(__func__, "Uploaded %3u%%", percent_loaded);
		            multiplex_setTime( "Lo:Ad" );
		        	vTaskDelay(1);
		        }
		        last_percent_loaded = percent_loaded;
		    }
        }
        else if (recv_len < 0) 
        {
	        ESP_LOGI(__func__, "Error: recv data error! errno=%d", errno);
        }

    } while (recv_len > 0 && content_received < content_length);

//	ESP_LOGI(TAG, "OTA Transferred Finished: %d bytes", content_received);
	
	
	char res_buff[128];
	int send_len;
	send_len = sprintf(res_buff, "200 OK\n\n");
	send(connect_socket, res_buff, send_len, 0);
	vTaskDelay(2000 / portTICK_PERIOD_MS);
	close(connect_socket);

    ESP_ERROR_CHECK( esp_ota_end(ota_handle) );
	
    esp_err_t err = esp_ota_set_boot_partition(update_partition);
	
	if (err == ESP_OK) 
	{
		const esp_partition_t *boot_partition = esp_ota_get_boot_partition();
	
		ESP_LOGI(__func__, "***********************************************************");
		ESP_LOGI(__func__, "OTA Successful");
		ESP_LOGI(__func__, "Next Boot Partition Subtype %d At Offset 0x%x", boot_partition->subtype, boot_partition->address);
		ESP_LOGI(__func__, "***********************************************************");
	}
	else
	{
		ESP_LOGI(__func__, "!!! OTA Failed !!!");
	}

    ESP_LOGI(__func__, "Restarting system...");
    vTaskDelay( pdMS_TO_TICKS( 3000UL ) );
    esp_restart();
}
