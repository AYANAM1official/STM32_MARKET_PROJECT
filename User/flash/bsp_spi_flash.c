/**
 ******************************************************************************
 * @file    bsp_spi_flash.c
 * @author  STMicroelectronics & Modified for DMA
 * @version V2.0
 * @date    2025-xx-xx
 * @brief   spi flash 底层应用函数bsp (DMA Version)
 ******************************************************************************
 */

#include "./flash/bsp_spi_flash.h"

/* DMA通道定义 */
#define FLASH_DMA_CLK RCC_AHBPeriph_DMA1
#define FLASH_SPI_DMA_TX DMA1_Channel3
#define FLASH_SPI_DMA_RX DMA1_Channel2
#define FLASH_SPI_DR_Base ((uint32_t)0x4001300C) // SPI1->DR 地址

static __IO uint32_t SPITimeout = SPIT_LONG_TIMEOUT;
static uint16_t SPI_TIMEOUT_UserCallback(uint8_t errorCode);

/* 这里的 SendByte 依然保留用于发送命令和地址，因为短字节轮询更快 */
u8 SPI_FLASH_SendByte(u8 byte);

/**
 * @brief  SPI_FLASH初始化 (增加DMA初始化)
 */
void SPI_FLASH_Init(void)
{
  SPI_InitTypeDef SPI_InitStructure;
  GPIO_InitTypeDef GPIO_InitStructure;

  /* 使能SPI时钟 */
  FLASH_SPI_APBxClock_FUN(FLASH_SPI_CLK, ENABLE);

  /* 使能DMA时钟 */
  RCC_AHBPeriphClockCmd(FLASH_DMA_CLK, ENABLE);

  /* 使能SPI引脚相关的时钟 */
  FLASH_SPI_CS_APBxClock_FUN(FLASH_SPI_CS_CLK | FLASH_SPI_SCK_CLK |
                                 FLASH_SPI_MISO_PIN | FLASH_SPI_MOSI_PIN,
                             ENABLE);

  /* 配置SPI的 CS引脚 */
  GPIO_InitStructure.GPIO_Pin = FLASH_SPI_CS_PIN;
  GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
  GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_PP;
  GPIO_Init(FLASH_SPI_CS_PORT, &GPIO_InitStructure);

  /* 配置SPI的 SCK引脚*/
  GPIO_InitStructure.GPIO_Pin = FLASH_SPI_SCK_PIN;
  GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_PP;
  GPIO_Init(FLASH_SPI_SCK_PORT, &GPIO_InitStructure);

  /* 配置SPI的 MISO引脚*/
  GPIO_InitStructure.GPIO_Pin = FLASH_SPI_MISO_PIN;
  GPIO_Init(FLASH_SPI_MISO_PORT, &GPIO_InitStructure);

  /* 配置SPI的 MOSI引脚*/
  GPIO_InitStructure.GPIO_Pin = FLASH_SPI_MOSI_PIN;
  GPIO_Init(FLASH_SPI_MOSI_PORT, &GPIO_InitStructure);

  /* 停止信号 FLASH: CS引脚高电平*/
  SPI_FLASH_CS_HIGH();

  /* SPI 模式配置 */
  SPI_InitStructure.SPI_Direction = SPI_Direction_2Lines_FullDuplex;
  SPI_InitStructure.SPI_Mode = SPI_Mode_Master;
  SPI_InitStructure.SPI_DataSize = SPI_DataSize_8b;
  SPI_InitStructure.SPI_CPOL = SPI_CPOL_High;
  SPI_InitStructure.SPI_CPHA = SPI_CPHA_2Edge;
  SPI_InitStructure.SPI_NSS = SPI_NSS_Soft;
  SPI_InitStructure.SPI_BaudRatePrescaler = SPI_BaudRatePrescaler_4; // 根据实际需求调整速度
  SPI_InitStructure.SPI_FirstBit = SPI_FirstBit_MSB;
  SPI_InitStructure.SPI_CRCPolynomial = 7;
  SPI_Init(FLASH_SPIx, &SPI_InitStructure);

  /* 使能 SPI  */
  SPI_Cmd(FLASH_SPIx, ENABLE);

  /* 使能SPI DMA请求 (发送和接收) */
  SPI_I2S_DMACmd(FLASH_SPIx, SPI_I2S_DMAReq_Tx, ENABLE);
  SPI_I2S_DMACmd(FLASH_SPIx, SPI_I2S_DMAReq_Rx, ENABLE);
}

