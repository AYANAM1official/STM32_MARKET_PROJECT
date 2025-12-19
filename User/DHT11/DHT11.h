#ifndef __DHT11_H
#define __DHT11_H

#include "stm32f10x.h" 

#define DHT11_RCC    RCC_APB2Periph_GPIOB  
#define DHT11_PORT   GPIOB                 
#define DHT11_IO     GPIO_Pin_12         

#ifndef u8
#define u8  uint8_t
#endif

#ifndef u16
#define u16 uint16_t
#endif

#ifndef u32
#define u32 uint32_t
#endif


void DHT11_IO_OUT(void);

void DHT11_IO_IN(void);

void DHT11_RST(void);

u8 DHT11_Check(void);

u8 DHT11_Init(void);

u8 DHT11_Read_Bit(void);

u8 DHT11_Read_Byte(void);

u8 DHT11_Read_Data(u8 *temp, u8 *humi);

u8 DHT11_GetHumidity(void);

#endif  
