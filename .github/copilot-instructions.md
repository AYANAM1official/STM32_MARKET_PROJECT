# STM32 智能超市系统 - AI 编码指南

## 项目概述
基于 STM32F103 的无人超市下位机系统，使用 **SPI Flash (W25Q64)** 作为商品数据库，通过 **串口协议** 与上位机通信，实现商品数据同步与扫码查询。集成温湿度传感器 (DS18B20/DHT11)、串口屏 (HMI)、舵机控制等多模块。

**核心流程**: PC 通过串口发送商品数据 → STM32 写入 Flash → 扫码时从 Flash 查询 → 返回商品信息 → 串口屏显示 → 购物车管理

---

## 架构关键点

### 1. 双状态机架构 (`User/main.h`, `User/main.c`)
系统采用**双状态机**并行运行，优先级不同:

**从机状态机 (SlaveState)** - 高优先级，处理 PC 同步
```c
typedef enum {
    SYS_STATE_IDLE,          // 空闲，可扫码
    SYS_STATE_SYNC_START,    // 擦除数据库中
    SYS_STATE_SYNC_ING       // 接收数据中
} SlaveState_t;
```

**商店状态机 (ShoppingState)** - 低优先级，处理本地业务
```c
typedef enum {
    SHOP_STATE_IDLE,             // 空闲，等待扫码
    SHOP_STATE_WORKING,          // 工作中
    SHOP_STATE_SCANNING,         // 扫码中，收集数据
    SHOP_STATE_UPDATING_HMI,     // 更新串口屏
    SHOP_STATE_SYNCING,          // 数据同步中 (屏蔽模块通信)
    SHOP_STATE_WAITING_PAYOFF,   // 等待结算
    // ...更多状态
} ShoppingState_t;
```

**关键**: 数据同步时 `Shop_transtate(SHOP_STATE_SYNCING)` 暂停购物流程，防止冲突

### 2. Flash 数据库设计 (`User/products.c/h`)
- **定长存储**: 每个商品 64 字节 (`Product_Item_t`)，便于索引计算: `地址 = 0x001000 + (index * 64)`
- **地址规划**:
  - `0x000000`: 元数据扇区 (存储商品总数 `total_count`)
  - `0x001000`: 商品数据起始地址
- **查询优化**: `Product_Find_By_ID()` 先只读 4 字节 ID，匹配后再读完整 64 字节，减少 SPI 传输
- **缓存机制**: `g_cached_total_count` 避免频繁读取元数据

### 3. 串口协议 (`User/protocol.c/h`)
- **环形缓冲区**: 中断中将字节存入 `ring_buf[1024]`，主循环解析完整行
- **协议格式**: 
  ```
  CMD:SYNC_START,TOTAL:100    // 开始同步
  CMD:SYNC_DATA,ID:xxx,PR:xxx,NM:xxx  // 商品数据
  CMD:SYNC_END,SUM:100        // 结束同步
  CMD:SCAN,ID:6912345         // 扫码查询
  CMD:REPORT,ID:xxx,PR:xxx,NM:xxx  // 返回查询结果
  CMD:ALARM,LEVEL:x,MSG:xxx   // 错误报警
  ```
- **解析流程**: `Protocol_Receive_Byte_IRQ()` (中断入队) → `Protocol_Parse_Line()` (主循环解析)

### 4. 购物车机制 (`User/main.h`)
- **RAM 购物车**: `MCU_Product_t shopping_car[66]` 存储当前购物车 (商品 + 数量)
- **去重逻辑**: `add_product_to_shopping_car()` 检查 ID 是否存在，存在则 `num++`，否则从 Flash 读取商品信息添加
- **串口屏同步**: `refresh_MCU_products_list()` → `Screen_Load_New_Data()` 更新显示

---

## 关键文件说明