/**
 * @brief  DMA 配置辅助函数
 * @param  DMA_Channel: DMA通道
 * @param  SrcAddr: 传输源地址
 * @param  DstAddr: 传输目的地址
 * @param  Size: 传输数据长度
 * @param  Dir: 数据传输方向
 * @param  MemInc: 内存是否自增
 */
static void SPI_DMA_Config(DMA_Channel_TypeDef *DMA_Channel, uint32_t SrcAddr, uint32_t DstAddr, uint16_t Size, uint32_t Dir, uint32_t MemInc)
{
  DMA_InitTypeDef DMA_InitStructure;

  DMA_DeInit(DMA_Channel);

  DMA_InitStructure.DMA_PeripheralBaseAddr = (uint32_t)FLASH_SPI_DR_Base; // SPI数据寄存器地址
  DMA_InitStructure.DMA_MemoryBaseAddr = (uint32_t)0;                     // 后面根据参数设置

  /* 这里根据传入参数判断源和目的，因为SPI作为外设，地址是固定的 */
  if (Dir == DMA_DIR_PeripheralDST) // 内存 -> 外设 (发送)
  {
    DMA_InitStructure.DMA_MemoryBaseAddr = SrcAddr;
  }
  else // 外设 -> 内存 (接收)
  {
    DMA_InitStructure.DMA_MemoryBaseAddr = DstAddr;
  }

  DMA_InitStructure.DMA_DIR = Dir;
  DMA_InitStructure.DMA_BufferSize = Size;
  DMA_InitStructure.DMA_PeripheralInc = DMA_PeripheralInc_Disable; // 外设地址不增
  DMA_InitStructure.DMA_MemoryInc = MemInc;                        // 内存地址是否自增
  DMA_InitStructure.DMA_PeripheralDataSize = DMA_PeripheralDataSize_Byte;
  DMA_InitStructure.DMA_MemoryDataSize = DMA_MemoryDataSize_Byte;
  DMA_InitStructure.DMA_Mode = DMA_Mode_Normal;
  DMA_InitStructure.DMA_Priority = DMA_Priority_Medium;
  DMA_InitStructure.DMA_M2M = DMA_M2M_Disable;

  DMA_Init(DMA_Channel, &DMA_InitStructure);
  DMA_Cmd(DMA_Channel, ENABLE);
}

/**
 * @brief  擦除FLASH扇区 (保留轮询，命令短)
 */
void SPI_FLASH_SectorErase(u32 SectorAddr)
{
  SPI_FLASH_WriteEnable();
  SPI_FLASH_WaitForWriteEnd();

  SPI_FLASH_CS_LOW();
  SPI_FLASH_SendByte(W25X_SectorErase);
  SPI_FLASH_SendByte((SectorAddr & 0xFF0000) >> 16);
  SPI_FLASH_SendByte((SectorAddr & 0xFF00) >> 8);
  SPI_FLASH_SendByte(SectorAddr & 0xFF);
  SPI_FLASH_CS_HIGH();

  SPI_FLASH_WaitForWriteEnd();
}

/**
 * @brief  整片擦除 (保留轮询)
 */
void SPI_FLASH_BulkErase(void)
{
  SPI_FLASH_WriteEnable();
  SPI_FLASH_CS_LOW();
  SPI_FLASH_SendByte(W25X_ChipErase);
  SPI_FLASH_CS_HIGH();
  SPI_FLASH_WaitForWriteEnd();
}

/**
 * @brief  对FLASH按页写入数据 (改用DMA)
 * @param  pBuffer，要写入数据的指针
 * @param WriteAddr，写入地址
 * @param  NumByteToWrite，写入数据长度
 */
