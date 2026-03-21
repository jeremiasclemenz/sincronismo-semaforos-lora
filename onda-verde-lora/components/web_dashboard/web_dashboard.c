#include "web_dashboard.h"

#include <stdio.h>
#include <string.h>
#include "esp_err.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_log.h"
#include "esp_http_server.h"

static const char *TAG = "web_dashboard";
static char ap_ip_str[16] = "192.168.4.1";

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
    esp_err_t err = httpd_start(&server, &config);

    if (err == ESP_OK) {
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

        ESP_LOGI(TAG, "Servidor HTTP iniciado en http://%s", ap_ip_str);
    } else {
        ESP_LOGE(TAG, "httpd_start fallo: %s", esp_err_to_name(err));
    }
    return server;
}

// -------------------------------------------------------
// Inicializar WiFi en modo Access Point
// -------------------------------------------------------
static esp_err_t wifi_init_ap(const app_config_t *app_config)
{
    const bool is_master = app_config_is_master(app_config);
    const char *ssid = is_master ? "LoRa-Sync-AP-Master" : "LoRa-Sync-AP-Slave";

    // esp_netif_init() y esp_event_loop_create_default() ya se llamaron en app_main
    esp_netif_create_default_wifi_ap();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_err_t err = esp_wifi_init(&cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_init falló: %s", esp_err_to_name(err));
        return err;
    }

    wifi_config_t wifi_config = { 0 };
    snprintf((char *)wifi_config.ap.ssid, sizeof(wifi_config.ap.ssid), "%s", ssid);
    if(is_master){
        snprintf((char *)wifi_config.ap.password, sizeof(wifi_config.ap.password), "%s", "12345678");
    }else{
        snprintf((char *)wifi_config.ap.password, sizeof(wifi_config.ap.password), "%s", "87654321");
    }
    wifi_config.ap.ssid_len = strlen(ssid);
    if(is_master){
        wifi_config.ap.channel = 1;
    }else{
        wifi_config.ap.channel = 11; // Extremo de la banda
    }
    wifi_config.ap.max_connection = 4;
    wifi_config.ap.authmode = WIFI_AUTH_WPA2_PSK;
    wifi_config.ap.pairwise_cipher = WIFI_CIPHER_TYPE_CCMP;
    wifi_config.ap.pmf_cfg.capable = true;
    wifi_config.ap.pmf_cfg.required = false;

    err = esp_wifi_set_mode(WIFI_MODE_AP);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_set_mode(AP) falló: %s", esp_err_to_name(err));
        return err;
    }

    err = esp_wifi_set_config(WIFI_IF_AP, &wifi_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_set_config(AP) falló: %s", esp_err_to_name(err));
        return err;
    }

    // Desactiva power-save para que el beacon del AP sea estable durante depuración.
    err = esp_wifi_set_ps(WIFI_PS_NONE);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "esp_wifi_set_ps(WIFI_PS_NONE) falló: %s", esp_err_to_name(err));
    }

    err = esp_wifi_start();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_start falló: %s", esp_err_to_name(err));
        return err;
    }

    esp_netif_t *ap_netif = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");
    if (ap_netif != NULL) {
        esp_netif_ip_info_t ip_info;
        if (esp_netif_get_ip_info(ap_netif, &ip_info) == ESP_OK) {
            snprintf(ap_ip_str, sizeof(ap_ip_str), IPSTR, IP2STR(&ip_info.ip));
        }
    }

    ESP_LOGI(TAG, "WiFi AP iniciado - SSID: %s  Pass: %s  Canal: %d", ssid, wifi_config.ap.password, wifi_config.ap.channel);
    ESP_LOGI(TAG, "Conectate al AP y abre http://%s", ap_ip_str);
    return ESP_OK;
}

// -------------------------------------------------------
// Tarea principal del dashboard web
// -------------------------------------------------------
void web_dashboard_task(void *pvParameters)
{
    const app_config_t *app_config = (const app_config_t *)pvParameters;

    printf("[web_dashboard] Inicializando WiFi AP...\n");
    if (wifi_init_ap(app_config) != ESP_OK) {
        ESP_LOGE(TAG, "No se pudo iniciar el AP WiFi. Dashboard deshabilitado.");
        vTaskDelete(NULL);
        return;
    }

    printf("[web_dashboard] Iniciando servidor HTTP...\n");
    if (start_webserver() == NULL) {
        ESP_LOGE(TAG, "No se pudo iniciar el servidor HTTP.");
    }

    // La tarea se mantiene viva; los handlers HTTP corren en el
    // hilo del servidor.  El loop solo mantiene la tarea activa.
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}