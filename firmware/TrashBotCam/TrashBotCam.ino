// =============================================================================
//  TRASHBOT CAM
//
//  An ESP32-CAM (AI-Thinker module, OV2640) that gives the web remote a
//  camera that is not the phone's own. It runs no detector - an ESP32 cannot
//  run a useful object detector, and pretending otherwise would be the one
//  kind of lie this project does not tell. Instead it does the two things it
//  can do well:
//
//    GET /capture   one JPEG, fresh, with Access-Control-Allow-Origin: *
//    GET /stream    MJPEG, for a human to look at
//    GET /status    JSON: frame size, uptime, free heap
//
//  The phone's VISION tab points at this board, polls /capture a few times a
//  second, runs COCO-SSD in the browser, and reports the result to the bin.
//  The CORS header is what makes that possible: without it the browser may
//  display the picture but is not allowed to read its pixels.
//
//  Network: joins the bin's own access point by default (TRASHBOT-SETUP), so
//  the whole thing works in a room with no Wi-Fi. Change WIFI_SSID/PASS below
//  to put it on a house network alongside the bin instead. mDNS: trashcam.local
//
//  Wiring: nothing beyond 5 V and GND. The board's UART0 (U0T/U0R) is free
//  for programming. If you ever add an on-board detector, reportToBin() at
//  the bottom already knows how to tell the bin what it saw.
// =============================================================================
#include <WiFi.h>
#include <ESPmDNS.h>
#include <HTTPClient.h>
#include <esp_camera.h>
#include <esp_http_server.h>
#include <esp_timer.h>

// ---------------------------------------------------------------------------
// Configuration
// ---------------------------------------------------------------------------
#define WIFI_SSID      "TRASHBOT-SETUP"      // the bin's AP (see TrashBotWeb settings.h)
#define WIFI_PASS      "uselessbin"
#define HOSTNAME       "trashcam"
#define BIN_HOST       "192.168.4.1"         // where /api/vision lives, if a detector is ever added

static const framesize_t FRAME_SIZE   = FRAMESIZE_VGA;   // 640x480; the browser downscales anyway
static const int         JPEG_QUALITY = 12;              // 10..63, lower is better and bigger
static const uint32_t    WIFI_TIMEOUT_MS = 15000;

// AI-Thinker ESP32-CAM pin map. Other modules differ - check yours.
#define PWDN_GPIO_NUM     32
#define RESET_GPIO_NUM    -1
#define XCLK_GPIO_NUM      0
#define SIOD_GPIO_NUM     26
#define SIOC_GPIO_NUM     27
#define Y9_GPIO_NUM       35
#define Y8_GPIO_NUM       34
#define Y7_GPIO_NUM       39
#define Y6_GPIO_NUM       36
#define Y5_GPIO_NUM       21
#define Y4_GPIO_NUM       19
#define Y3_GPIO_NUM       18
#define Y2_GPIO_NUM        5
#define VSYNC_GPIO_NUM    25
#define HREF_GPIO_NUM     23
#define PCLK_GPIO_NUM     22
#define FLASH_LED_GPIO     4

static httpd_handle_t httpd = nullptr;
static bool cameraOk = false;

// ---------------------------------------------------------------------------
static bool startCamera() {
    camera_config_t c = {};
    c.ledc_channel = LEDC_CHANNEL_0;
    c.ledc_timer   = LEDC_TIMER_0;
    c.pin_d0 = Y2_GPIO_NUM;  c.pin_d1 = Y3_GPIO_NUM;  c.pin_d2 = Y4_GPIO_NUM;  c.pin_d3 = Y5_GPIO_NUM;
    c.pin_d4 = Y6_GPIO_NUM;  c.pin_d5 = Y7_GPIO_NUM;  c.pin_d6 = Y8_GPIO_NUM;  c.pin_d7 = Y9_GPIO_NUM;
    c.pin_xclk = XCLK_GPIO_NUM;   c.pin_pclk = PCLK_GPIO_NUM;
    c.pin_vsync = VSYNC_GPIO_NUM; c.pin_href = HREF_GPIO_NUM;
    c.pin_sccb_sda = SIOD_GPIO_NUM; c.pin_sccb_scl = SIOC_GPIO_NUM;
    c.pin_pwdn = PWDN_GPIO_NUM;   c.pin_reset = RESET_GPIO_NUM;
    c.xclk_freq_hz = 20000000;
    c.pixel_format = PIXFORMAT_JPEG;
    c.frame_size   = FRAME_SIZE;
    c.jpeg_quality = JPEG_QUALITY;
    c.fb_count     = psramFound() ? 2 : 1;
    c.fb_location  = psramFound() ? CAMERA_FB_IN_PSRAM : CAMERA_FB_IN_DRAM;
    c.grab_mode    = CAMERA_GRAB_LATEST;      // /capture must be *now*, not a stale buffer

    if (esp_camera_init(&c) != ESP_OK) return false;

    sensor_t* s = esp_camera_sensor_get();
    if (s) {
        s->set_vflip(s, 0);
        s->set_hmirror(s, 0);
    }
    return true;
}

// ---------------------------------------------------------------------------
// HTTP. esp_http_server, like the CameraWebServer example, because it copes
// with a long-lived MJPEG connection without help.
// ---------------------------------------------------------------------------
static void cors(httpd_req_t* req) {
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
}

static esp_err_t captureHandler(httpd_req_t* req) {
    if (!cameraOk) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "camera not initialised");
        return ESP_FAIL;
    }
    camera_fb_t* fb = esp_camera_fb_get();
    if (!fb) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "capture failed");
        return ESP_FAIL;
    }
    cors(req);
    httpd_resp_set_type(req, "image/jpeg");
    httpd_resp_set_hdr(req, "Content-Disposition", "inline; filename=capture.jpg");
    esp_err_t r = httpd_resp_send(req, (const char*)fb->buf, fb->len);
    esp_camera_fb_return(fb);
    return r;
}