void SPI_FLASH_PageWrite(u8 *pBuffer, u32 WriteAddr, u16 NumByteToWrite)
{
  if (NumByteToWrite == 0)
    return;

  /* 发送FLASH写使能命令 */
  SPI_FLASH_WriteEnable();

  /* 选择FLASH: CS低电平 */
  SPI_FLASH_CS_LOW();

  /* 1. 先通过轮询发送写命令和地址 (速度够快，不需要DMA) */
  SPI_FLASH_SendByte(W25X_PageProgram);
  SPI_FLASH_SendByte((WriteAddr & 0xFF0000) >> 16);
  SPI_FLASH_SendByte((WriteAddr & 0xFF00) >> 8);
  SPI_FLASH_SendByte(WriteAddr & 0xFF);

  if (NumByteToWrite > SPI_FLASH_PerWritePageSize)
  {
    NumByteToWrite = SPI_FLASH_PerWritePageSize;
    FLASH_ERROR("SPI_FLASH_PageWrite too large!");
  }

  /* 2. 使用DMA发送数据 (DMA1_Channel3 for SPI1_TX) */
  /* 配置DMA: 内存->外设, 内存地址自增 */
  SPI_DMA_Config(FLASH_SPI_DMA_TX, (uint32_t)pBuffer, 0, NumByteToWrite, DMA_DIR_PeripheralDST, DMA_MemoryInc_Enable);

  /* 3. 等待DMA传输完成 */
  while (DMA_GetFlagStatus(DMA1_FLAG_TC3) == RESET)
    ;                                 // 等待发送完成
  DMA_ClearFlag(DMA1_FLAG_TC3);       // 清除标志位
  DMA_Cmd(FLASH_SPI_DMA_TX, DISABLE); // 关闭DMA

  /* 4. 等待SPI总线空闲 (重要: DMA传完进SPI FIFO，SPI发完才是真的完) */
  /* 也就是等待 BSY 位为0 */
  while (SPI_I2S_GetFlagStatus(FLASH_SPIx, SPI_I2S_FLAG_BSY) == SET)
    ;

  /* 停止信号 FLASH: CS 高电平 */
  SPI_FLASH_CS_HIGH();

  /* 等待Flash内部写入完毕 */
  SPI_FLASH_WaitForWriteEnd();
}

/**
 * @brief  对FLASH写入数据 (逻辑不变，会自动调用上面的DMA版PageWrite)
 */
void SPI_FLASH_BufferWrite(u8 *pBuffer, u32 WriteAddr, u16 NumByteToWrite)
{
  u8 NumOfPage = 0, NumOfSingle = 0, Addr = 0, count = 0, temp = 0;

  Addr = WriteAddr % SPI_FLASH_PageSize;
  count = SPI_FLASH_PageSize - Addr;
  NumOfPage = NumByteToWrite / SPI_FLASH_PageSize;
  NumOfSingle = NumByteToWrite % SPI_FLASH_PageSize;

  if (Addr == 0)
  {
    if (NumOfPage == 0)
    {
      SPI_FLASH_PageWrite(pBuffer, WriteAddr, NumByteToWrite);
    }
    else
    {
      while (NumOfPage--)
      {
        SPI_FLASH_PageWrite(pBuffer, WriteAddr, SPI_FLASH_PageSize);
        WriteAddr += SPI_FLASH_PageSize;
        pBuffer += SPI_FLASH_PageSize;
      }
      SPI_FLASH_PageWrite(pBuffer, WriteAddr, NumOfSingle);
    }
  }
  else
  {
    if (NumOfPage == 0)
    {
      if (NumOfSingle > count)
      {
        temp = NumOfSingle - count;
        SPI_FLASH_PageWrite(pBuffer, WriteAddr, count);
        WriteAddr += count;
        pBuffer += count;
        SPI_FLASH_PageWrite(pBuffer, WriteAddr, temp);
      }
      else
      {
        SPI_FLASH_PageWrite(pBuffer, WriteAddr, NumByteToWrite);
      }
    }
    else
    {
      NumByteToWrite -= count;
      NumOfPage = NumByteToWrite / SPI_FLASH_PageSize;
      NumOfSingle = NumByteToWrite % SPI_FLASH_PageSize;

      SPI_FLASH_PageWrite(pBuffer, WriteAddr, count);

      WriteAddr += count;
      pBuffer += count;
      while (NumOfPage--)
      {
        SPI_FLASH_PageWrite(pBuffer, WriteAddr, SPI_FLASH_PageSize);
        WriteAddr += SPI_FLASH_PageSize;
        pBuffer += SPI_FLASH_PageSize;
      }
      if (NumOfSingle != 0)
      {
        SPI_FLASH_PageWrite(pBuffer, WriteAddr, NumOfSingle);
      }
    }
  }
}

