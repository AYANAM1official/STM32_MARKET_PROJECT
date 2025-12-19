#include "stm32f10x.h"                  // Device header
#include "DHT11.h"
#include "delay.h"

void DHT11_IO_OUT (void){ 
	
	GPIO_InitTypeDef  GPIO_InitStructure; 	
    GPIO_InitStructure.GPIO_Pin = DHT11_IO;                        
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_PP;       
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;    
	GPIO_Init(DHT11_PORT, &GPIO_InitStructure);
}

void DHT11_IO_IN (void){ 
	GPIO_InitTypeDef  GPIO_InitStructure; 	
    GPIO_InitStructure.GPIO_Pin = DHT11_IO;                        
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IN_FLOATING;      
	GPIO_Init(DHT11_PORT, &GPIO_InitStructure);
}

void DHT11_RST (void){ 						
	DHT11_IO_OUT();							
	GPIO_ResetBits(DHT11_PORT,DHT11_IO); 	
	delay_ms(20); 											
	GPIO_SetBits(DHT11_PORT,DHT11_IO); 								
	delay_us(30); 							
}

u8 DHT11_Check(void){ 		   
    u8 retry=0;			
    DHT11_IO_IN();			 

	while ((GPIO_ReadInputDataBit(DHT11_PORT,DHT11_IO) == 1) && retry<100)	
	{
		retry++;
        delay_us(1);
    }
    if(retry>=100)return 1; 	
	else retry=0;

    while ((GPIO_ReadInputDataBit(DHT11_PORT,DHT11_IO) == 0) && retry<100)  
	{  
        retry++;
        delay_us(1);
    }
    if(retry>=100)return 1;	    
    return 0;
}

u8 DHT11_Init (void){	
	RCC_APB2PeriphClockCmd(DHT11_RCC,ENABLE);	
	DHT11_RST();								
	return DHT11_Check(); 					
}

u8 DHT11_Read_Bit(void)
{
    u8 retry = 0;
    while((GPIO_ReadInputDataBit(DHT11_PORT,DHT11_IO) == 1) && retry < 100) 
    {
        retry++;
        delay_us(1);
    }
    retry = 0;
    while((GPIO_ReadInputDataBit(DHT11_PORT,DHT11_IO) == 0) && retry < 100) 
    {
        retry++;
        delay_us(1);
    }
    delay_us(40);
    if(GPIO_ReadInputDataBit(DHT11_PORT,DHT11_IO) == 1)      
        return 1;
    else
        return 0;
}

u8 DHT11_Read_Byte(void)
{
    u8 i, dat;
    dat = 0;
    for (i = 0; i < 8; i++)
    {
        dat <<= 1;					
        dat |= DHT11_Read_Bit();	
    }
    return dat;
}

u8 DHT11_Read_Data(u8 *temp, u8 *humi)
{
    u8 buf[5];
    u8 i;
    DHT11_RST();						
    if(DHT11_Check() == 0)				
    {
        for(i = 0; i < 5; i++) 			
        {
            buf[i] = DHT11_Read_Byte();	
        }
        if((buf[0] + buf[1] + buf[2] + buf[3]) == buf[4])	
        {
            *humi = buf[0];				
            *temp = buf[2];				
        }
    }
    else return 1;
    return 0;
}

u8 DHT11_GetHumidity(void){
    u8 humidity = 0;
    u8 temp = 0;
    if(DHT11_Read_Data(&temp, &humidity) == 0){
        return humidity; // 返回湿度
    }
    return 0; // 读取失败，返回 0
}