### `User/main.c` - 双状态机控制器
- **主循环结构**: `while(1)` 中先处理温度读取，再处理 `Protocol_Parse_Line()`
- **状态机驱动**: `switch(rx_packet.event)` 处理 4 种协议事件
- **同步流程**: 
  1. `EVENT_SYNC_START` → 切换 `SlaveState` 和 `ShoppingState` → 擦除 Flash (`Product_Clear_Database()`) 
  2. 发送 `CMD:REQ_SYNC\n` 握手信号
  3. 循环接收 `EVENT_SYNC_DATA` → `Product_Write_Item()` 写入
  4. `EVENT_SYNC_END` → 校验数量 → `Product_Update_Metadata()` 更新元数据
- **扫码流程**: `EVENT_SCAN` → `Product_Find_By_ID()` → 成功则 `CMD:REPORT` / 失败则 `CMD:ALARM`

### `User/main.h` - 全局状态定义
- **状态转换函数**: `Shop_transtate()`, `Slave_transtate()` 带历史记录
- **购物车实现**: `shopping_car[]` 数组 + `add_product_to_shopping_car()` 去重逻辑
- **传感器数据**: `temper` (DS18B20), `humidity` (DHT11)，定义最大阈值 `MAX_TEMPER=35`, `MAX_HUMIDITY=100`

### `User/products.c` - Flash 数据库管理
- `Product_Manager_Init()`: 上电时读取元数据，恢复 `total_count`，打印启动信息
- `Product_Clear_Database()`: 擦除元数据 + 前 20 个扇区 (80KB)，**耗时约 500ms**
- `Product_Find_By_ID()`: **性能关键** - 线性查找 O(n)，先读 4 字节 ID 再读完整结构
- `Product_Write_Item()`: 计算地址 `FLASH_ADDR_DB_START + (index * ITEM_SIZE)` 后写入

### `User/protocol.c` - 协议解析器
- `Get_Value_By_Key()`: 从 `CMD:xxx,KEY:value,KEY2:value2` 提取值，逗号分隔
- `Protocol_Parse_Line()`: 
  - 从环形缓冲区逐字节拼行，遇到 `\n` 触发解析
  - 使用 `strstr()` 匹配命令字符串 (`CMD:SYNC_START`, `CMD:SCAN` 等)
  - 返回 `ParsedPacket_t` 包含 `event` 枚举和解析字段

### `User/stm32f10x_it.c` - 中断服务
- `USART1_IRQHandler()`: 
  - 检查 `USART_IT_RXNE` 标志
  - 读取字节 → `Protocol_Receive_Byte_IRQ()` 入队
  - **仅做入队，不做解析**，保证中断响应速度

### 外设驱动模块
- `User/flash/bsp_spi_flash.c`: W25Q64 驱动，提供 `SPI_FLASH_SectorErase()`, `SPI_FLASH_BufferWrite/Read()`
- `User/screen/screen.c`: 串口屏驱动，`Screen_Load_New_Data()` 刷新显示
- `User/DS18B20/`, `User/DHT11/`: 温湿度传感器驱动
- `User/sg90.c`: 舵机控制 (用于门禁)

---

## 编码规范

### 命名约定
- **函数**: `模块_动作_对象()` 如 `Product_Write_Item()`, `Protocol_Parse_Line()`
- **全局变量**: `g_` 前缀，如 `g_cached_total_count`
- **常量**: 全大写，如 `FLASH_ADDR_METADATA`, `PRODUCT_MAGIC_VALID`

### Flash 操作模式
```c
// 写入前必须擦除扇区 (W25Q64 特性)
SPI_FLASH_SectorErase(address);  // 擦除 4KB 扇区
SPI_FLASH_BufferWrite(data, address, size);  // 写入数据

// 读取无需擦除
SPI_FLASH_BufferRead(buffer, address, size);
```

### 中断安全
- **入队操作**: 在 `USART1_IRQHandler()` 中只调用 `Protocol_Receive_Byte_IRQ()`，不做复杂逻辑
- **解析操作**: 在 `main()` 循环中调用 `Protocol_Parse_Line()`，避免中断阻塞

---

## 关键文件说明

