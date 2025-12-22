#ifndef __PROTOCOL_H
#define __PROTOCOL_H

#include "stm32f10x.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>

// ==========================================
// 1. Flash 存储结构定义 (定长 64字节)
// ==========================================
// 256字节(一页) / 64 = 4，完美对齐，无跨页写入风险
typedef struct {
    uint64_t id;          // 商品条码 (支持 EAN-13 等 13 位条码)
    float    price;       // 价格
    char     name[48];    // 商品名称 (UTF-8)
    uint32_t magic;       // 有效标志位 (0xA5A5A5A5)
} Product_Flash_Store_t;

typedef char Product_Flash_Store_t_size_must_be_64_bytes[(sizeof(Product_Flash_Store_t) == 64) ? 1 : -1];

// Flash 地址规划
#define FLASH_SECTOR_METADATA   0x000000  // 扇区0: 存放元数据(总数等)
#define FLASH_SECTOR_DATA_BASE  0x001000  // 扇区1: 开始存放商品数据
#define ITEM_STORE_SIZE         sizeof(Product_Flash_Store_t)

// ==========================================
// 2. 串口协议定义
// ==========================================
#define RING_BUFFER_SIZE  1024  // 加大缓冲区，防止擦除Flash时溢出
#define LINE_BUFFER_SIZE  128

// 解析出的事件类型
typedef enum {
    EVENT_NONE = 0,
    EVENT_SYNC_START,       // CMD:SYNC_START
    EVENT_SYNC_DATA,        // CMD:SYNC_DATA
    EVENT_SYNC_END,         // CMD:SYNC_END
    EVENT_SCAN              // CMD:SCAN
} ProtocolEvent_t;

// 解析结果包
typedef struct {
    ProtocolEvent_t event;
    uint32_t total_count;   // 对应 TOTAL/SUM
    uint64_t id;            // 对应 ID
    uint8_t id_valid;       // 1=ID解析成功(纯数字且未溢出), 0=无效
    float price;            // 对应 PR
    char name[48];          // 对应 NM
} ParsedPacket_t;

// 协议管理器句柄
typedef struct {
    uint8_t ring_buf[RING_BUFFER_SIZE];
    volatile uint16_t head;
    volatile uint16_t tail;
    
    char line_buf[LINE_BUFFER_SIZE];
    uint16_t line_idx;
} ProtocolManager_t;

// API
void Protocol_Init(void);
void Protocol_Receive_Byte_IRQ(uint8_t byte);
uint8_t Protocol_Parse_Line(ParsedPacket_t *out_packet);

#endif
