#ifndef __PRODUCT_H
#define __PRODUCT_H

#include "stm32f10x.h"
#include <string.h>
#include <stdio.h>
#include <stdint.h>

// ==========================================
// 1. 配置与内存映射
// ==========================================
// 有效标记 (用于判断 Flash 该位置是否有数据)
#define PRODUCT_MAGIC_VALID     0xA5A5A5A5
#define PRODUCT_MAGIC_EMPTY     0xFFFFFFFF 

// Flash 地址规划 (基于 W25Q64)
// 扇区 0 (0x000000 - 0x000FFF): 存放系统元数据 (商品总数、版本等)
#define FLASH_ADDR_METADATA     0x000000  
// 扇区 1 (0x001000) 开始: 存放具体商品数据
#define FLASH_ADDR_DB_START     0x001000  

// 最大支持商品数量 (防止遍历死循环)
#define PRODUCT_MAX_COUNT       5000  

// ==========================================
// 2. 数据结构定义
// ==========================================

// 元数据结构 (存放在 Sector 0)
typedef struct {
    uint32_t total_count;       // 当前存储的商品总数
    uint32_t update_timestamp;  // 更新时间戳 (可选)
    uint32_t version;           // 数据库版本
    uint32_t magic;             // 元数据有效标记
} Product_Metadata_t;

// 商品存储结构 (定长 64 字节)
// 必须定长，以便通过 index 直接计算地址
typedef struct {
    uint64_t id;          // 条码 (Key) - 支持 13 位条码
    float    price;       // 价格
    char     name[48];    // 名称 (UTF-8)
    uint32_t magic;       // 有效标记
} Product_Item_t;

typedef char Product_Item_t_size_must_be_64_bytes[(sizeof(Product_Item_t) == 64) ? 1 : -1];

// 获取单个商品占用的 Flash 字节数
#define ITEM_SIZE  sizeof(Product_Item_t)

// ==========================================
// 3. API 函数声明
// ==========================================

/* 初始化 */
void Product_Manager_Init(void);

/* 数据库管理 */
void Product_Clear_Database(void);           // 擦除数据库
void Product_Update_Metadata(uint32_t count);// 更新商品总数

/* 写操作 */
// 将商品写入指定索引位置
void Product_Write_Item(uint32_t index, uint64_t id, float price, char* name);

/* 读/查操作 */
// 根据索引读取 (用于遍历)
uint8_t Product_Read_ByIndex(uint32_t index, Product_Item_t *out_item);
// 根据 ID 查找 (用于扫码) - 核心功能
uint8_t Product_Find_By_ID(uint64_t target_id, Product_Item_t *out_item);

void Product_Get_All_Info(Product_Item_t* list, int totalItems);

/* 调试 */
void Product_Debug_Dump_All(void);

//---------用于MCU内部产品列表刷新---------
#define DATA_BUFFER_VOLUME 66 // MCU端商品数据缓冲区大小
typedef struct{
    Product_Item_t product;
    int num;
} MCU_Product_t; // MCU端商品结构体

#endif /* __PRODUCT_H */
