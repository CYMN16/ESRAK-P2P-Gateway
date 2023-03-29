#include <stdio.h>
#include <stdlib.h>
#include <string.h> //Requires by memset
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_spi_flash.h"
#include <esp_http_server.h>

#include "esp_wifi.h"
#include "esp_event.h"
#include "freertos/event_groups.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "driver/gpio.h"
#include <lwip/sockets.h>
#include <lwip/sys.h>
#include <lwip/api.h>
#include <lwip/netdb.h>

#include "driver/uart.h"
#include "driver/gpio.h"
#include "sdkconfig.h"
#include <string.h>

#define LED_PIN 2

char html_page[] = "<!DOCTYPE HTML><html>\n"
                   "<head>\n"
                   "  <title>ESP-IDF ESP32 Web Server</title>\n"
                   "  <meta http-equiv=\"refresh\" content=\"10\">\n"
                   "  <meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">\n"
                   "  <link rel=\"stylesheet\" href=\"https://use.fontawesome.com/releases/v5.7.2/css/all.css\" integrity=\"sha384-fnmOCqbTlWIlj8LyTjo7mOUStjsKC4pOpQbqyi7RrhN7udi9RwhKkMHpvLbHG9Sr\" crossorigin=\"anonymous\">\n"
                   "  <link rel=\"icon\" href=\"data:,\">\n"
                   "  <style>\n"
                   "    html {font-family: Arial; display: inline-block; text-align: center;}\n"
                   "    p {  font-size: 1.2rem;}\n"
                   "    body {  margin: 0;}\n"
                   "    .topnav { overflow: hidden; background-color: #4B1D3F; color: white; font-size: 1.7rem; }\n"
                   "    .content { padding: 20px; }\n"
                   "    .card { background-color: white; box-shadow: 2px 2px 12px 1px rgba(140,140,140,.5); }\n"
                   "    .cards { max-width: 700px; margin: 0 auto; display: grid; grid-gap: 2rem; grid-template-columns: repeat(auto-fit, minmax(300px, 1fr)); }\n"
                   "    .reading { font-size: 2.8rem; }\n"
                   "    .card.loradata { color: #0e7c7b; }\n"
                   "  </style>\n"
                   "</head>\n"
                   "<body>\n"
                   "  <div class=\"topnav\">\n"
                   "    <h3>ESP-IDF ESP32 WEB SERVER</h3>\n"
                   "  </div>\n"
                   "  <div class=\"content\">\n"
                   "    <div class=\"cards\">\n"
                   "      <div class=\"card loradata\">\n"
                   "        <h4><i class=\"fas fa-database\"></i> LORA DATA</h4><p><span class=\"reading\">%50s</span></p>\n"
                   "      </div>\n"
                   "    </div>\n"
                   "  </div>\n"
                   "</body>\n"
                   "</html>";
static const char *TAG = "espressif"; // TAG for debug
int led_state = 0;

#define EXAMPLE_ESP_WIFI_SSID CONFIG_ESP_WIFI_SSID
#define EXAMPLE_ESP_WIFI_PASS CONFIG_ESP_WIFI_PASSWORD
#define EXAMPLE_ESP_MAXIMUM_RETRY CONFIG_ESP_MAXIMUM_RETRY

/* FreeRTOS event group to signal when we are connected*/
static EventGroupHandle_t s_wifi_event_group;

/* The event group allows multiple bits for each event, but we only care about two events:
 * - we are connected to the AP with an IP
 * - we failed to connect after the maximum amount of retries */
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT BIT1

static int s_retry_num = 0;

TaskHandle_t serverTaskHandle = NULL;
TaskHandle_t loraTaskHandle = NULL;

uint8_t *data;

// uart communication defines
#define ECHO_TEST_TXD (CONFIG_EXAMPLE_UART_TXD)
#define ECHO_TEST_RXD (CONFIG_EXAMPLE_UART_RXD)
#define ECHO_TEST_RTS (UART_PIN_NO_CHANGE)
#define ECHO_TEST_CTS (UART_PIN_NO_CHANGE)

