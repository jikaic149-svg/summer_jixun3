#ifndef DEVICE_ESP8266_H
#define DEVICE_ESP8266_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define ESP8266_MAX_TCP_PAYLOAD  255U

typedef enum {
    ESP8266_SESSION_STARTING = 0,
    ESP8266_SESSION_READY,
    ESP8266_SESSION_ERROR
} esp8266_session_status_t;

typedef enum {
    ESP8266_ERROR_NONE = 0,
    ESP8266_ERROR_AT_TIMEOUT,
    ESP8266_ERROR_AT_REJECTED,
    ESP8266_ERROR_TCP_CONNECT_TIMEOUT,
    ESP8266_ERROR_TCP_CONNECT_REJECTED,
    ESP8266_ERROR_TCP_CLOSED,
    ESP8266_ERROR_SEND_TIMEOUT,
    ESP8266_ERROR_SEND_REJECTED,
    ESP8266_ERROR_UART_QUEUE
} esp8266_error_t;

typedef struct esp8266 esp8266_t;

struct esp8266 {
    uint8_t state;
    uint8_t flags;
    esp8266_error_t error;
    uint32_t deadline_ms;
    char line[128];
    uint16_t line_length;
    uint16_t ipd_length;
    uint16_t ipd_remaining;
    bool parsing_ipd_length;
    uint8_t rx_payload[ESP8266_MAX_TCP_PAYLOAD];
    uint16_t rx_read;
    uint16_t rx_write;
    uint8_t tx_payload[ESP8266_MAX_TCP_PAYLOAD];
    uint16_t tx_length;
    bool tx_pending;
    uint32_t rx_bytes;
    uint32_t rx_dropped;
    uint32_t tx_frames;
    uint32_t tx_rejected;
};

void esp8266_init(esp8266_t *device, uint32_t now_ms);
void esp8266_poll(esp8266_t *device, uint32_t now_ms);
esp8266_session_status_t esp8266_status(const esp8266_t *device);
esp8266_error_t esp8266_error(const esp8266_t *device);
bool esp8266_send(esp8266_t *device, const uint8_t *payload, size_t length);
bool esp8266_read(esp8266_t *device, uint8_t *value);

#endif
