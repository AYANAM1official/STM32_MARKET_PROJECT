#include "delay.h"
#include "stm32f10x_it.h"
static u8  fac_us=0;  // us延时倍乘数
static u16 fac_ms=0;  // ms延时倍乘数

/**
  * @brief  初始化延迟函数
  * @brief  当使用OS的时候,此函数需要更改
  * @brief  SYSTICK的时钟固定为HCLK时钟的1/8
  * @param  None
  * @retval None
  */
void delay_init()
{
    // 选择外部时钟  HCLK/8
    SysTick_CLKSourceConfig(SysTick_CLKSource_HCLK_Div8); 
    
    // SystemCoreClock 默认为 72000000
    // fac_us = 72000000 / 8000000 = 9
    // 代表 1us 需要 SysTick 跑 9 个周期
    fac_us = SystemCoreClock / 8000000; 
    
    // 代表 1ms 需要 SysTick 跑 9000 个周期
    fac_ms = (u16)fac_us * 1000;        
}

/**
  * @brief  微秒(us)延时
  * @param  nus: 延时的微秒数
  * @retval None
  */
void delay_us(u32 nus)
{		
    u32 temp;	    	 
    SysTick->LOAD = nus * fac_us;             // 时间加载
    SysTick->VAL = 0x00;                      // 清空计数器
    SysTick->CTRL |= SysTick_CTRL_ENABLE_Msk; // 开始倒数	
    do
    {
        temp = SysTick->CTRL;
    } while((temp & 0x01) && !(temp & (1<<16))); // 等待时间到达
    
    SysTick->CTRL &= ~SysTick_CTRL_ENABLE_Msk; // 关闭计数器
    SysTick->VAL = 0X00;                       // 清空计数器	 
}

/**
  * @brief  毫秒(ms)延时
  * @brief  注意:由于SysTick是24位计数器, nms的最大值约为1864ms
  * @param  nms: 延时的毫秒数
  * @retval None
  */
void delay_ms(u16 nms)
{	 		  	  
    u32 temp;		   
    SysTick->LOAD = (u32)nms * fac_ms;        // 时间加载(SysTick->LOAD为24bit)
    SysTick->VAL = 0x00;                      // 清空计数器
    SysTick->CTRL |= SysTick_CTRL_ENABLE_Msk; // 开始倒数  
    do
    {
        temp = SysTick->CTRL;
    } while((temp & 0x01) && !(temp & (1<<16))); // 等待时间到达
    
    SysTick->CTRL &= ~SysTick_CTRL_ENABLE_Msk; // 关闭计数器
    SysTick->VAL = 0X00;                       // 清空计数器	  	    
}

