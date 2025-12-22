#include "products.h"
#include "./flash/bsp_spi_flash.h" // 引用底层驱动

// 内部缓存变量，避免频繁读取元数据
static uint32_t g_cached_total_count = 0;

/**
 * @brief  初始化商品管理器
 */
void Product_Manager_Init(void)
{
    SPI_FLASH_Init();        // 初始化 SPI Flash (bsp_spi_flash.c)
    // 上电时读取一次元数据，获取当前商品总数
    Product_Metadata_t meta;
    SPI_FLASH_BufferRead((uint8_t *)&meta, FLASH_ADDR_METADATA, sizeof(meta));

    if (meta.magic == PRODUCT_MAGIC_VALID)
    {
        g_cached_total_count = meta.total_count;
        printf("[Product] DB Init. Total Items: %d\r\n", g_cached_total_count);
    }
    else
    {
        g_cached_total_count = 0;
        printf("[Product] DB Empty or Invalid.\r\n");
    }

    printf("\r\n");
    printf("============================================\r\n");
    printf("   STM32 Smart Supermarket System V3.0      \r\n");
    printf("   SPI Flash Database Loaded.               \r\n");
    printf("============================================\r\n");

    // 打印当前 Flash ID 以确认硬件连接正常
    printf("Hardware Check: Flash ID = 0x%X\r\n", SPI_FLASH_ReadID());
}

/**
 * @brief  清空/格式化数据库 (用于同步开始时)
 */
void Product_Clear_Database(void)
{
    printf("[Product] Erasing Database...\r\n");
    // 1. 擦除元数据区 (Sector 0)
    SPI_FLASH_SectorErase(FLASH_ADDR_METADATA);

    // 2. 擦除数据区
    // 根据实际情况，擦除足够的扇区。W25Q64 一个扇区 4KB，存 64字节商品可存 64个。
    // 假设最大 5000 个商品 -> 5000 * 64 = 320,000 字节 ≈ 312 KB ≈ 79 个扇区
    // 这里简单演示循环擦除前 10 个扇区，实际建议根据 g_cached_total_count 动态计算
    uint32_t i;
    for (i = 0; i < 20; i++)
    {
        SPI_FLASH_SectorErase(FLASH_ADDR_DB_START + (i * 4096)); // 4096 is Sector Size
    }

    g_cached_total_count = 0;
    printf("[Product] Erase Done.\r\n");
}

/**
 * @brief  更新商品总数 (用于同步结束时)
 */
void Product_Update_Metadata(uint32_t count)
{
    Product_Metadata_t meta;
    meta.total_count = count;
    meta.version = 0x0100;
    meta.magic = PRODUCT_MAGIC_VALID;

    // 写入 Sector 0
    SPI_FLASH_BufferWrite((uint8_t *)&meta, FLASH_ADDR_METADATA, sizeof(meta));
    g_cached_total_count = count;
    printf("[Product] Metadata Updated. Total: %d\r\n", count);
}

/**
 * @brief  写入单个商品
 * @param  index: 存储序号 (0, 1, 2...)
 */
void Product_Write_Item(uint32_t index, uint64_t id, float price, char *name)
{
    Product_Item_t item;

    // 1. 填充结构体
    item.id = id;
    item.price = price;
    item.magic = PRODUCT_MAGIC_VALID;

    memset(item.name, 0, sizeof(item.name));
    strncpy(item.name, name, sizeof(item.name) - 1);

    // 2. 计算地址
    uint32_t write_addr = FLASH_ADDR_DB_START + (index * ITEM_SIZE);

    // 3. 写入 Flash
    SPI_FLASH_BufferWrite((uint8_t *)&item, write_addr, ITEM_SIZE);
}

/**
 * @brief  根据索引读取商品
 * @return 1=成功, 0=无效数据
 */
uint8_t Product_Read_ByIndex(uint32_t index, Product_Item_t *out_item)
{
    uint32_t addr = FLASH_ADDR_DB_START + (index * ITEM_SIZE);

    // 读取
    SPI_FLASH_BufferRead((uint8_t *)out_item, addr, ITEM_SIZE);

    // 校验
    if (out_item->magic == PRODUCT_MAGIC_VALID)
    {
        return 1;
    }
    return 0;
}

/**
 * @brief  [核心] 根据 ID 查找商品
 * @note   这是线性查找，速度取决于 Flash 读取速度。
 * 优化思路：在 RAM 中建立索引表 (ID -> Address)。
 * @return 1=找到, 0=未找到
 */
uint8_t Product_Find_By_ID(uint64_t target_id, Product_Item_t *out_item)
{
    uint32_t i;

    // 如果数据库为空，直接返回
    if (g_cached_total_count == 0)
        return 0;

    // 遍历搜索
    for (i = 0; i < g_cached_total_count; i++)
    {
        // 计算地址
        uint32_t addr = FLASH_ADDR_DB_START + (i * ITEM_SIZE);

        // 优化：先只读前 4 个字节 (ID)，如果匹配再读剩下的
        // 这样比每次读 64 字节快很多
        uint64_t read_id;
        SPI_FLASH_BufferRead((uint8_t *)&read_id, addr, 8);

        if (read_id == target_id)
        {
            // ID 匹配！读取完整信息
            SPI_FLASH_BufferRead((uint8_t *)out_item, addr, ITEM_SIZE);

            // 二次确认 magic (防止读到坏数据)
            if (out_item->magic == PRODUCT_MAGIC_VALID)
            {
                return 1; // 找到了
            }
        }
    }

    return 0; // 遍历完都没找到
}

void Product_Get_All_Info(Product_Item_t* list, int totalItems)
{
    Product_Item_t item;
    uint32_t i;
    totalItems = 0;
    for (i = 0; i < totalItems; i++)
    {
        if (Product_Read_ByIndex(i, &item))
        {
            list[i] = item;
            totalItems++;
        }
        else
        {
            // 遇到无效数据提前退出
            if (i > g_cached_total_count)
                break;
        }
    }
}

/**
 * @brief  调试：打印所有数据
 */
void Product_Debug_Dump_All(void)
{
    Product_Item_t item;
    uint32_t i;

    printf("\r\n--- Product Dump ---\r\n");

    // 使用 cached_count 避免读取空数据
    uint32_t limit = (g_cached_total_count > 0) ? g_cached_total_count : 100;

    for (i = 0; i < limit; i++)
    {
        if (Product_Read_ByIndex(i, &item))
        {
            printf("[%d] ID:%llu, Price:%.2f, Name:%s\r\n",
                   i,
                   (unsigned long long)item.id,
                   item.price,
                   item.name);
        }
        else
        {
            // 遇到无效数据提前退出
            if (i > g_cached_total_count)
                break;
        }
    }
    printf("--- End ---\r\n");
}