#define ECHO_UART_PORT_NUM (CONFIG_EXAMPLE_UART_PORT_NUM)
#define ECHO_UART_BAUD_RATE (CONFIG_EXAMPLE_UART_BAUD_RATE)
#define ECHO_TASK_STACK_SIZE (CONFIG_EXAMPLE_TASK_STACK_SIZE)

#define BUF_SIZE (1024)

static void event_handler(void *arg, esp_event_base_t event_base,
                          int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START)
    {
        esp_wifi_connect();
    }
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED)
    {
        if (s_retry_num < EXAMPLE_ESP_MAXIMUM_RETRY)
        {
            esp_wifi_connect();
            s_retry_num++;
            ESP_LOGI(TAG, "retry to connect to the AP");
        }
        else
        {
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
        }
        ESP_LOGI(TAG, "connect to the AP fail");
    }
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP)
    {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "got ip:" IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_num = 0;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

void connect_wifi(void)
{
    s_wifi_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());

    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &event_handler,
                                                        NULL,
                                                        &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &event_handler,
                                                        NULL,
                                                        &instance_got_ip));

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = EXAMPLE_ESP_WIFI_SSID,
            .password = EXAMPLE_ESP_WIFI_PASS,
            /* Setting a password implies station will connect to all security modes including WEP/WPA.
             * However these modes are deprecated and not advisable to be used. Incase your Access point
             * doesn't support WPA2, these mode can be enabled by commenting below line */
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "wifi_init_sta finished.");

    /* Waiting until either the connection is established (WIFI_CONNECTED_BIT) or connection failed for the maximum
     * number of re-tries (WIFI_FAIL_BIT). The bits are set by event_handler() (see above) */
    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
                                           WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                                           pdFALSE,
                                           pdFALSE,
                                           portMAX_DELAY);

    /* xEventGroupWaitBits() returns the bits before the call returned, hence we can test which event actually
     * happened. */
    if (bits & WIFI_CONNECTED_BIT)
    {
        ESP_LOGI(TAG, "connected to ap SSID:%s password:%s",
                 EXAMPLE_ESP_WIFI_SSID, EXAMPLE_ESP_WIFI_PASS);
    }
    else if (bits & WIFI_FAIL_BIT)
    {
        ESP_LOGI(TAG, "Failed to connect to SSID:%s, password:%s",
                 EXAMPLE_ESP_WIFI_SSID, EXAMPLE_ESP_WIFI_PASS);
    }
    else
    {
        ESP_LOGE(TAG, "UNEXPECTED EVENT");
    }
    vEventGroupDelete(s_wifi_event_group);
}

esp_err_t send_web_page(httpd_req_t *req)
{
    int response;
    char response_data[sizeof(html_page) + 200];
    memset(response_data, 0, sizeof(response_data));
    printf("Passed memset\n");
    sprintf(response_data, html_page, (const char *)data);
    printf("Passed sprintf\n");

    response = httpd_resp_send(req, response_data , HTTPD_RESP_USE_STRLEN);

    return response;
}
esp_err_t get_req_handler(httpd_req_t *req)
{
    return send_web_page(req);
}

httpd_uri_t uri_get = {
    .uri = "/",
    .method = HTTP_GET,
    .handler = get_req_handler,
    .user_ctx = NULL};

httpd_handle_t setup_server(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    httpd_handle_t server = NULL;

    if (httpd_start(&server, &config) == ESP_OK)
    {
        httpd_register_uri_handler(server, &uri_get);
    }

    return server;
}

void task1_web_server()
{
    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_LOGI(TAG, "ESP_WIFI_MODE_STA");
    connect_wifi();

    // GPIO initialization
    esp_rom_gpio_pad_select_gpio(LED_PIN);
    gpio_set_direction(LED_PIN, GPIO_MODE_OUTPUT);

    led_state = 0;
    ESP_LOGI(TAG, "ESP32 Web Server is running ... ...\n");
    setup_server();
    while (1)
        vTaskDelay(10);
}