### `User/main.c` - 双状态机控制器
- **主循环结构**: `while(1)` 中先处理温度读取，再处理 `Protocol_Parse_Line()`
- **状态机驱动**: `switch(rx_packet.event)` 处理 4 种协议事件
- **同步流程**: 
  1. `EVENT_SYNC_START` → 切换 `SlaveState` 和 `ShoppingState` → 擦除 Flash (`Product_Clear_Database()`) 
  2. 发送 `CMD:REQ_SYNC\n` 握手信号
  3. 循环接收 `EVENT_SYNC_DATA` → `Product_Write_Item()` 写入
  4. `EVENT_SYNC_END` → 校验数量 → `Product_Update_Metadata()` 更新元数据
- **扫码流程**: `EVENT_SCAN` → `Product_Find_By_ID()` → 成功则 `CMD:REPORT` / 失败则 `CMD:ALARM`

### `User/main.h` - 全局状态定义
- **状态转换函数**: `Shop_transtate()`, `Slave_transtate()` 带历史记录
- **购物车实现**: `shopping_car[]` 数组 + `add_product_to_shopping_car()` 去重逻辑
- **传感器数据**: `temper` (DS18B20), `humidity` (DHT11)，定义最大阈值 `MAX_TEMPER=35`, `MAX_HUMIDITY=100`

### `User/products.c` - Flash 数据库管理
- `Product_Manager_Init()`: 上电时读取元数据，恢复 `total_count`，打印启动信息
- `Product_Clear_Database()`: 擦除元数据 + 前 20 个扇区 (80KB)，**耗时约 500ms**
- `Product_Find_By_ID()`: **性能关键** - 线性查找 O(n)，先读 4 字节 ID 再读完整结构
- `Product_Write_Item()`: 计算地址 `FLASH_ADDR_DB_START + (index * ITEM_SIZE)` 后写入

### `User/protocol.c` - 协议解析器
- `Get_Value_By_Key()`: 从 `CMD:xxx,KEY:value,KEY2:value2` 提取值，逗号分隔
- `Protocol_Parse_Line()`: 
  - 从环形缓冲区逐字节拼行，遇到 `\n` 触发解析
  - 使用 `strstr()` 匹配命令字符串 (`CMD:SYNC_START`, `CMD:SCAN` 等)
  - 返回 `ParsedPacket_t` 包含 `event` 枚举和解析字段

### `User/stm32f10x_it.c` - 中断服务
- `USART1_IRQHandler()`: 
  - 检查 `USART_IT_RXNE` 标志
  - 读取字节 → `Protocol_Receive_Byte_IRQ()` 入队
  - **仅做入队，不做解析**，保证中断响应速度

### 外设驱动模块
- `User/flash/bsp_spi_flash.c`: W25Q64 驱动，提供 `SPI_FLASH_SectorErase()`, `SPI_FLASH_BufferWrite/Read()`
- `User/screen/screen.c`: 串口屏驱动，`Screen_Load_New_Data()` 刷新显示
- `User/DS18B20/`, `User/DHT11/`: 温湿度传感器驱动
- `User/sg90.c`: 舵机控制 (用于门禁)

---

## 常见任务

### 添加新协议命令
1. 在 `protocol.h` 的 `ProtocolEvent_t` 枚举添加新事件 (如 `EVENT_CLEAR_CART`)
2. 在 `Protocol_Parse_Line()` 中添加识别逻辑:
   ```c
   else if (strstr(g_protocol.line_buf, "CMD:CLEAR_CART")) {
       out_packet->event = EVENT_CLEAR_CART;
       return 1;
   }
   ```
3. 在 `main.c` 的 `switch(rx_packet.event)` 添加处理分支
4. 必要时发送响应: `printf("CMD:ACK,MSG:Cart_Cleared\n");`

### 优化查询性能
**当前**: 线性查找 O(n)，1000 个商品平均需 500 次 SPI 读取。

**优化方案**:
- **方案 1 (推荐)**: Flash 中按 ID 排序存储 + 二分查找 O(log n)
  - 修改 `Product_Write_Item()` 插入时保持有序
  - 重写 `Product_Find_By_ID()` 使用二分算法
- **方案 2**: RAM 哈希表，启动时建立 ID→地址映射 (需 8KB+ RAM)
- **方案 3**: Flash 前 20KB 存索引区 (ID + 地址对)

