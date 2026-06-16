#include "web_dashboard.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include "event_log.h"
#include "esp_err.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_log.h"
#include "esp_http_server.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static const char *TAG = "web_dashboard";
static char ap_ip_str[16] = "192.168.4.1";

/* ── Estado compartido (protegido por mutex) ────────────────────────── */
static SemaphoreHandle_t dash_mutex = NULL;

static int   s_rssi          = 0;
static int   s_packets       = 0;
static bool  s_relay_active  = false;
static char  s_state[32]     = "IDLE";

/* Referencia a la config para exponer parámetros RF en /data */
static const app_config_t *s_cfg = NULL;

/* ── API de escritura ───────────────────────────────────────────────── */

void web_dashboard_set_rssi(int rssi)
{
    if (dash_mutex) xSemaphoreTake(dash_mutex, portMAX_DELAY);
    s_rssi = rssi;
    if (dash_mutex) xSemaphoreGive(dash_mutex);
}

void web_dashboard_set_state(const char *state)
{
    if (!state) return;
    if (dash_mutex) xSemaphoreTake(dash_mutex, portMAX_DELAY);
    snprintf(s_state, sizeof(s_state), "%s", state);
    if (dash_mutex) xSemaphoreGive(dash_mutex);
}

void web_dashboard_inc_packets(void)
{
    if (dash_mutex) xSemaphoreTake(dash_mutex, portMAX_DELAY);
    s_packets++;
    if (dash_mutex) xSemaphoreGive(dash_mutex);
}

void web_dashboard_set_relay_active(bool active)
{
    if (dash_mutex) xSemaphoreTake(dash_mutex, portMAX_DELAY);
    s_relay_active = active;
    if (dash_mutex) xSemaphoreGive(dash_mutex);
}

/* ── HTML embebido ──────────────────────────────────────────────────── */
static const char INDEX_HTML[] =
"<!DOCTYPE html>"
"<html><head><meta charset='UTF-8'>"
"<meta name='viewport' content='width=device-width,initial-scale=1'>"
"<title>LoRa Sync Dashboard</title>"
"<style>"
"body{font-family:Arial,sans-serif;background:#1a1a2e;color:#eee;"
"display:flex;justify-content:center;align-items:flex-start;min-height:100vh;"
"margin:0;padding:20px;box-sizing:border-box}"
".card{background:#16213e;border-radius:12px;padding:24px;width:360px;"
"box-shadow:0 4px 20px rgba(0,0,0,.4)}"
"h1{text-align:center;color:#e0e0ff;font-size:1.3em;margin-top:0}"
".row{display:flex;justify-content:space-between;align-items:center;"
"padding:8px 0;border-bottom:1px solid #0f3460}"
".label{color:#a0a0b0;font-size:.9em}"
".value{font-weight:bold;color:#e94560}"
".state-box{text-align:center;padding:12px;margin:12px 0;border-radius:8px;"
"font-size:1.1em;letter-spacing:.05em;font-weight:bold;"
"background:#444;color:#ccc;transition:background .3s}"
".st-gris{background:#444;color:#ccc}"
".st-azul{background:#1565c0;color:#fff}"
".st-naranja{background:#e65100;color:#fff}"
".st-rojo{background:#b71c1c;color:#fff}"
".st-verde{background:#1b5e20;color:#b9f6ca}"
".semaforos{display:flex;justify-content:space-around;margin:16px 0}"
".sem{text-align:center}"
".sem-label{font-size:.8em;color:#a0a0b0;margin-top:4px}"
".luz{width:40px;height:40px;border-radius:50%;margin:4px auto;"
"border:2px solid #333}"
".verde{background:#00e676;box-shadow:0 0 12px #00e676}"
".rojo{background:#f44336;box-shadow:0 0 8px #f44336}"
".apagado{background:#333}"
"canvas{display:block;margin:6px auto 0;background:#0f3460;border-radius:4px}"
"button{background:#0f3460;color:#eee;border:none;padding:8px 16px;"
"border-radius:6px;cursor:pointer;margin:4px;font-size:.9em}"
"button:hover{background:#1a4a8a}"
"</style></head><body>"
"<div class='card'>"
"<h1>&#128225; LoRa Sync</h1>"

