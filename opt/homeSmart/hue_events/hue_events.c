/*
 * hue_event - Version 1.1.3
 * Uses Hue v2 Event Stream API (HTTP/2) with CLI args and MQTT.
 * FIXED: MQTT always emits regardless of debug level.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdarg.h>
#include <curl/curl.h>
#include <mosquitto.h>

#define EVENT_STREAM_PATH "/eventstream/clip/v2"
#define DEFAULT_PORT 1883

enum DebugLevel {
    DEBUG_NONE = 0,
    DEBUG_INFO = 1,
    DEBUG_VERBOSE = 2
};

static int debug_level = DEBUG_INFO;

static void log_debug(int level, const char *fmt, ...) {
    if (level <= debug_level) {
        va_list args;
        va_start(args, fmt);
        vfprintf(stderr, fmt, args);
        va_end(args);
    }
}

static size_t stream_callback(char *ptr, size_t size, size_t nmemb, void *userdata) {
    size_t total_size = size * nmemb;
    ptr[total_size] = '\0';

    char *start = strstr(ptr, "data:");
    if (start) {
        start += 5;
        while (*start == ' ') start++;

        // Always publish
        struct mosquitto *mosq = (struct mosquitto *)userdata;
        mosquitto_publish(mosq, NULL, "hue/events/raw", strlen(start), start, 0, false);

        // Only log if debug
        log_debug(DEBUG_VERBOSE, "[hue_event] Received: %s\n", start);
    }

    return total_size;
}

void parse_mqtt_url(const char *url, char *host, int *port) {
    *port = DEFAULT_PORT;
    if (sscanf(url, "mqtt://%255[^:]:%d", host, port) >= 1) return;
    if (sscanf(url, "mqtt://%255[^/]", host) == 1) return;
    strcpy(host, "localhost");
}

int main(int argc, char *argv[]) {
    const char *debug_env = getenv("DEBUG");
    if (debug_env) {
        debug_level = atoi(debug_env);
        if (debug_level < 0 || debug_level > 2) debug_level = DEBUG_INFO;
    }

    if (argc < 4 || strcmp(argv[3], "--mqtt") != 0 || argc < 5) {
        fprintf(stderr, "Usage: %s <bridge_ip> <api_key> --mqtt <mqtt_url>\n", argv[0]);
        return 1;
    }

    const char *bridge_ip = argv[1];
    const char *api_key = argv[2];
    const char *mqtt_url = argv[4];
    char mqtt_host[256] = "localhost";
    int mqtt_port = DEFAULT_PORT;

    parse_mqtt_url(mqtt_url, mqtt_host, &mqtt_port);
    log_debug(DEBUG_INFO, "[hue_event] MQTT -> host: %s, port: %d\n", mqtt_host, mqtt_port);

    mosquitto_lib_init();
    struct mosquitto *mosq = mosquitto_new(NULL, true, NULL);
    if (!mosq) {
        fprintf(stderr, "[hue_event] Failed to create MQTT client\n");
        return 1;
    }

    if (mosquitto_connect(mosq, mqtt_host, mqtt_port, 60) != MOSQ_ERR_SUCCESS) {
        fprintf(stderr, "[hue_event] MQTT connection failed\n");
        return 1;
    }

    CURL *curl = curl_easy_init();
    if (!curl) {
        fprintf(stderr, "[hue_event] Failed to initialize CURL\n");
        return 1;
    }

    char url[512];
    snprintf(url, sizeof(url), "https://%s%s", bridge_ip, EVENT_STREAM_PATH);

    struct curl_slist *headers = NULL;
    char auth_header[512];
    snprintf(auth_header, sizeof(auth_header), "hue-application-key: %s", api_key);
    headers = curl_slist_append(headers, auth_header);
    headers = curl_slist_append(headers, "Accept: text/event-stream");

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, stream_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)mosq);
    curl_easy_setopt(curl, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_2TLS);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);

    log_debug(DEBUG_INFO, "[hue_event] Connecting to Hue event stream at %s...\n", url);
    CURLcode res = curl_easy_perform(curl);
    if (res != CURLE_OK) {
        fprintf(stderr, "[hue_event] CURL failed: %s\n", curl_easy_strerror(res));
    }

    curl_easy_cleanup(curl);
    curl_slist_free_all(headers);
    mosquitto_disconnect(mosq);
    mosquitto_destroy(mosq);
    mosquitto_lib_cleanup();

    return 0;
}