/**
 * @brief  读取FLASH数据 (改用DMA)
 * @param  pBuffer，存储读出数据的指针
 * @param   ReadAddr，读取地址
 * @param   NumByteToRead，读取数据长度
 * @retval 无
 */
void SPI_FLASH_BufferRead(u8 *pBuffer, u32 ReadAddr, u16 NumByteToRead)
{
  if (NumByteToRead == 0)
    return;

  /* 定义一个空字节用于发送产生时钟 */
  static uint8_t dummyByte = Dummy_Byte;

  /* 选择FLASH: CS低电平 */
  SPI_FLASH_CS_LOW();

  /* 1. 轮询发送读指令和地址 */
  SPI_FLASH_SendByte(W25X_ReadData);
  SPI_FLASH_SendByte((ReadAddr & 0xFF0000) >> 16);
  SPI_FLASH_SendByte((ReadAddr & 0xFF00) >> 8);
  SPI_FLASH_SendByte(ReadAddr & 0xFF);

  /* 2. 配置DMA进行数据接收 */
  /* SPI Master接收数据的原理是：同时发送数据产生SCK时钟。
     所以我们需要：
     DMA1_Channel2 (RX): 负责把SPI_DR的数据搬运到 pBuffer (地址自增)
     DMA1_Channel3 (TX): 负责把 dummyByte 搬运到 SPI_DR (地址不自增!)
  */

  /* 配置 RX DMA (SPI -> Memory) */
  SPI_DMA_Config(FLASH_SPI_DMA_RX, 0, (uint32_t)pBuffer, NumByteToRead, DMA_DIR_PeripheralSRC, DMA_MemoryInc_Enable);

  /* 配置 TX DMA (Memory -> SPI) - 注意这里 MemoryInc 是 Disable，因为只发同一个 dummyByte */
  SPI_DMA_Config(FLASH_SPI_DMA_TX, (uint32_t)&dummyByte, 0, NumByteToRead, DMA_DIR_PeripheralDST, DMA_MemoryInc_Disable);

  /* 3. 等待接收DMA完成 (以接收完成为准) */
  while (DMA_GetFlagStatus(DMA1_FLAG_TC2) == RESET)
    ;

  /* 清除标志位 & 关闭DMA */
  DMA_ClearFlag(DMA1_FLAG_TC2);
  DMA_ClearFlag(DMA1_FLAG_TC3); // 发送的标志位也要清
  DMA_Cmd(FLASH_SPI_DMA_RX, DISABLE);
  DMA_Cmd(FLASH_SPI_DMA_TX, DISABLE);

  /* 停止信号 FLASH: CS 高电平 */
  SPI_FLASH_CS_HIGH();
}

/**
 * @brief  读取FLASH ID (保留轮询)
 */
u32 SPI_FLASH_ReadID(void)
{
  u32 Temp = 0, Temp0 = 0, Temp1 = 0, Temp2 = 0;
  SPI_FLASH_CS_LOW();
  SPI_FLASH_SendByte(W25X_JedecDeviceID);
  Temp0 = SPI_FLASH_SendByte(Dummy_Byte);
  Temp1 = SPI_FLASH_SendByte(Dummy_Byte);
  Temp2 = SPI_FLASH_SendByte(Dummy_Byte);
  SPI_FLASH_CS_HIGH();
  Temp = (Temp0 << 16) | (Temp1 << 8) | Temp2;
  return Temp;
}

/**
 * @brief  读取FLASH Device ID (保留轮询)
 */
u32 SPI_FLASH_ReadDeviceID(void)
{
  u32 Temp = 0;
  SPI_FLASH_CS_LOW();
  SPI_FLASH_SendByte(W25X_DeviceID);
  SPI_FLASH_SendByte(Dummy_Byte);
  SPI_FLASH_SendByte(Dummy_Byte);
  SPI_FLASH_SendByte(Dummy_Byte);
  Temp = SPI_FLASH_SendByte(Dummy_Byte);
  SPI_FLASH_CS_HIGH();
  return Temp;
}

/**
 * @brief  StartReadSequence (保留)
 */