/* Semáforos */
"<div class='semaforos'>"
"<div class='sem'>"
"<div class='luz apagado' id='sem1r'></div>"
"<div class='luz apagado' id='sem1v'></div>"
"<div class='sem-label'>V&iacute;a Principal</div>"
"</div>"
"<div class='sem'>"
"<div class='luz apagado' id='sem2r'></div>"
"<div class='luz apagado' id='sem2v'></div>"
"<div class='sem-label'>V&iacute;a Secundaria</div>"
"</div>"
"</div>"

/* Estado del sistema (con color por estado y contador de lockout) */
"<div class='state-box' id='state'>--</div>"

/* Datos */
"<div class='row'><span class='label'>RSSI</span>"
"<span class='value' id='rssi'>--</span></div>"
"<canvas id='spark' width='312' height='36'></canvas>"
"<div class='row'><span class='label'>Paquetes</span>"
"<span class='value' id='packets'>--</span></div>"
"<div class='row'><span class='label'>Uptime</span>"
"<span class='value' id='uptime'>--</span></div>"
"<div class='row'><span class='label'>SF / BW / CR</span>"
"<span class='value' id='rf'>--</span></div>"
"<div class='row'><span class='label'>TX Power</span>"
"<span class='value' id='txp'>--</span></div>"
"<div class='row'><span class='label'>Debug mode</span>"
"<span class='value' id='dbg'>--</span></div>"

/* Controles */
"<div style='text-align:center;margin-top:14px'>"
"<button onclick='toggleDebug()' id='btnDebug'>&#128295; Debug ON/OFF</button>"
"<button onclick='location.href=\"/export.csv\"'>&#128229; CSV</button>"
"</div>"
"</div>"

"<script>"
/* Sincronizar hora al cargar */
"fetch('/sync_time',{method:'POST',"
"headers:{'Content-Type':'application/json'},"
"body:JSON.stringify({unix_time:Math.floor(Date.now()/1000)})});"

"var hist=[],prevSt='',lkT0=0,lkDur=0;"
"var BW=[7.8,10.4,15.6,20.8,31.25,41.7,62.5,125,250,500];"

/* Uptime en T+HH:MM:SS */
"function f2(n){return String(n).padStart(2,'0')}"
"function fmtUp(ms){var s=Math.floor(ms/1000),h=Math.floor(s/3600),"
"m=Math.floor(s%3600/60);return'T+'+f2(h)+':'+f2(m)+':'+f2(s%60)}"

/* Sparkline RSSI (ultimos 20 valores, Canvas nativo) */
"function drawSpark(){var c=document.getElementById('spark'),"
"x=c.getContext('2d');x.clearRect(0,0,c.width,c.height);"
"if(hist.length<2)return;"
"var mn=Math.min.apply(null,hist)-2,mx=Math.max.apply(null,hist)+2,"
"w=c.width/19;x.strokeStyle='#00bcd4';x.lineWidth=2;x.beginPath();"
"for(var i=0;i<hist.length;i++){"
"var y=c.height-3-(hist[i]-mn)/(mx-mn)*(c.height-6);"
"if(i)x.lineTo(i*w,y);else x.moveTo(0,y)}x.stroke()}"

/* Semáforos */
"function luz(id,cl){document.getElementById(id).className='luz '+cl}"
"function setSem(st,relay){"
"var v1=st.indexOf('BEACON')>=0||st.indexOf('LOCK')>=0||st.indexOf('DEBUG')>=0;"
"var v2=relay||st.indexOf('RELAY')>=0;"
"luz('sem1v',v1?'verde':'apagado');luz('sem1r',v1?'apagado':'rojo');"
"luz('sem2v',v2?'verde':'apagado');luz('sem2r',v2?'apagado':'rojo')}"