### 扩展购物车功能
示例：添加删除商品功能
```c
void remove_product_from_cart(uint32_t id) {
    for(int i=0; i<total_products; i++){
        if(shopping_car[i].product.id == id){
            if(shopping_car[i].num > 1) {
                shopping_car[i].num--;  // 数量减 1
            } else {
                // 删除该商品，后续元素前移
                for(int j=i; j<total_products-1; j++){
                    shopping_car[j] = shopping_car[j+1];
                }
                total_products--;
            }
            refresh_MCU_products_list();  // 同步串口屏
            break;
        }
    }
}
```

### 调试工具与技巧
- **Flash 验证**: `SPI_FLASH_ReadID()` 返回 `0xEF4017` (W25Q64 ID)
- **数据库查看**: `Product_Debug_Dump_All()` 打印所有商品 (用于测试)
- **协议调试**: 串口助手发送 `CMD:SCAN,ID:123456\n` 测试查询
- **状态监控**: 在状态转换时打印当前状态: 
  ```c
  printf("[State] Slave:%d Shop:%d\r\n", SlaveState, ShoppingState);
  ```

---

## 约束与注意事项

### 硬件限制
- **Flash 寿命**: W25Q64 擦写次数 10 万次，避免频繁擦除同一扇区
- **RAM 容量**: STM32F103ZE 有 64KB RAM，购物车 `shopping_car[66]` 占用约 5KB
- **SPI 速度**: 当前配置约 18 MHz，读取 64 字节需 ~35μs

### 软件约束
- **定长结构**: `Product_Item_t` 必须 64 字节，修改字段需重新计算所有地址
- **缓冲区溢出**: `RING_BUFFER_SIZE=1024`，擦除 Flash 时 (500ms) 若数据超 1KB 会丢失
- **中文编码**: 商品名称 UTF-8，52 字节最多存约 17 个中文字符
- **同步阻塞**: `Product_Clear_Database()` 在主循环中阻塞约 500ms，期间无法处理其他任务

### 多模块冲突防护
- **同步期间**: `SHOP_STATE_SYNCING` 禁用温湿度读取、串口屏刷新、舵机控制
- **串口资源**: 只有 USART1 用于协议通信 + 调试输出，注意 `printf()` 可能延迟协议响应
- **状态检查**: 执行操作前必须检查 `SlaveState == SYS_STATE_IDLE` 和 `ShoppingState != SHOP_STATE_SYNCING`

---

## 构建与调试

### 开发环境
- **IDE**: Keil MDK (uVision5)
- **项目文件**: `Project/RVMDK（uv5）/BH-F103.uvprojx`
- **调试器**: J-Link (配置在 `JLinkSettings.ini`)
- **编译选项**: 使用 C99 标准，启用 `-O2` 优化

### 调试流程
1. **编译**: 在 Keil 中按 `F7` 编译，检查 Output 窗口错误
2. **下载**: 按 `F8` 下载到板卡 (通过 J-Link)
3. **串口连接**: 
   - 波特率 115200，格式 8N1
   - 使用串口助手 (如 XCOM, Putty) 连接 USART1
4. **测试协议**:
   ```
   CMD:SYNC_START,TOTAL:5
   CMD:SYNC_DATA,ID:6912345,PR:5.99,NM:可乐
   ...
   CMD:SYNC_END,SUM:5
   CMD:SCAN,ID:6912345
   ```
5. **查看输出**: 观察终端打印的 `[Product]`, `[Scan]` 等日志

### 常见问题排查
| 现象 | 可能原因 | 解决方法 |
|------|----------|----------|
| Flash ID 读取失败 | SPI 硬件连接问题 | 检查 `SPI_FLASH_ReadID()` 返回值，应为 `0xEF4017` |
| 同步后查询失败 | 数据未写入或地址错误 | 调用 `Product_Debug_Dump_All()` 查看 Flash 内容 |
| 串口无输出 | USART 未初始化或中断未开启 | 检查 `Setup_USART_Interrupt()` 是否调用 |
| 购物车不更新 | `refresh_MCU_products_list()` 未调用 | 在 `add_product_to_shopping_car()` 后手动调用 |

修改代码后重新编译下载，通过串口工具发送协议命令验证功能。
