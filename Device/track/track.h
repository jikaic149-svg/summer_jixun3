/**
 * @file track.h
 * @brief 8路红外模拟循迹模块驱动（MSPM0 平台）
 * @note  通信协议: 发送 "$0,1,0#" 启动, 接收 "$A,x1:val,...,x8:val#"
 *        使用 BSP 层 UART DMA 接收，上层回调解析
 */
#ifndef TRACK_H
#define TRACK_H

#include <stdint.h>

#define TRACK_SENSOR_COUNT  8
#define TRACK_LOST_LINE     999.0f
#define TRACK_ADC_MAX_VALUE 4095U

typedef void (*Track_UartSend_t)(uint8_t *data, uint16_t len);

typedef struct {
    uint16_t adc_value[TRACK_SENSOR_COUNT];
    float error;
    uint8_t black_count;
    uint8_t black_mask;
    uint32_t frame_count;
    uint32_t version;
} Track_Snapshot_t;

typedef struct {
    uint8_t          rx_buf[128];
    uint16_t         rx_len;
    volatile uint16_t adc_value[TRACK_SENSOR_COUNT];
    volatile uint32_t publish_version;
    volatile uint8_t data_ready;
    volatile uint32_t frame_count;
    volatile uint32_t rx_byte_count;
    volatile uint32_t parse_error_count;
    Track_UartSend_t uart_send;
} Track_Instance;

/**
 * @brief 初始化循迹模块实例
 * @param inst    实例指针（调用者分配）
 * @param send_fn UART 发送函数
 */
void Track_Init(Track_Instance *inst, Track_UartSend_t send_fn);

/**
 * @brief 从接收到的数据包中解析 8 路模拟值
 * @param inst 实例指针
 * @param data 接收缓冲区
 * @param len  数据长度
 */
void Track_ParsePacket(Track_Instance *inst, const uint8_t *data, uint16_t len);
void Track_OnRxByte(uint8_t byte);
uint8_t Track_GetSnapshot(const Track_Instance *inst, Track_Snapshot_t *snapshot);

/**
 * @brief 计算循迹误差（加权重心法）
 * @param inst 实例指针
 * @return 偏差值 (>0 线在左, <0 线在右, 999.0 丢线)
 */
float Track_GetError(Track_Instance *inst);
uint8_t Track_GetBlackCount(Track_Instance *inst);
uint32_t Track_GetFrameCount(const Track_Instance *inst);
uint32_t Track_GetRxByteCount(const Track_Instance *inst);

/**
 * @brief 获取指定通道的原始 ADC 值
 */
static inline uint16_t Track_GetADC(Track_Instance *inst, uint8_t ch) {
    return (ch < TRACK_SENSOR_COUNT) ? inst->adc_value[ch] : 0;
}

#endif /* TRACK_H */