/* Estado textual con color + contadores de lockout (master 60s, slave 15s) */
"function setState(st){"
"if(st==='LOCK'&&prevSt!=='LOCK'){lkT0=Date.now();lkDur=60}"
"if(st==='LISTEN'&&prevSt==='RELAY ON'){lkT0=Date.now();lkDur=15}"
"if(st==='IDLE'||st==='RELAY ON')lkDur=0;"
"prevSt=st;"
"var txt=st,cls='st-gris',rem=0;"
"if(lkDur>0)rem=Math.max(0,lkDur-Math.floor((Date.now()-lkT0)/1000));"
"if(st==='LOCK'){cls='st-naranja';txt='LOCK ('+rem+'s)'}"
"else if(st.indexOf('BEACON')>=0||st.indexOf('DEBUG')>=0)cls='st-azul';"
"else if(st==='TX LOST')cls='st-rojo';"
"else if(st==='RELAY ON')cls='st-verde';"
"else if(st==='LISTEN'&&lkDur===15){"
"if(rem>0){cls='st-naranja';txt='LISTEN - LOCKOUT ('+rem+'s)'}else lkDur=0}"
"var b=document.getElementById('state');"
"b.textContent=txt;b.className='state-box '+cls}"

/* Polling principal */
"function up(){"
"fetch('/data').then(function(r){return r.json()}).then(function(d){"
"var st=String(d.state||'').toUpperCase().trim();"
/* relay_active: booleano JSON; tolerar string por robustez */
"var relay=(d.relay_active===true)||(d.relay_active==='true');"
"hist.push(d.rssi);if(hist.length>20)hist.shift();drawSpark();"
"document.getElementById('rssi').textContent=d.rssi+' dBm';"
"document.getElementById('packets').textContent=d.packets;"
"document.getElementById('uptime').textContent=fmtUp(d.uptime_ms);"
"document.getElementById('rf').textContent="
"'SF'+d.sf+' / '+(BW[d.bw]||'?')+' kHz / 4/'+(d.cr+4);"
"document.getElementById('txp').textContent=d.tx_power+' dBm';"
"document.getElementById('dbg').textContent=d.debug_mode?'ON':'OFF';"
"setSem(st,relay);setState(st);"
"}).catch(function(){})}"
"setInterval(up,1000);up();"

/* Toggle debug */
"function toggleDebug(){"
"fetch('/data').then(function(r){return r.json()}).then(function(d){"
"return fetch('/config',{method:'POST',"
"headers:{'Content-Type':'application/json'},"
"body:JSON.stringify({debug_mode:!d.debug_mode})});"
"}).then(up)}"
"</script></body></html>";

/* ── Handlers HTTP ──────────────────────────────────────────────────── */

static esp_err_t root_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, INDEX_HTML, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

