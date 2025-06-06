/* hue_event - Version 1.1.2
 * Updated: 2025-06-05
 * Description: Full event listener that parses MQTT_URL and publishes Hue events to MQTT.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <curl/curl.h>
#include <mosquitto.h>

#define EVENT_STREAM_ENDPOINT "/eventstream/clip/v2"

static struct mosquitto *mosq = NULL;

// Callback for incoming data from CURL
static size_t curl_callback(char *ptr, size_t size, size_t nmemb, void *userdata) {
    size_t total_size = size * nmemb;
    if (total_size > 0) {
        printf("[hue_event] Event: %.*s\n", (int)total_size, ptr);
        mosquitto_publish(mosq, NULL, (const char *)userdata, total_size, ptr, 0, false);
    }
    return total_size;
}

int main() {
    const char *bridge_ip = getenv("BRIDGE_IP");
    const char *api_key = getenv("API_KEY");
    const char *mqtt_url = getenv("MQTT_URL");

    if (!bridge_ip || !api_key || !mqtt_url) {
        fprintf(stderr, "[hue_event] Required env vars: BRIDGE_IP, API_KEY, MQTT_URL\n");
        return 1;
    }

    // Parse MQTT_URL
    char *mqtt_host = NULL;
    int mqtt_port = 1883;
    if (strncmp(mqtt_url, "mqtt://", 7) == 0) {
        mqtt_url += 7;
        char *url_copy = strdup(mqtt_url);
        char *colon = strchr(url_copy, ':');
        if (colon) {
            *colon = '\0';
            mqtt_host = url_copy;
            mqtt_port = atoi(colon + 1);
        } else {
            mqtt_host = url_copy;
        }
        printf("[hue_event] MQTT host parsed as: %s\n", mqtt_host);
        printf("[hue_event] MQTT port parsed as: %d\n", mqtt_port);
    } else {
        fprintf(stderr, "[hue_event] Invalid MQTT_URL. Expected mqtt://host[:port]\n");
        return 1;
    }

    // Init MQTT
    mosquitto_lib_init();
    mosq = mosquitto_new(NULL, true, NULL);
    if (!mosq) {
        fprintf(stderr, "[hue_event] Failed to create MQTT client\n");
        return 1;
    }
    if (mosquitto_connect(mosq, mqtt_host, mqtt_port, 60) != MOSQ_ERR_SUCCESS) {
        fprintf(stderr, "[hue_event] Failed to connect to MQTT broker\n");
        return 1;
    }

    // Setup CURL for Hue event stream
    CURL *curl = curl_easy_init();
    if (!curl) {
        fprintf(stderr, "[hue_event] Failed to init CURL\n");
        return 1;
    }

    char url[256];
    snprintf(url, sizeof(url), "http://%s%s", bridge_ip, EVENT_STREAM_ENDPOINT);
    char auth_header[256];
    snprintf(auth_header, sizeof(auth_header), "hue-application-key: %s", api_key);

    struct curl_slist *headers = NULL;
    headers = curl_slist_append(headers, auth_header);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_callback);

    char topic[128];
    snprintf(topic, sizeof(topic), "hue/events/%s", bridge_ip);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, topic);

    printf("[hue_event] Starting event stream from %s\n", url);
    curl_easy_perform(curl);

    // Cleanup
    curl_easy_cleanup(curl);
    curl_slist_free_all(headers);
    mosquitto_destroy(mosq);
    mosquitto_lib_cleanup();
    free(mqtt_host);

    return 0;
}