static const char* STREAM_CT   = "multipart/x-mixed-replace;boundary=frame";
static const char* STREAM_BND  = "\r\n--frame\r\n";
static const char* STREAM_PART = "Content-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n";

static esp_err_t streamHandler(httpd_req_t* req) {
    if (!cameraOk) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "camera not initialised");
        return ESP_FAIL;
    }
    cors(req);
    esp_err_t r = httpd_resp_set_type(req, STREAM_CT);
    if (r != ESP_OK) return r;
    char part[64];
    while (true) {
        camera_fb_t* fb = esp_camera_fb_get();
        if (!fb) return ESP_FAIL;
        size_t n = snprintf(part, sizeof(part), STREAM_PART, (unsigned)fb->len);
        r = httpd_resp_send_chunk(req, STREAM_BND, strlen(STREAM_BND));
        if (r == ESP_OK) r = httpd_resp_send_chunk(req, part, n);
        if (r == ESP_OK) r = httpd_resp_send_chunk(req, (const char*)fb->buf, fb->len);
        esp_camera_fb_return(fb);
        if (r != ESP_OK) return r;              // the viewer went away
    }
}

static esp_err_t statusHandler(httpd_req_t* req) {
    char buf[200];
    snprintf(buf, sizeof(buf),
             "{\"camera\":%s,\"frame\":\"%s\",\"uptime\":%lu,\"heap\":%u,\"psram\":%u,\"rssi\":%d}",
             cameraOk ? "true" : "false",
             FRAME_SIZE == FRAMESIZE_VGA ? "640x480" : "other",
             (unsigned long)(esp_timer_get_time() / 1000), (unsigned)ESP.getFreeHeap(),
             (unsigned)ESP.getFreePsram(), WiFi.RSSI());
    cors(req);
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, buf, strlen(buf));
}

static esp_err_t indexHandler(httpd_req_t* req) {
    static const char page[] =
        "<!doctype html><meta name=viewport content='width=device-width'>"
        "<body style='background:#111;color:#ddd;font:14px monospace;padding:16px'>"
        "<h3 style='color:#f5a623;letter-spacing:.2em'>TRASHBOT CAM</h3>"
        "<p>Point the bin's VISION tab at this address. Endpoints: "
        "<a href=/capture style='color:#5b9dd9'>/capture</a> "
        "<a href=/stream style='color:#5b9dd9'>/stream</a> "
        "<a href=/status style='color:#5b9dd9'>/status</a></p>"
        "<img src=/stream style='max-width:100%;border:1px solid #333'></body>";
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, page, sizeof(page) - 1);
}

static void startHttp() {
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.server_port = 80;
    cfg.max_open_sockets = 5;
    if (httpd_start(&httpd, &cfg) != ESP_OK) return;
    httpd_uri_t u;
    u.method = HTTP_GET; u.user_ctx = nullptr;
    u.uri = "/";        u.handler = indexHandler;   httpd_register_uri_handler(httpd, &u);
    u.uri = "/capture"; u.handler = captureHandler; httpd_register_uri_handler(httpd, &u);
    u.uri = "/stream";  u.handler = streamHandler;  httpd_register_uri_handler(httpd, &u);
    u.uri = "/status";  u.handler = statusHandler;  httpd_register_uri_handler(httpd, &u);
}

// ---------------------------------------------------------------------------
// If a detector ever runs on this board, this is how it tells the bin. The
// bin treats it exactly like a report from the phone. Unused today, on
// purpose: nothing here has anything honest to report.
// ---------------------------------------------------------------------------
__attribute__((unused))
static void reportToBin(uint8_t persons, const char* objectsCsv, uint8_t conf) {
    if (WiFi.status() != WL_CONNECTED) return;
    HTTPClient http;
    http.begin(String("http://") + BIN_HOST + "/api/vision");
    http.addHeader("Content-Type", "application/x-www-form-urlencoded");
    http.setTimeout(500);
    String body = String("persons=") + persons + "&objects=" + objectsCsv + "&conf=" + conf + "&src=cam";
    http.POST(body);
    http.end();
}

// ---------------------------------------------------------------------------
void setup() {
    Serial.begin(115200);
    delay(300);
    pinMode(FLASH_LED_GPIO, OUTPUT);
    digitalWrite(FLASH_LED_GPIO, LOW);      // the flash LED is blinding; never on by accident

    cameraOk = startCamera();
    Serial.printf("camera: %s\n", cameraOk ? "ok" : "FAILED - check the ribbon cable and the pin map");

    WiFi.mode(WIFI_STA);
    WiFi.setHostname(HOSTNAME);
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    Serial.printf("joining '%s'", WIFI_SSID);
    uint32_t t0 = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - t0 < WIFI_TIMEOUT_MS) {
        delay(250);
        Serial.print('.');
    }
    Serial.println();
    if (WiFi.status() == WL_CONNECTED) {
        Serial.printf("  http://%s/   (also http://%s.local/)\n", WiFi.localIP().toString().c_str(), HOSTNAME);
    } else {
        Serial.println("  no network - will keep trying in the background");
    }
    if (MDNS.begin(HOSTNAME)) MDNS.addService("http", "tcp", 80);

    startHttp();
}

void loop() {
    // Reconnect quietly if the bin's AP came up after we did.
    static uint32_t lastTry = 0;
    if (WiFi.status() != WL_CONNECTED && millis() - lastTry > 10000) {
        lastTry = millis();
        WiFi.disconnect();
        WiFi.begin(WIFI_SSID, WIFI_PASS);
    }
    delay(50);      // nothing to do here - esp_http_server runs its own task
}