static esp_err_t data_handler(httpd_req_t *req)
{
    xSemaphoreTake(dash_mutex, portMAX_DELAY);
    int  rssi         = s_rssi;
    int  packets      = s_packets;
    bool relay_active = s_relay_active;
    char state[32];
    snprintf(state, sizeof(state), "%s", s_state);
    xSemaphoreGive(dash_mutex);

    uint32_t uptime_ms = (uint32_t)(esp_timer_get_time() / 1000ULL);

    char json[256];
    snprintf(json, sizeof(json),
        "{"
        "\"rssi\":%d,"
        "\"packets\":%d,"
        "\"state\":\"%s\","
        "\"uptime_ms\":%lu,"
        "\"relay_active\":%s,"
        "\"debug_mode\":%s,"
        "\"sf\":%d,"
        "\"bw\":%d,"
        "\"cr\":%d,"
        "\"tx_power\":%d"
        "}",
        rssi, packets, state,
        (unsigned long)uptime_ms,
        relay_active ? "true" : "false",
        (s_cfg && s_cfg->debug_mode) ? "true" : "false",
        s_cfg ? s_cfg->lora_sf      : 9,
        s_cfg ? s_cfg->lora_bw      : 7,
        s_cfg ? s_cfg->lora_cr      : 1,
        s_cfg ? s_cfg->lora_tx_power : 17);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

static esp_err_t sync_time_handler(httpd_req_t *req)
{
    /* Leer body JSON: {"unix_time": <epoch>} — ignoramos el valor por ahora,
     * el endpoint existe para que el browser no reciba 404. */
    char buf[64];
    int  len = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (len > 0) buf[len] = '\0';

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, "{\"ok\":true}", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

static esp_err_t config_get_handler(httpd_req_t *req)
{
    char json[128];
    snprintf(json, sizeof(json),
        "{"
        "\"sf\":%d,"
        "\"bw\":%d,"
        "\"cr\":%d,"
        "\"tx_power\":%d,"
        "\"debug_mode\":%s"
        "}",
        s_cfg ? s_cfg->lora_sf      : 9,
        s_cfg ? s_cfg->lora_bw      : 7,
        s_cfg ? s_cfg->lora_cr      : 1,
        s_cfg ? s_cfg->lora_tx_power : 17,
        (s_cfg && s_cfg->debug_mode) ? "true" : "false");

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

static esp_err_t config_post_handler(httpd_req_t *req)
{
    /* Parseo mínimo de {"debug_mode": true/false}.
     * Una implementación completa usaría cJSON. */
    char buf[128];
    int  len = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (len > 0) {
        buf[len] = '\0';
        if (s_cfg) {
            app_config_t *cfg = (app_config_t *)s_cfg; /* cast para modificar */
            if (strstr(buf, "\"debug_mode\":true"))  cfg->debug_mode = true;
            if (strstr(buf, "\"debug_mode\":false")) cfg->debug_mode = false;
        }
    }
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, "{\"ok\":true}", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

/* ── Export CSV ─────────────────────────────────────────────────────── */

static const char *evt_name(uint8_t e)
{
    if (e & EVT_BOOT)     return "BOOT";
    if (e & EVT_TX_OK)    return "TX_OK";
    if (e & EVT_TX_LOST)  return "TX_LOST";
    if (e & EVT_RX_OK)    return "RX_OK";
    if (e & EVT_RX_ERR)   return "RX_ERR";
    if (e & EVT_RELAY_ON) return "RELAY_ON";
    if (e & EVT_DEBUG)    return "DEBUG";
    return "UNKNOWN";
}

static esp_err_t export_csv_handler(httpd_req_t *req)
{
    uint16_t     n       = event_log_count();
    log_entry_t *entries = NULL;

    if (n > 0) {
        entries = malloc((size_t)n * sizeof(log_entry_t));
        if (!entries) {
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                                "sin memoria para exportar");
            return ESP_FAIL;
        }
        n = event_log_read_all(entries, n);
    }

    httpd_resp_set_type(req, "text/csv");
    httpd_resp_set_hdr(req, "Content-Disposition",
                       "attachment; filename=\"lora_sync_log.csv\"");

    /* Envío chunked: el log completo (1024 × ~40 bytes) no entra en un
     * buffer único sin comprometer RAM. */
    httpd_resp_sendstr_chunk(req,
        "uptime_ms,event,rssi_dbm,snr_db,sequence,debug\n");

    char line[80];
    for (uint16_t i = 0; i < n; i++) {
        const log_entry_t *e = &entries[i];
        /* SNR en centésimas de dB para evitar printf de float */
        int sc = (int)e->snr_x4 * 25;
        snprintf(line, sizeof(line), "%lu,%s,%d,%s%d.%02d,%u,%u\n",
                 (unsigned long)e->uptime_ms,
                 evt_name(e->event),
                 (int)e->rssi,
                 (sc < 0) ? "-" : "", abs(sc) / 100, abs(sc) % 100,
                 (unsigned)e->sequence,
                 (e->event & EVT_DEBUG) ? 1u : 0u);
        if (httpd_resp_sendstr_chunk(req, line) != ESP_OK) break;
    }

    free(entries);
    httpd_resp_sendstr_chunk(req, NULL); /* fin de la respuesta chunked */
    return ESP_OK;
}

/* ── Servidor HTTP ──────────────────────────────────────────────────── */
static httpd_handle_t start_webserver(void)
{
    httpd_config_t config  = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers = 8;
    httpd_handle_t server  = NULL;

    if (httpd_start(&server, &config) != ESP_OK) {
        ESP_LOGE(TAG, "httpd_start falló");
        return NULL;
    }

    httpd_uri_t uris[] = {
        { .uri = "/",          .method = HTTP_GET,  .handler = root_handler        },
        { .uri = "/data",      .method = HTTP_GET,  .handler = data_handler        },
        { .uri = "/config",    .method = HTTP_GET,  .handler = config_get_handler  },
        { .uri = "/config",    .method = HTTP_POST, .handler = config_post_handler },
        { .uri = "/sync_time", .method = HTTP_POST, .handler = sync_time_handler   },
        { .uri = "/export.csv", .method = HTTP_GET, .handler = export_csv_handler  },
    };

    for (int i = 0; i < (int)(sizeof(uris) / sizeof(uris[0])); i++) {
        httpd_register_uri_handler(server, &uris[i]);
    }

    ESP_LOGI(TAG, "Servidor HTTP iniciado en http://%s", ap_ip_str);
    return server;
}

/* ── WiFi AP ────────────────────────────────────────────────────────── */
static esp_err_t wifi_init_ap(const app_config_t *cfg)
{
    const bool  is_master = app_config_is_master(cfg);
    const char *ssid      = is_master ? "LoRa-Sync-Master" : "LoRa-Sync-Slave";
    const char *pass      = is_master ? "12345678"         : "87654321";
    const int   channel   = is_master ? 1                  : 11;

    esp_netif_create_default_wifi_ap();

    wifi_init_config_t wcfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&wcfg));

    wifi_config_t ap_cfg = { 0 };
    snprintf((char *)ap_cfg.ap.ssid,     sizeof(ap_cfg.ap.ssid),     "%s", ssid);
    snprintf((char *)ap_cfg.ap.password, sizeof(ap_cfg.ap.password), "%s", pass);
    ap_cfg.ap.ssid_len       = strlen(ssid);
    ap_cfg.ap.channel        = channel;
    ap_cfg.ap.max_connection = 4;
    ap_cfg.ap.authmode       = WIFI_AUTH_WPA2_PSK;
    ap_cfg.ap.pairwise_cipher = WIFI_CIPHER_TYPE_CCMP;
    ap_cfg.ap.pmf_cfg.capable = true;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_cfg));
    esp_wifi_set_ps(WIFI_PS_NONE);
    ESP_ERROR_CHECK(esp_wifi_start());

    esp_netif_t *ap_netif = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");
    if (ap_netif) {
        esp_netif_ip_info_t ip;
        if (esp_netif_get_ip_info(ap_netif, &ip) == ESP_OK) {
            snprintf(ap_ip_str, sizeof(ap_ip_str), IPSTR, IP2STR(&ip.ip));
        }
    }

    ESP_LOGI(TAG, "AP: %s  pass: %s  ch: %d  ip: %s", ssid, pass, channel, ap_ip_str);
    return ESP_OK;
}

/* ── Tarea principal ────────────────────────────────────────────────── */
void web_dashboard_task(void *pvParameters)
{
    s_cfg      = (const app_config_t *)pvParameters;
    dash_mutex = xSemaphoreCreateMutex();
    configASSERT(dash_mutex != NULL);

    if (wifi_init_ap(s_cfg) != ESP_OK) {
        ESP_LOGE(TAG, "No se pudo iniciar AP WiFi.");
        vTaskDelete(NULL);
        return;
    }

    if (start_webserver() == NULL) {
        ESP_LOGE(TAG, "No se pudo iniciar el servidor HTTP.");
    }

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
