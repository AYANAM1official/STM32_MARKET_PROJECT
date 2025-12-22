# STM32 智能超市系统（STM32F103 + W25Q64）— Copilot 指南

## 快速上手（先看这些文件）
- 入口与业务状态机：`User/main.c`（`while(1)` + `callSyncHandler()` + 购物流程状态）
- 全局状态/购物车：`User/main.h`（`SlaveState_t` + `ShoppingState_t` + `shopping_car[]`）
- 串口协议解析：`User/protocol.c/.h`（环形缓冲区 + 逐行 `\n` 解析）
- Flash 商品库：`User/products.c/.h`（W25Q64 数据库、元数据、查找/写入）
- 串口中断入口：`User/stm32f10x_it.c`（`USART1_IRQHandler()` 只做入队）

## 架构要点（必须遵守）
- **双状态机**：
  - `SlaveState_t` 处理上位机同步（`SYS_STATE_IDLE/SYNC_START/SYNC_ING`）
  - `ShoppingState_t` 处理本地购物（`IDLE/SCANNING/WAITING_PAYOFF/...`）
- **同步期间屏蔽业务**：收到 `EVENT_SYNC_START` 会 `Shop_transtate(SHOP_STATE_SYNCING)`，并在 `TIM2_IRQHandler()` 中直接 `return` 跳过传感器/外设刷新；同步结束恢复 `last_ShoppingState`。

## Flash 数据库约定（改动会影响所有地址）
- 元数据扇区：`FLASH_ADDR_METADATA = 0x000000`（`Product_Metadata_t`）
- 数据起始：`FLASH_ADDR_DB_START = 0x001000`（商品数组）
- 单条商品：`Product_Item_t` **必须是 64 字节**（`products.h` 有编译期校验）；地址计算：`FLASH_ADDR_DB_START + index * ITEM_SIZE`。

## 串口协议（USART1，ASCII 行协议）
- 串口参数：USART1 **固定 `115200 8N1`**（协议通信 + `printf` 调试共用）。
- ISR：`USART1_IRQHandler()` 调 `Protocol_Receive_Byte_IRQ()` 入队；**不要在中断里解析**。
- 解析：`Protocol_Parse_Line()` 在主循环里按 `\n` 分帧，字段通过 `KEY:VALUE`（逗号分隔）。
- 关键命令（示例必须带 `\n`）：
  - `CMD:SYNC_START,TOTAL:100\n` → MCU 擦除后回 `CMD:REQ_SYNC\n`
  - `CMD:SYNC_DATA,ID:6912345,PR:5.99,NM:可乐\n`
  - `CMD:SYNC_END,SUM:100\n`
  - `CMD:SCAN,ID:6912345\n` → 回 `CMD:REPORT,...\n` 或 `CMD:ALARM,...\n`

## 与串口屏交互的坑点
- `User/screen/screen.c` 明确提示：**不要在这里重配 USART1**（协议/调试占用）；串口屏走 `uart2_init(...)`。

## 常见改动路径（加命令/加功能）
- 新协议命令：加 `ProtocolEvent_t`（`User/protocol.h`）→ 在 `Protocol_Parse_Line()` 加 `strstr` 分支 → 在 `callSyncHandler()` 的 `switch(rx_packet.event)` 处理。

## 构建/下载（Keil/J-Link）
- Keil 工程：`Project/RVMDK（uv5）/BH-F103.uvprojx`，Target 名通常为 `SPI FLASH`；J-Link 配置在 `Project/RVMDK（uv5）/JLinkSettings.ini`。
- 说明：本仓库不要求固定的命令行构建脚本/`tasks.json` 工作流（以 Keil 工程为准）。
