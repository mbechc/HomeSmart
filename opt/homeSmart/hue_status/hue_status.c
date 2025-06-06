#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <curl/curl.h>
#include <MQTTClient.h>

#define MQTT_QOS 1
#define MQTT_TIMEOUT 10000L
#define BUF_SIZE (1024 * 64)

#define LOG(fmt, ...) do { fprintf(stdout, "[hue_status] " fmt "\n", ##__VA_ARGS__); fflush(stdout); } while (0)
#define ERR(fmt, ...) do { fprintf(stderr, "[hue_status] ERROR: " fmt "\n", ##__VA_ARGS__); fflush(stderr); } while (0)

volatile sig_atomic_t keep_running = 1;

void int_handler(int dummy) {
    keep_running = 0;
}

struct memory {
    char *response;
    size_t size;
};

size_t write_callback(char *ptr, size_t size, size_t nmemb, void *userdata) {
    size_t total = size * nmemb;
    struct memory *mem = (struct memory *)userdata;

    if (mem->size + total >= BUF_SIZE - 1) return 0;

    memcpy(mem->response + mem->size, ptr, total);
    mem->size += total;
    mem->response[mem->size] = '\0';
    return total;
}

int publish_resource(const char *mqtt_url, const char *topic, const char *payload) {
    MQTTClient client;
    MQTTClient_create(&client, mqtt_url, "hue-status-pub", MQTTCLIENT_PERSISTENCE_NONE, NULL);
    MQTTClient_connectOptions conn_opts = MQTTClient_connectOptions_initializer;
    conn_opts.keepAliveInterval = 20;
    conn_opts.cleansession = 1;

    int rc = MQTTClient_connect(client, &conn_opts);
    if (rc != MQTTCLIENT_SUCCESS) {
        ERR("MQTT connect failed: %d", rc);
        return 1;
    }

    MQTTClient_message msg = MQTTClient_message_initializer;
    msg.payload = (void *)payload;
    msg.payloadlen = (int)strlen(payload);
    msg.qos = MQTT_QOS;
    msg.retained = 0;

    MQTTClient_deliveryToken token;
    MQTTClient_publishMessage(client, topic, &msg, &token);
    MQTTClient_waitForCompletion(client, token, MQTT_TIMEOUT);
    MQTTClient_disconnect(client, 1000);
    MQTTClient_destroy(&client);
    return 0;
}

int fetch_and_publish(const char *bridge_ip, const char *api_key, const char *mqtt_url, const char *endpoint, const char *type) {
    CURL *curl = curl_easy_init();
    if (!curl) {
        ERR("Failed to initialize curl");
        return 1;
    }

    struct memory chunk;
    chunk.response = malloc(BUF_SIZE);
    chunk.size = 0;
    chunk.response[0] = '\0';

    char url[512];
    snprintf(url, sizeof(url), "https://%s/clip/v2/resource/%s", bridge_ip, endpoint);

    char auth_header[256];
    snprintf(auth_header, sizeof(auth_header), "hue-application-key: %s", api_key);

    struct curl_slist *headers = NULL;
    headers = curl_slist_append(headers, auth_header);

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &chunk);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);

    CURLcode res = curl_easy_perform(curl);
    if (res != CURLE_OK) {
        ERR("Failed to fetch %s: %s", endpoint, curl_easy_strerror(res));
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);
        free(chunk.response);
        return 1;
    }

    // Very basic split and publish per resource object (not full JSON parsing)
    const char *p = chunk.response;
    while ((p = strstr(p, "{\"")) != NULL) {
        const char *end = strchr(p, '}');
        if (!end) break;
        end++;

        int len = end - p;
        if (len >= BUF_SIZE - 1) break;

        char payload[BUF_SIZE];
        strncpy(payload, p, len);
        payload[len] = '\0';

        // extract "id":"<uuid>" to build topic
        const char *idptr = strstr(payload, "\"id\":\"");
        if (!idptr) { p = end; continue; }
        idptr += 6;
        const char *idend = strchr(idptr, '"');
        if (!idend) { p = end; continue; }

        char id[128];
        int idlen = idend - idptr;
        if (idlen >= sizeof(id)) idlen = sizeof(id) - 1;
        strncpy(id, idptr, idlen);
        id[idlen] = '\0';

        char topic[512];
        snprintf(topic, sizeof(topic), "hue/status/%s/%s/%s", bridge_ip, type, id);

        publish_resource(mqtt_url, topic, payload);
        p = end;
    }

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    free(chunk.response);
    return 0;
}

int main(int argc, char *argv[]) {
    const char *bridge_ip = getenv("BRIDGE_IP");
    const char *api_key = getenv("API_KEY");
    const char *mqtt_url = getenv("MQTT_URL");

    if (!bridge_ip || !api_key || !mqtt_url) {
        ERR("Missing environment variables: BRIDGE_IP, API_KEY, MQTT_URL");
        return 1;
    }

    signal(SIGINT, int_handler);
    LOG("Starting hue_status for %s", bridge_ip);

    while (keep_running) {
        LOG("Polling Hue status...");

        fetch_and_publish(bridge_ip, api_key, mqtt_url, "sensor", "sensor");
        fetch_and_publish(bridge_ip, api_key, mqtt_url, "light", "light");

        for (int i = 0; i < 60 && keep_running; i++) sleep(1);
    }

    LOG("Shutting down.");
    return 0;
}

