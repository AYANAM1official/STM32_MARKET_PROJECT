#include "sg90.h"

/**
  * @brief  SG90舵机硬件初始化
  * @note   使用 TIM4_CH1 (PB6)
  */
void SG90_Init(void)
{
    TIM_TimeBaseInitTypeDef  TIM_TimeBaseStructure;
    TIM_OCInitTypeDef  TIM_OCInitStructure;
    GPIO_InitTypeDef GPIO_InitStructure;

    // 1. 开启外设时钟
    // 注意：ZET6的通用定时器通常在 APB1，GPIO 在 APB2
    SG90_TIM_RCC_CMD(SG90_TIM_RCC, ENABLE); 
    RCC_APB2PeriphClockCmd(SG90_GPIO_RCC, ENABLE);

    // 2. 配置 GPIO (复用推挽输出)
    GPIO_InitStructure.GPIO_Pin = SG90_PIN;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_PP; // 复用推挽很重要，否则PWM出不来
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(SG90_PORT, &GPIO_InitStructure);

    // 3. 配置定时器时基
    // 目标：频率50Hz (周期20ms)
    TIM_TimeBaseStructure.TIM_Period = SG90_ARR;       // 20000 - 1
    TIM_TimeBaseStructure.TIM_Prescaler = SG90_PSC;    // 72 - 1
    TIM_TimeBaseStructure.TIM_ClockDivision = TIM_CKD_DIV1;
    TIM_TimeBaseStructure.TIM_CounterMode = TIM_CounterMode_Up;
    TIM_TimeBaseInit(SG90_TIM, &TIM_TimeBaseStructure);

    // 4. 配置 PWM 输出通道 (Channel 1)
    // 注意：如果是 PB6，对应的是 TIM4 的 Channel 1，所以使用 TIM_OC1Init
    TIM_OCInitStructure.TIM_OCMode = TIM_OCMode_PWM1;
    TIM_OCInitStructure.TIM_OutputState = TIM_OutputState_Enable;
    TIM_OCInitStructure.TIM_Pulse = 0;              // 初始占空比0
    TIM_OCInitStructure.TIM_OCPolarity = TIM_OCPolarity_High;
    TIM_OC1Init(SG90_TIM, &TIM_OCInitStructure);    // <--- 关键点：OC1Init

    // 5. 使能预装载
    TIM_OC1PreloadConfig(SG90_TIM, TIM_OCPreload_Enable);
    TIM_ARRPreloadConfig(SG90_TIM, ENABLE);

    // 6. 开启定时器
    TIM_Cmd(SG90_TIM, ENABLE);
}

/**
  * @brief  设置舵机角度
  * @param  angle: 角度值 (0.0 ~ 180.0)
  * @retval None
  */
void SG90_SetAngle(float angle)
{
    uint16_t ccr_val = 0;

    // 1. 输入限幅 (保护舵机)
    if (angle < 0.0f) angle = 0.0f;
    if (angle > 180.0f) angle = 180.0f;

    // 2. 角度转占空比计算
    // 0度   = 0.5ms = 500us  (计数值 500)
    // 180度 = 2.5ms = 2500us (计数值 2500)
    // 线性关系: CCR = 500 + (angle / 180) * 2000
    ccr_val = (uint16_t)(500 + (angle / 180.0f) * 2000);

    // 3. 设置比较寄存器 (通道1)
    TIM_SetCompare1(SG90_TIM, ccr_val); 
}
