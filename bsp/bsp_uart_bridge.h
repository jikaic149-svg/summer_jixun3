#ifndef BSP_UART_BRIDGE_H
#define BSP_UART_BRIDGE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint32_t pc_to_esp_bytes;
    uint32_t esp_to_pc_bytes;
    uint32_t app_to_esp_bytes;
    uint32_t esp_to_app_bytes;
    uint32_t pc_to_esp_dropped;
    uint32_t esp_to_pc_dropped;
    uint32_t app_to_esp_dropped;
    uint32_t esp_to_app_dropped;
    uint32_t usart1_errors;
    uint32_t usart3_errors;
} bsp_uart_bridge_stats_t;

/*
 * Development-only byte bridge:
 * DAP VCOM <-> USART1 (PA9/PA10) <-> USART3 (PB10/PB11) <-> ESP8266.
 */
void bsp_uart_bridge_init(void);
void bsp_uart_bridge_claim_esp(void);
bool bsp_uart_bridge_esp_write(const uint8_t *data, size_t length);
bool bsp_uart_bridge_esp_read(uint8_t *value);
void bsp_uart_bridge_get_stats(bsp_uart_bridge_stats_t *stats);

#endif
