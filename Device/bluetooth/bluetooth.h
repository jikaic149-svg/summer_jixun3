#ifndef BLUETOOTH_H
#define BLUETOOTH_H

/**
 * @file    bluetooth.h
 * @brief   Device层 — 蓝牙通信模块 (DMA + CRC16 协议)
 * @note    协议格式:
 *          | Header(2B) | CMD(1B) | Length(1B) | Payload(0~N B) | CRC16(2B) |
 *          | 0xA5 0x5A  | cmd_id  | pay_len   |    data...      | crc_h crc_l|
 *
 *          遵循 TI.md Rule 6: packed + Header + Length + Payload + CRC16
 */

#include <stdint.h>
#include <stdbool.h>

/* ======================== 协议常量 ======================== */

#define BT_FRAME_HEADER_0       0xA5
#define BT_FRAME_HEADER_1       0x5A
#define BT_FRAME_OVERHEAD       6U      /* Header(2) + CMD(1) + Len(1) + CRC16(2) */
#define BT_MAX_PAYLOAD_SIZE     28U     /* 最大 Payload 长度 */
#define BT_MAX_FRAME_SIZE       (BT_FRAME_OVERHEAD + BT_MAX_PAYLOAD_SIZE)
#define BT_RX_STATE_BUF_SIZE    64U

/* ======================== 命令 ID 定义 ======================== */

typedef enum
{
    BT_CMD_ATTITUDE     = 0x01,     /* TX: 姿态角 float[4] (Roll, Pitch, Yaw, YawTotal) */
    BT_CMD_RAW_IMU      = 0x02,     /* TX: 原始IMU float[6] (gyro[3] + accel[3]) */
    BT_CMD_CALIB_STATUS = 0x03,     /* TX: 校准状态 uint8_t */
    BT_CMD_HEARTBEAT    = 0x04,     /* TX: 心跳 (无 Payload) */
    BT_CMD_REMOTE       = 0x80,     /* RX: 远程命令 */
} BT_CmdId_e;

/* ======================== 帧结构体 (packed) ======================== */

typedef struct __attribute__((packed))
{
    uint8_t  header[2];     /* 0xA5, 0x5A */
    uint8_t  cmd_id;        /* 命令 ID */
    uint8_t  payload_len;   /* Payload 长度 */
    uint8_t  payload[BT_MAX_PAYLOAD_SIZE]; /* 数据域 */
    /* CRC16 紧跟 payload 之后, 由发送函数动态填充 */
} BT_Frame_t;

/* ======================== 姿态数据包 (packed) ======================== */

typedef struct __attribute__((packed))
{
    float roll;
    float pitch;
    float yaw;
    float yaw_total;
} BT_AttitudePayload_t;

/* ======================== 函数指针类型 ======================== */

typedef bool (*BT_DmaSend_t)(const uint8_t *data, uint16_t len);

/* ======================== 接收状态机 ======================== */

typedef enum
{
    BT_RX_WAIT_HEADER_0,
    BT_RX_WAIT_HEADER_1,
    BT_RX_WAIT_CMD,
    BT_RX_WAIT_LEN,
    BT_RX_WAIT_PAYLOAD,
    BT_RX_WAIT_CRC,
} BT_RxState_e;

/* ======================== 接收回调 ======================== */

typedef void (*BT_RxCmdCallback_t)(uint8_t cmd_id, const uint8_t *payload, uint8_t len);

/* 文本行回调: 上层用来接收 "Yxx.xx\n" 这类 ASCII 命令 */
typedef void (*BT_RxLineCallback_t)(const char *line, uint16_t len);

#define BT_TEXT_LINE_BUF_SIZE  64U

/* ======================== 实例结构体 ======================== */

typedef struct
{
    /* BSP 注入 */
    BT_DmaSend_t dma_send;

    /* 发送缓冲 */
    uint8_t tx_buf[BT_MAX_FRAME_SIZE];

    /* 接收状态机 */
    BT_RxState_e  rx_state;
    uint8_t       rx_cmd;
    uint8_t       rx_len;
    uint8_t       rx_payload[BT_MAX_PAYLOAD_SIZE];
    uint8_t       rx_idx;
    uint8_t       rx_crc_buf[2];
    uint8_t       rx_crc_idx;

    /* 接收回调 */
    BT_RxCmdCallback_t  rx_callback;        /* CRC 协议命令 */
    BT_RxLineCallback_t rx_line_callback;   /* 文本行命令 */

    /* 文本行装配缓冲 */
    uint8_t  line_buf[BT_TEXT_LINE_BUF_SIZE];
    uint16_t line_idx;
} Bluetooth_Instance_t;

/* ======================== 接口函数 ======================== */

/**
 * @brief  初始化蓝牙实例
 * @param  inst     实例指针 (调用者分配)
 * @param  send_fn  DMA 发送函数指针 (BSP 层)
 */
void Bluetooth_Init(Bluetooth_Instance_t *inst, BT_DmaSend_t send_fn);

/**
 * @brief  注册接收命令回调 (CRC 协议)
 */
void Bluetooth_RegisterRxCallback(Bluetooth_Instance_t *inst, BT_RxCmdCallback_t callback);

/**
 * @brief  注册文本行回调 (\n 切包, 去掉 \r\n, NUL 收尾)
 */
void Bluetooth_RegisterLineCallback(Bluetooth_Instance_t *inst, BT_RxLineCallback_t callback);

/**
 * @brief  喂入单字节 (BSP 字节回调里调用)
 *         内部同时跑 CRC 状态机 和 文本行装配, 谁先匹配谁触发
 */
void Bluetooth_FeedRxByte(Bluetooth_Instance_t *inst, uint8_t b);

/**
 * @brief  发送姿态数据
 * @param  inst 实例指针
 * @param  att  姿态数据
 * @retval true=发送成功
 */
bool Bluetooth_SendAttitude(Bluetooth_Instance_t *inst, const BT_AttitudePayload_t *att);

/**
 * @brief  发送通用命令帧
 * @param  inst    实例指针
 * @param  cmd_id  命令 ID
 * @param  payload 数据
 * @param  len     数据长度
 * @retval true=发送成功
 */
bool Bluetooth_SendFrame(Bluetooth_Instance_t *inst, uint8_t cmd_id,
                         const uint8_t *payload, uint8_t len);

/**
 * @brief  喂入接收数据 (由 BSP 回调调用，支持多字节)
 * @param  inst 实例指针
 * @param  data 接收到的数据
 * @param  len  数据长度
 */
void Bluetooth_FeedRxData(Bluetooth_Instance_t *inst, const uint8_t *data, uint16_t len);

#endif /* BLUETOOTH_H */