void SPI_FLASH_StartReadSequence(u32 ReadAddr)
{
  SPI_FLASH_CS_LOW();
  SPI_FLASH_SendByte(W25X_ReadData);
  SPI_FLASH_SendByte((ReadAddr & 0xFF0000) >> 16);
  SPI_FLASH_SendByte((ReadAddr & 0xFF00) >> 8);
  SPI_FLASH_SendByte(ReadAddr & 0xFF);
}

/**
 * @brief  使用SPI读取一个字节的数据 (保留)
 */
u8 SPI_FLASH_ReadByte(void)
{
  return (SPI_FLASH_SendByte(Dummy_Byte));
}

/**
 * @brief  使用SPI发送一个字节的数据 (保留)，此函数用于发送命令和参数，不建议改为DMA
 */
u8 SPI_FLASH_SendByte(u8 byte)
{
  SPITimeout = SPIT_FLAG_TIMEOUT;
  /* 等待发送缓冲区为空，TXE事件 */
  while (SPI_I2S_GetFlagStatus(FLASH_SPIx, SPI_I2S_FLAG_TXE) == RESET)
  {
    if ((SPITimeout--) == 0)
      return SPI_TIMEOUT_UserCallback(0);
  }

  /* 写入数据寄存器，把要写入的数据写入发送缓冲区 */
  SPI_I2S_SendData(FLASH_SPIx, byte);

  SPITimeout = SPIT_FLAG_TIMEOUT;
  /* 等待接收缓冲区非空，RXNE事件 */
  while (SPI_I2S_GetFlagStatus(FLASH_SPIx, SPI_I2S_FLAG_RXNE) == RESET)
  {
    if ((SPITimeout--) == 0)
      return SPI_TIMEOUT_UserCallback(1);
  }

  /* 读取数据寄存器，获取接收缓冲区数据 */
  return SPI_I2S_ReceiveData(FLASH_SPIx);
}

/**
 * @brief  使用SPI发送两个字节的数据 (保留)
 */
u16 SPI_FLASH_SendHalfWord(u16 HalfWord)
{
  SPITimeout = SPIT_FLAG_TIMEOUT;
  while (SPI_I2S_GetFlagStatus(FLASH_SPIx, SPI_I2S_FLAG_TXE) == RESET)
  {
    if ((SPITimeout--) == 0)
      return SPI_TIMEOUT_UserCallback(2);
  }
  SPI_I2S_SendData(FLASH_SPIx, HalfWord);
  SPITimeout = SPIT_FLAG_TIMEOUT;
  while (SPI_I2S_GetFlagStatus(FLASH_SPIx, SPI_I2S_FLAG_RXNE) == RESET)
  {
    if ((SPITimeout--) == 0)
      return SPI_TIMEOUT_UserCallback(3);
  }
  return SPI_I2S_ReceiveData(FLASH_SPIx);
}

/**
 * @brief  向FLASH发送 写使能 命令
 */
void SPI_FLASH_WriteEnable(void)
{
  SPI_FLASH_CS_LOW();
  SPI_FLASH_SendByte(W25X_WriteEnable);
  SPI_FLASH_CS_HIGH();
}

/**
 * @brief  等待WIP(BUSY)标志被置0
 */
void SPI_FLASH_WaitForWriteEnd(void)
{
  u8 FLASH_Status = 0;
  SPI_FLASH_CS_LOW();
  SPI_FLASH_SendByte(W25X_ReadStatusReg);
  do
  {
    FLASH_Status = SPI_FLASH_SendByte(Dummy_Byte);
  } while ((FLASH_Status & WIP_Flag) == SET);
  SPI_FLASH_CS_HIGH();
}

// 进入掉电模式
void SPI_Flash_PowerDown(void)
{
  SPI_FLASH_CS_LOW();
  SPI_FLASH_SendByte(W25X_PowerDown);
  SPI_FLASH_CS_HIGH();
}

// 唤醒
void SPI_Flash_WAKEUP(void)
{
  SPI_FLASH_CS_LOW();
  SPI_FLASH_SendByte(W25X_ReleasePowerDown);
  SPI_FLASH_CS_HIGH();
}

/**
 * @brief  等待超时回调函数
 */
static uint16_t SPI_TIMEOUT_UserCallback(uint8_t errorCode)
{
  FLASH_ERROR("SPI 等待超时!errorCode = %d", errorCode);
  return 0;
}
