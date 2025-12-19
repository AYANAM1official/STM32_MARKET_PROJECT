#ifndef __SG90_H
#define __SG90_H

#include "stm32f10x.h"

// ================== 硬件配置宏定义 ==================
// 如果需要更换引脚，只需修改这里即可

// 选定定时器：TIM4
#define SG90_TIM            TIM4
#define SG90_TIM_RCC        RCC_APB1Periph_TIM4
#define SG90_TIM_RCC_CMD    RCC_APB1PeriphClockCmd  // TIM4挂载在APB1上

// 选定引脚：PB6 (TIM4_CH1)
#define SG90_GPIO_RCC       RCC_APB2Periph_GPIOB
#define SG90_PORT           GPIOB
#define SG90_PIN            GPIO_Pin_6

// ================== 逻辑参数定义 ==================
#define SG90_ARR            19999   // 周期 20ms
#define SG90_PSC            71      // 预分频 72M/(71+1) = 1MHz

// ================== 函数声明 ==================
void SG90_Init(void);           // 初始化
void SG90_SetAngle(float angle); // 设置角度 (0.0 - 180.0)

#endif
