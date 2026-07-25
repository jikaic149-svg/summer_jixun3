#include "esp8266.h"

#include <stddef.h>
#include <string.h>

#include "bsp_uart_bridge.h"
#include "lcd_demo_config.h"

#define STRINGIFY_INNER(value)  #value
#define STRINGIFY(value)        STRINGIFY_INNER(value)

enum {
    STATE_BOOT_WAIT = 0,
    STATE_SEND_AT,
    STATE_WAIT_AT,
    STATE_SEND_MODE,
    STATE_WAIT_MODE,
    STATE_SEND_MUX,
    STATE_WAIT_MUX,
    STATE_WAIT_WIFI,
    STATE_SEND_CONNECT,
    STATE_WAIT_CONNECT,
    STATE_READY,
    STATE_SEND_LENGTH,
    STATE_WAIT_PROMPT,
    STATE_SEND_PAYLOAD,
    STATE_WAIT_SEND,
    STATE_RETRY_WAIT,
    STATE_ERROR
};

enum {
    FLAG_OK = 1U << 0,
    FLAG_ERROR = 1U << 1,
    FLAG_CONNECT = 1U << 2,
    FLAG_CLOSED = 1U << 3,
    FLAG_PROMPT = 1U << 4,
    FLAG_SEND_OK = 1U << 5,
    FLAG_WIFI_GOT_IP = 1U << 6
};

static bool deadline_reached(uint32_t now_ms, uint32_t deadline_ms)
{
    return (int32_t)(now_ms - deadline_ms) >= 0;
}

static bool send_text(const char *text)
{
    return bsp_uart_bridge_esp_write((const uint8_t *)text, strlen(text));
}

static void fail(esp8266_t *device,
                 esp8266_error_t error,
                 uint32_t now_ms)
{
    device->error = error;
    device->tx_pending = false;
    device->tx_length = 0U;
    device->line_length = 0U;
    device->ipd_length = 0U;
    device->ipd_remaining = 0U;
    device->parsing_ipd_length = false;
    device->flags &= FLAG_WIFI_GOT_IP;
    device->deadline_ms = now_ms + ESP8266_RECONNECT_INTERVAL_MS;
    device->state = STATE_RETRY_WAIT;
}

static void clear_response_flags(esp8266_t *device)
{
    device->flags &= FLAG_WIFI_GOT_IP;
}

static void accept_line(esp8266_t *device)
{
    device->line[device->line_length] = '\0';
    if (strcmp(device->line, "OK") == 0) {
        device->flags |= FLAG_OK;
    } else if ((strcmp(device->line, "ERROR") == 0) ||
               (strcmp(device->line, "FAIL") == 0)) {
        device->flags |= FLAG_ERROR;
    } else if ((strcmp(device->line, "CONNECT") == 0) ||
               (strcmp(device->line, "ALREADY CONNECTED") == 0)) {
        device->flags |= FLAG_CONNECT;
    } else if (strcmp(device->line, "CLOSED") == 0) {
        device->flags |= FLAG_CLOSED;
    } else if (strcmp(device->line, "SEND OK") == 0) {
        device->flags |= FLAG_SEND_OK;
    } else if (strcmp(device->line, "WIFI GOT IP") == 0) {
        device->flags |= FLAG_WIFI_GOT_IP;
    } else if (strcmp(device->line, "WIFI DISCONNECT") == 0) {
        device->flags &= (uint8_t)~FLAG_WIFI_GOT_IP;
    }
    device->line_length = 0U;
}

static void push_payload(esp8266_t *device, uint8_t value)
{
    const uint16_t next = (uint16_t)((device->rx_write + 1U) %
                                     ESP8266_MAX_TCP_PAYLOAD);
    if (next == device->rx_read) {
        ++device->rx_dropped;
        return;
    }
    device->rx_payload[device->rx_write] = value;
    device->rx_write = next;
    ++device->rx_bytes;
}

static void parse_byte(esp8266_t *device, uint8_t value)
{
    static const char ipd_prefix[] = "+IPD,";

    if (device->ipd_remaining > 0U) {
        push_payload(device, value);
        --device->ipd_remaining;
        return;
    }

    if (device->parsing_ipd_length) {
        if ((value >= (uint8_t)'0') && (value <= (uint8_t)'9')) {
            const uint32_t length = (uint32_t)device->ipd_length * 10U +
                                    (uint32_t)(value - (uint8_t)'0');
            if (length <= 65535U) {
                device->ipd_length = (uint16_t)length;
            }
        } else if (value == (uint8_t)':') {
            device->ipd_remaining = device->ipd_length;
            device->ipd_length = 0U;
            device->parsing_ipd_length = false;
        } else {
            device->ipd_length = 0U;
            device->parsing_ipd_length = false;
        }
        return;
    }

    if (value == (uint8_t)'>') {
        device->flags |= FLAG_PROMPT;
        return;
    }
    if (value == (uint8_t)'\r') {
        return;
    }
    if (value == (uint8_t)'\n') {
        if (device->line_length > 0U) {
            accept_line(device);
        }
        return;
    }
    if (device->line_length < sizeof(device->line) - 1U) {
        device->line[device->line_length++] = (char)value;
        if ((device->line_length == sizeof(ipd_prefix) - 1U) &&
            (memcmp(device->line, ipd_prefix,
                    sizeof(ipd_prefix) - 1U) == 0)) {
            device->line_length = 0U;
            device->ipd_length = 0U;
            device->parsing_ipd_length = true;
        }
    } else {
        device->line_length = 0U;
    }
}

