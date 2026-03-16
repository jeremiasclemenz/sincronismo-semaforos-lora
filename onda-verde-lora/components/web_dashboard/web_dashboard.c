#include "web_dashboard.h"

#include <stdio.h>
#include <string.h>
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_http_server.h"
#include "nvs_flash.h"

// -------------------------------------------------------
// Variables globales compartidas con lora_sync_task
// -------------------------------------------------------
int  last_rssi              = 0;
int  packet_counter         = 0;
int  last_beacon_duration_ms = 0;
char system_state[32]       = "IDLE";

// -------------------------------------------------------
// HTML del dashboard (embebido como string)
// -------------------------------------------------------
static const char INDEX_HTML[] =
"<!DOCTYPE html>"
"<html><head><meta charset='UTF-8'>"
"<meta name='viewport' content='width=device-width,initial-scale=1'>"
"<title>LoRa Dashboard</title>"
"<style>"
"body{font-family:Arial,sans-serif;background:#1a1a2e;color:#eee;"
"display:flex;justify-content:center;align-items:center;min-height:100vh;margin:0}"
".card{background:#16213e;border-radius:12px;padding:24px;width:320px;"
"box-shadow:0 4px 20px rgba(0,0,0,.4)}"
"h1{text-align:center;color:#0f3460;font-size:1.3em;margin-top:0}"
".row{display:flex;justify-content:space-between;padding:10px 0;"
"border-bottom:1px solid #0f3460}"
".label{color:#a0a0b0}.value{font-weight:bold;color:#e94560}"
".state{text-align:center;padding:12px;margin-top:12px;"
"background:#0f3460;border-radius:8px;font-size:1.1em}"
"</style></head><body>"
"<div class='card'>"
"<h1>&#128225; LoRa Sync Dashboard</h1>"
"<div class='row'><span class='label'>RSSI</span>"
"<span class='value' id='rssi'>--</span></div>"
"<div class='row'><span class='label'>Paquetes</span>"
"<span class='value' id='packets'>--</span></div>"
"<div class='row'><span class='label'>Duraci&oacute;n</span>"
"<span class='value' id='duration'>--</span></div>"
"<div class='state' id='state'>--</div>"
"</div>"
"<script>"
"function up(){fetch('/data').then(r=>r.json()).then(d=>{"
"document.getElementById('rssi').textContent=d.rssi+' dBm';"
"document.getElementById('packets').textContent=d.packets;"
"document.getElementById('duration').textContent=d.duration_ms+' ms';"
"document.getElementById('state').textContent=d.state;"
"}).catch(()=>{})}setInterval(up,1000);up();"
"</script></body></html>";

// -------------------------------------------------------
// Handler GET "/" – devuelve la página HTML
// -------------------------------------------------------
static esp_err_t root_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, INDEX_HTML, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

// -------------------------------------------------------
// Handler GET "/data" – devuelve JSON con datos actuales
// -------------------------------------------------------
static esp_err_t data_handler(httpd_req_t *req)
{
    char json[128];
    snprintf(json, sizeof(json),
             "{\"rssi\":%d,\"packets\":%d,\"duration_ms\":%d,\"state\":\"%s\"}",
             last_rssi, packet_counter, last_beacon_duration_ms, system_state);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

// -------------------------------------------------------
// Iniciar servidor HTTP con los dos endpoints
// -------------------------------------------------------
static httpd_handle_t start_webserver(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    httpd_handle_t server = NULL;

    if (httpd_start(&server, &config) == ESP_OK) {
        httpd_uri_t root = {
            .uri     = "/",
            .method  = HTTP_GET,
            .handler = root_handler,
        };
        httpd_register_uri_handler(server, &root);

        httpd_uri_t data = {
            .uri     = "/data",
            .method  = HTTP_GET,
            .handler = data_handler,
        };
        httpd_register_uri_handler(server, &data);

        printf("[web_dashboard] Servidor HTTP iniciado en http://192.168.4.1\n");
    }
    return server;
}

// -------------------------------------------------------
// Inicializar WiFi en modo Access Point
// -------------------------------------------------------
static void wifi_init_ap(void)
{
    // NVS requerido por el driver WiFi
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }

    esp_netif_init();
    esp_event_loop_create_default();
    esp_netif_create_default_wifi_ap();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&cfg);

    wifi_config_t wifi_config = {
        .ap = {
        #if MASTER_MODE == 1 
            .ssid           = "LoRa-Sync-AP Master",
        #endif
        #if MASTER_MODE == 0
            .ssid           = "LoRa-Sync-AP Slave",
        #endif
            .ssid_len       = 0,
            .password       = "12345678",
            .channel        = 1,
            .max_connection = 4,
            .authmode       = WIFI_AUTH_WPA2_PSK,
        },
    };

    esp_wifi_set_mode(WIFI_MODE_AP);
    esp_wifi_set_config(WIFI_IF_AP, &wifi_config);
    esp_wifi_start();

    #if MASTER_MODE == 1
        printf("[web_dashboard] WiFi AP iniciado – SSID: LoRa-Sync-AP Master   Pass: 12345678\n");
    #else
        printf("[web_dashboard] WiFi AP iniciado – SSID: LoRa-Sync-AP Slave   Pass: 12345678\n");
    #endif
}

// -------------------------------------------------------
// Tarea principal del dashboard web
// -------------------------------------------------------
void web_dashboard_task(void *pvParameters)
{
    (void)pvParameters;

    printf("[web_dashboard] Inicializando WiFi AP...\n");
    wifi_init_ap();

    printf("[web_dashboard] Iniciando servidor HTTP...\n");
    start_webserver();

    // La tarea se mantiene viva; los handlers HTTP corren en el
    // hilo del servidor.  El loop solo mantiene la tarea activa.
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(10000));
    }
}