static void task2_lora_p2p(void *arg)
{
    /* Configure parameters of an UART driver,
     * communication pins and install the driver */
    uart_config_t uart_config = {
        .baud_rate = ECHO_UART_BAUD_RATE,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_APB,
    };
    int intr_alloc_flags = 0;

#if CONFIG_UART_ISR_IN_IRAM
    intr_alloc_flags = ESP_INTR_FLAG_IRAM;
#endif

    ESP_ERROR_CHECK(uart_driver_install(ECHO_UART_PORT_NUM, BUF_SIZE * 2, 0, 0, NULL, intr_alloc_flags));
    ESP_ERROR_CHECK(uart_param_config(ECHO_UART_PORT_NUM, &uart_config));
    ESP_ERROR_CHECK(uart_set_pin(ECHO_UART_PORT_NUM, ECHO_TEST_TXD, ECHO_TEST_RXD, ECHO_TEST_RTS, ECHO_TEST_CTS));

    // Configure a temporary buffer for the incoming data
    data = (uint8_t *)malloc(BUF_SIZE);

    int length = 0;
    ESP_ERROR_CHECK(uart_get_buffered_data_len(ECHO_UART_PORT_NUM, (size_t *)&length));
    length = uart_read_bytes(ECHO_UART_PORT_NUM, data, length, 10);

    if (length > 0)
    {
        printf("RAK message (len: %d) :%.*s\n", length, length, (const char *)data);
    }
    else
    {
        printf("No UART data received\n");
    }

    char *msg = "AT+NWM=0\r\n";
    uart_write_bytes(ECHO_UART_PORT_NUM, (const char *)msg, strlen(msg));

    printf("Uart message(len: %d): %s,\n", strlen(msg), (const char *)msg);

    msg = "AT+P2P=868000000:7:125:0:8:15\r\n";
    uart_write_bytes(ECHO_UART_PORT_NUM, (const char *)msg, strlen(msg));

    printf("Uart message(len: %d): %s,\n", strlen(msg), (const char *)msg);

    while (1)
    {

        // data = NULL;
        //  Read data from the UART
        // int len = uart_read_bytes(ECHO_UART_PORT_NUM, data, BUF_SIZE, 20 / portTICK_PERIOD_MS);
        // Write data back to the UART
        // uart_write_bytes(ECHO_UART_PORT_NUM, (const char *) data, len);
        // data[len+128] = '\0';

        int length = 0;
        ESP_ERROR_CHECK(uart_get_buffered_data_len(ECHO_UART_PORT_NUM, (size_t *)&length));
        length = uart_read_bytes(ECHO_UART_PORT_NUM, data, length, 10);

        if (length > 0)
        {

            // sprintf(lora_last_data_recv, "%.*s", length, (const char *)data);
            printf("RAK message (len: %d) :%.*s\n", length, length, (const char *)data);
        }
        else
        {
            printf("No UART data received\n");
        }
        uart_flush(ECHO_UART_PORT_NUM);

        // msg = "AT+P2P=868000000:7:125:0:8:15\r\n";
        // uart_write_bytes(ECHO_UART_PORT_NUM, (const char *)msg, strlen(msg));

        // msg = "AT+PSEND=4e4f5448454c4c4f\r\n";
        // uart_write_bytes(ECHO_UART_PORT_NUM, (const char *)msg, strlen(msg));
        // printf("Uart message(len: %d): %s,\n", strlen(msg), (const char *)msg);

        msg = "AT+PRECV=5000\r\n";
        uart_write_bytes(ECHO_UART_PORT_NUM, (const char *)msg, strlen(msg));
        printf("Uart message(len: %d): %s,\n", strlen(msg), (const char *)msg);

        vTaskDelay(5000 / portTICK_PERIOD_MS);
    }
}

void app_main()
{
    xTaskCreatePinnedToCore(task1_web_server, "Web_server_task", 4096, NULL, 10, &serverTaskHandle, 0);
    xTaskCreatePinnedToCore(task2_lora_p2p, "P2P_lora_task", ECHO_TASK_STACK_SIZE, NULL, 10, &loraTaskHandle, 1);
}