static bool send_length_command(uint16_t length)
{
    char command[24];
    char reverse[6];
    size_t cursor = 0U;
    size_t count = 0U;

    memcpy(command, "AT+CIPSEND=", 11U);
    cursor = 11U;
    do {
        reverse[count++] = (char)('0' + (length % 10U));
        length = (uint16_t)(length / 10U);
    } while (length > 0U);
    while (count > 0U) {
        command[cursor++] = reverse[--count];
    }
    command[cursor++] = '\r';
    command[cursor++] = '\n';
    return bsp_uart_bridge_esp_write((const uint8_t *)command, cursor);
}

void esp8266_init(esp8266_t *device, uint32_t now_ms)
{
    if (device == NULL) {
        return;
    }
    memset(device, 0, sizeof(*device));
    bsp_uart_bridge_claim_esp();
    device->state = STATE_BOOT_WAIT;
    device->deadline_ms = now_ms + ESP8266_BOOT_WAIT_MS;
}

void esp8266_poll(esp8266_t *device, uint32_t now_ms)
{
    uint8_t value;

    if (device == NULL) {
        return;
    }
    while (bsp_uart_bridge_esp_read(&value)) {
        parse_byte(device, value);
    }
    if ((device->flags & FLAG_CLOSED) != 0U) {
        fail(device, ESP8266_ERROR_TCP_CLOSED, now_ms);
    }

    switch (device->state) {
    case STATE_BOOT_WAIT:
        if (deadline_reached(now_ms, device->deadline_ms)) {
            device->state = STATE_SEND_AT;
        }
        break;
    case STATE_SEND_AT:
        clear_response_flags(device);
        if (send_text("AT\r\n")) {
            device->deadline_ms = now_ms + ESP8266_AT_TIMEOUT_MS;
            device->state = STATE_WAIT_AT;
        }
        break;
    case STATE_WAIT_AT:
        if ((device->flags & FLAG_OK) != 0U) {
            device->state = STATE_SEND_MODE;
        } else if ((device->flags & FLAG_ERROR) != 0U) {
            fail(device, ESP8266_ERROR_AT_REJECTED, now_ms);
        } else if (deadline_reached(now_ms, device->deadline_ms)) {
            fail(device, ESP8266_ERROR_AT_TIMEOUT, now_ms);
        }
        break;
    case STATE_SEND_MODE:
        clear_response_flags(device);
        if (send_text("AT+CIPMODE=0\r\n")) {
            device->deadline_ms = now_ms + ESP8266_AT_TIMEOUT_MS;
            device->state = STATE_WAIT_MODE;
        }
        break;
    case STATE_WAIT_MODE:
        if ((device->flags & FLAG_OK) != 0U) {
            device->state = STATE_SEND_MUX;
        } else if ((device->flags & FLAG_ERROR) != 0U) {
            fail(device, ESP8266_ERROR_AT_REJECTED, now_ms);
        } else if (deadline_reached(now_ms, device->deadline_ms)) {
            fail(device, ESP8266_ERROR_AT_TIMEOUT, now_ms);
        }
        break;
    case STATE_SEND_MUX:
        clear_response_flags(device);
        if (send_text("AT+CIPMUX=0\r\n")) {
            device->deadline_ms = now_ms + ESP8266_AT_TIMEOUT_MS;
            device->state = STATE_WAIT_MUX;
        }
        break;
    case STATE_WAIT_MUX:
        if ((device->flags & FLAG_OK) != 0U) {
            device->deadline_ms = now_ms + ESP8266_TCP_CONNECT_TIMEOUT_MS;
            device->state = STATE_WAIT_WIFI;
        } else if ((device->flags & FLAG_ERROR) != 0U) {
            fail(device, ESP8266_ERROR_AT_REJECTED, now_ms);
        } else if (deadline_reached(now_ms, device->deadline_ms)) {
            fail(device, ESP8266_ERROR_AT_TIMEOUT, now_ms);
        }
        break;
    case STATE_WAIT_WIFI:
        if (((device->flags & FLAG_WIFI_GOT_IP) != 0U) ||
            deadline_reached(now_ms, device->deadline_ms)) {
            device->state = STATE_SEND_CONNECT;
        }
        break;
    case STATE_SEND_CONNECT:
        clear_response_flags(device);
        if (send_text("AT+CIPSTART=\"TCP\",\"" ESP8266_TCP_SERVER_IPV4
                      "\"," STRINGIFY(ESP8266_TCP_SERVER_PORT) "\r\n")) {
            device->deadline_ms = now_ms + ESP8266_TCP_CONNECT_TIMEOUT_MS;
            device->state = STATE_WAIT_CONNECT;
        }
        break;
    case STATE_WAIT_CONNECT:
        if ((device->flags & FLAG_CONNECT) != 0U) {
            device->error = ESP8266_ERROR_NONE;
            device->state = STATE_READY;
        } else if ((device->flags & FLAG_ERROR) != 0U) {
            fail(device, ESP8266_ERROR_TCP_CONNECT_REJECTED, now_ms);
        } else if (deadline_reached(now_ms, device->deadline_ms)) {
            fail(device, ESP8266_ERROR_TCP_CONNECT_TIMEOUT, now_ms);
        }
        break;
    case STATE_READY:
        if (device->tx_pending) {
            device->state = STATE_SEND_LENGTH;
        }
        break;
    case STATE_SEND_LENGTH:
        clear_response_flags(device);
        if (send_length_command(device->tx_length)) {
            device->deadline_ms = now_ms + ESP8266_AT_TIMEOUT_MS;
            device->state = STATE_WAIT_PROMPT;
        }
        break;
    case STATE_WAIT_PROMPT:
        if ((device->flags & FLAG_PROMPT) != 0U) {
            device->state = STATE_SEND_PAYLOAD;
        } else if ((device->flags & FLAG_ERROR) != 0U) {
            ++device->tx_rejected;
            fail(device, ESP8266_ERROR_SEND_REJECTED, now_ms);
        } else if (deadline_reached(now_ms, device->deadline_ms)) {
            ++device->tx_rejected;
            fail(device, ESP8266_ERROR_SEND_TIMEOUT, now_ms);
        }
        break;
    case STATE_SEND_PAYLOAD:
        clear_response_flags(device);
        if (bsp_uart_bridge_esp_write(device->tx_payload,
                                      device->tx_length)) {
            device->deadline_ms = now_ms + ESP8266_AT_TIMEOUT_MS;
            device->state = STATE_WAIT_SEND;
        }
        break;
    case STATE_WAIT_SEND:
        if ((device->flags & FLAG_SEND_OK) != 0U) {
            device->tx_pending = false;
            ++device->tx_frames;
            device->state = STATE_READY;
        } else if ((device->flags & FLAG_ERROR) != 0U) {
            ++device->tx_rejected;
            fail(device, ESP8266_ERROR_SEND_REJECTED, now_ms);
        } else if (deadline_reached(now_ms, device->deadline_ms)) {
            ++device->tx_rejected;
            fail(device, ESP8266_ERROR_SEND_TIMEOUT, now_ms);
        }
        break;
    case STATE_RETRY_WAIT:
        if (deadline_reached(now_ms, device->deadline_ms)) {
            device->state = STATE_SEND_AT;
        }
        break;
    case STATE_ERROR:
    default:
        break;
    }
}

esp8266_session_status_t esp8266_status(const esp8266_t *device)
{
    if ((device != NULL) &&
        (device->state >= STATE_READY) &&
        (device->state <= STATE_WAIT_SEND)) {
        return ESP8266_SESSION_READY;
    }
    if ((device == NULL) || (device->state == STATE_ERROR)) {
        return ESP8266_SESSION_ERROR;
    }
    return ESP8266_SESSION_STARTING;
}

esp8266_error_t esp8266_error(const esp8266_t *device)
{
    return (device != NULL) ? device->error : ESP8266_ERROR_UART_QUEUE;
}

bool esp8266_send(esp8266_t *device, const uint8_t *payload, size_t length)
{
    if ((device == NULL) || (payload == NULL) || (length == 0U) ||
        (length > sizeof(device->tx_payload)) || device->tx_pending ||
        (device->state != STATE_READY)) {
        return false;
    }
    memcpy(device->tx_payload, payload, length);
    device->tx_length = (uint16_t)length;
    device->tx_pending = true;
    return true;
}

bool esp8266_read(esp8266_t *device, uint8_t *value)
{
    if ((device == NULL) || (value == NULL) ||
        (device->rx_read == device->rx_write)) {
        return false;
    }
    *value = device->rx_payload[device->rx_read];
    device->rx_read = (uint16_t)((device->rx_read + 1U) %
                                 ESP8266_MAX_TCP_PAYLOAD);
    return true;
}
