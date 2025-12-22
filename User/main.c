#include "main.h"

// TIM2 1Hz 节拍计数（用于非阻塞超时）
static volatile uint32_t g_tim2_tick_s = 0;
static uint32_t g_waiting_payoff_start_s = 0;

/**
 * @brief  主函数中包含和串口有关的，切换状态机的高优先级任务，数据的更新在TIM中断中完成
 */
int main(void)
{
    // 1. 系统底层初始化
    // ---------------------------------------------------------
    USART_Config();          // 初始化串口 (bsp_usart_dma.c)
    Setup_USART_Interrupt(); // 额外开启 RXNE 中断用于 Protocol 接收
    Screen_Shopping_System_Init();
    delay_init();
    Key_GPIO_Config();
    // BEEP_GPIO_Config(); // 初始化蜂鸣器 GPIO
    LED_GPIO_Config();

    // 2. 中间件与协议初始化
    // ---------------------------------------------------------
    Protocol_Init();        // 初始化协议环形缓冲区
    Product_Manager_Init(); // 初始化商品管理器 (读取元数据，恢复总数)
    Product_Debug_Dump_All();
    Setup_TIM2_Interrupt(); // 初始化 TIM2 定时器 (0.5s 周期中断)

    BEEP(OFF);
    LED_RED(OFF);

    /**while (DHT11_Init())
    {
        printf("[System] DHT11_Init ing...\r\n");
    } // 初始化 DHT11 湿度传感器
    delay_ms(50); // 等待传感器稳定
    **/
    DS18B20_Init();
    printf("[System] DS18B20_Init Complete.\r\n");
    delay_ms(50);
    // 3. 主循环 (无限状态机)
    // ---------------------------------------------------------
    while (1)
    {
        /**if (sensor_data.temper > MAX_TEMPER || (sensor_data.humidity > MAX_HUMIDITY && sensor_data.humidity < 100))
        {
            // 进入紧急状态
            Shop_transtate(SHOP_STATE_EMMERGENCY);
            Slave_transtate(SYS_STATE_IDLE); // 从机状态回到空闲
            printf("[Emergency] Temperature or Humidity Exceeded Limits! Temp: %.2f, Humidity: %d%%\r\n", sensor_data.temper, sensor_data.humidity);
        }
        else if (ShoppingState == SHOP_STATE_EMMERGENCY)
        {
            // 恢复正常状态
            BEEP(OFF);
            LED_RED(OFF);
            Shop_transtate(SHOP_STATE_IDLE);
            Slave_transtate(SYS_STATE_IDLE); // 从机状态回到空闲
            control_Servo_Door(0);           // 关闭舵机门
            printf("[Recovery] Temperature and Humidity Back to Normal. Temp: %.2f, Humidity: %d%%\r\n", sensor_data.temper, sensor_data.humidity);
        }
          **/
        // 进入上位机服务函数，检查环形队列是否有更新
        callSyncHandler();

        switch (ShoppingState)
        {
        case SHOP_STATE_IDLE:
            // 空闲状态，等待扫码
            // clear_shopping_car();
            // printf("clear_shopping_car...\r\n");
            if (Screen_Check_Start_Shopping_Msg())
            {
                printf("[Shop] Shopping Started.\r\n");
                control_Servo_Door(1);
                delay_ms(800); // 等待舵机动作完成
                control_Servo_Door(0);

                Shop_transtate(SHOP_STATE_SCANNING);
            }

            break;
        case SHOP_STATE_SCANNING:
        {
            // 非阻塞：保持主循环继续运行（允许 `callSyncHandler()` 处理扫码/同步协议）
            if (Screen_Wait_For_PayOff_Msg())
            {
                g_waiting_payoff_start_s = g_tim2_tick_s;
                Shop_transtate(SHOP_STATE_WAITING_PAYOFF);
            }
            break;
        }
        case SHOP_STATE_WAITING_PAYOFF:
        {
            // 非阻塞：等待按键确认，超过 30s 自动返回扫码
            if (Key_Scan(KEY2_GPIO_PORT, KEY2_GPIO_PIN) == KEY_OFF)
            {
                printf("[Shop] Payment Confirmed. Switching to IDLE state.\r\n");
                printf("CMD:PAY_OFF,TOTAL:%d\n", total_products2paid);
                control_Servo_Door(1);
                delay_ms(800); // 等待舵机动作完成
                control_Servo_Door(0);
                Shop_transtate(SHOP_STATE_IDLE);
                clear_shopping_car();
                TJCPrintf("t4.txt=\"shopping finished\r\n\"");
            }
            else
            {
                if ((uint32_t)(g_tim2_tick_s - g_waiting_payoff_start_s) >= 30U)
                {
                    Shop_transtate(SHOP_STATE_SCANNING);
                    TJCPrintf("t4.txt=\"shopping out-time\r\n\"");
                }
            }
            break;
        }
        case SHOP_STATE_EMMERGENCY:
            // 紧急状态，闪灯，响蜂鸣器
            // callEmergencyHandler();
            break;
        case SHOP_STATE_SYNCING:
            // 数据同步中，暂停模块通信
            break;
        default:
            break;
        }
    }
}

// 调用数据同步所用的从机状态机
void callSyncHandler(void)
{
    // 尝试从协议缓冲区解析一条完整指令 (非阻塞)
    if (Protocol_Parse_Line(&rx_packet))
    {

        // 状态机根据当前状态和接收到的事件进行处理
        switch (rx_packet.event)
        {
        // ---------------------------------------------------------
        // 场景 A: 收到同步启动指令 (PC -> STM32)
        // 指令: CMD:SYNC_START,TOTAL:100
        // ---------------------------------------------------------
        case EVENT_SYNC_START:
            // 只有在空闲状态下才允许开始同步
            if (SlaveState == SYS_STATE_IDLE)
            {
                // =进入同步流程，屏蔽模块通信=
                Shop_transtate(SHOP_STATE_SYNCING);

                sync_expect_total = rx_packet.total_count;
                printf("<< SYNC_START >> Expecting %d items.\r\n", sync_expect_total);

                // [状态切换] 进入同步启动状态
                Slave_transtate(SYS_STATE_SYNC_START);

                // [核心操作] 格式化数据库 (耗时操作：擦除 Flash 扇区)
                // 注意：PC 端发送 START 后会进入等待，所以这里阻塞是安全的
                Product_Clear_Database();

                // [握手信号] 发送 REQ_SYNC 告诉 PC: "擦除完毕，请发送数据"
                // 对应文档中的 "阶段二：握手成功"
                printf("CMD:REQ_SYNC\n");

                // [状态切换] 进入接收数据状态
                Slave_transtate(SYS_STATE_SYNC_ING);
                sync_received_cnt = 0;
            }
            else
            {
                printf("CMD:ALARM,MSG:Busy_Syncing\n");
            }
            break;

        // ---------------------------------------------------------
        // 场景 B: 收到商品数据 (PC -> STM32)
        // 指令: CMD:SYNC_DATA,ID:...,PR:...,NM:...
        // ---------------------------------------------------------
        case EVENT_SYNC_DATA:
            if (SlaveState == SYS_STATE_SYNC_ING)
            {
                if (!rx_packet.id_valid)
                {
                    printf("CMD:ALARM,LEVEL:2,MSG:Invalid_ID\n");
                    break;
                }
                // [核心操作] 写入 Flash
                // 使用 sync_received_cnt 作为存储索引 (Index)
                Product_Write_Item(sync_received_cnt,
                                   rx_packet.id,
                                   rx_packet.price,
                                   rx_packet.name);

                sync_received_cnt++;

                // 可选：每接收 50 条打印一次进度日志 (避免串口刷屏)
                if (sync_received_cnt % 50 == 0)
                {
                    printf("[Log] Sync Progress: %d/%d\r\n", sync_received_cnt, sync_expect_total);
                }
            }
            break;

        // ---------------------------------------------------------
        // 场景 C: 收到同步结束指令 (PC -> STM32)
        // 指令: CMD:SYNC_END,SUM:100
        // ---------------------------------------------------------
        case EVENT_SYNC_END:
            if (SlaveState == SYS_STATE_SYNC_ING)
            {
                printf("<< SYNC_END >> Recv: %d, PC_Sum: %d\r\n", sync_received_cnt, rx_packet.total_count);

                // [校验] 检查接收数量是否与 PC 发送数量一致
                if (sync_received_cnt == rx_packet.total_count)
                {
                    // 校验通过：更新 Flash 中的元数据 (Total Count)
                    Product_Update_Metadata(sync_received_cnt);
                    printf("[Success] Database Updated Successfully.\r\n");

                    // 蜂鸣器提示可以加在这里...
                }
                else
                {
                    // 校验失败
                    printf("[Error] Data Count Mismatch!\r\n");
                    printf("CMD:ALARM,LEVEL:2,MSG:Sync_Mismatch_Error\n");
                }

                // [状态切换] 恢复空闲，允许扫码
                Slave_transtate(SYS_STATE_IDLE);
                // 回到原状态
                Shop_transtate(last_ShoppingState);
            }
            break;

        // ---------------------------------------------------------
        // 场景 D: 模拟扫码 / 实际扫码 (PC/Scanner -> STM32)
        // 指令: CMD:SCAN,ID:6912345
        // ---------------------------------------------------------
        case EVENT_SCAN:
            if (SlaveState == SYS_STATE_IDLE)
            {
                if (!rx_packet.id_valid)
                {
                    printf("CMD:ALARM,LEVEL:2,MSG:Invalid_ID\n");
                    break;
                }
                Product_Item_t result_item;
                // printf("[Scan] Searching ID: %d ...\r\n", rx_packet.id);

                // [核心操作] 在 Flash 中查找 ID
                if (Product_Find_By_ID(rx_packet.id, &result_item))
                {
                    // 找到商品 -> 上报销售信息
                    // 格式: CMD:REPORT,ID:xxx,PR:xxx,NM:xxx
                    printf("CMD:REPORT,ID:%llu,PR:%.2f,NM:%s\n",
                           (unsigned long long)result_item.id,
                           result_item.price,
                           result_item.name);
                    // 同时添加到购物车
                    add_product_to_shopping_car(&result_item);
                    // 调试：打印购物车情况
                    debug_print_shopping_car();

                    refresh_MCU_products_list();
                    Screen_Update_HMI_Shopping_List();
                    Screen_Calculate_And_Send_Total();
                }
                else
                {
                    // 未找到 -> 报警
                    printf("CMD:ALARM,LEVEL:1,MSG:Item_Not_Found\n");
                }
            }
            else
            {
                // 如果正在同步时扫码，提示系统忙
                printf("CMD:ALARM,MSG:System_Busy\n");
            }
            break;
        case EVENT_NONE:

            break;
        }
    }

    // 主循环空闲任务 (例如 LED 闪烁心跳)
    // Delay(100);
    // Toggle_LED();
}

void callEmergencyHandler(void)
{
    // 亮红灯，响蜂鸣器，开门，通知上位机
    LED_RED(ON);
    BEEP(ON);
    control_Servo_Door(1);
}

/**
 * @brief  配置 TIM2 定时器中断，0.5s 触发一次
 * @note   系统时钟 72MHz，APB1 时钟 36MHz，TIM2 在 APB1 上
 *         APB1 预分频系数不为 1 时，TIM 时钟 = APB1 × 2 = 72MHz
 *         目标：0.5s = 500ms
 *         配置：PSC = 7199, ARR = 9999
 *         计算：72MHz / (7199+1) / (9999+1) = 72MHz / 7200 / 10000 = 1Hz = 1s
 */
void Setup_TIM2_Interrupt(void)
{
    TIM_TimeBaseInitTypeDef TIM_InitStruct;
    NVIC_InitTypeDef NVIC_InitStruct;

    // 1. 使能 TIM2 时钟 (APB1)
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM2, ENABLE);

    // 2. 配置定时器基本参数
    TIM_InitStruct.TIM_Period = 9999;    // 自动重装载值 ARR = 9999 (计数 10000 次)
    TIM_InitStruct.TIM_Prescaler = 7199; // 预分频器 PSC = 7199 (72MHz / 7200 = 10KHz)
    TIM_InitStruct.TIM_ClockDivision = TIM_CKD_DIV1;
    TIM_InitStruct.TIM_CounterMode = TIM_CounterMode_Up; // 向上计数
    TIM_TimeBaseInit(TIM2, &TIM_InitStruct);

    // 3. 使能更新中断
    TIM_ITConfig(TIM2, TIM_IT_Update, ENABLE);

    // 4. 配置 NVIC 中断优先级
    NVIC_InitStruct.NVIC_IRQChannel = TIM2_IRQn;
    NVIC_InitStruct.NVIC_IRQChannelPreemptionPriority = 2; // 抢占优先级 2 (低于串口)
    NVIC_InitStruct.NVIC_IRQChannelSubPriority = 0;
    NVIC_InitStruct.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init(&NVIC_InitStruct);

    // 5. 启动定时器
    TIM_Cmd(TIM2, ENABLE);

    // printf("[TIM2] Initialized. Period: 0.5s (2Hz)\r\n");
}

/**
 * @brief  配置串口中断 (补充配置)
 * @note   bsp_usart_dma.c 中默认可能只开了 IDLE，我们需要开启 RXNE 以支持环形缓冲区
 */

void Setup_USART_Interrupt(void)
{
    NVIC_InitTypeDef NVIC_InitStruct;

    // 1. 开启串口接收寄存器非空中断 (RXNE) -> 对应 stm32f10x_it.c 中的处理
    USART_ITConfig(DEBUG_USARTx, USART_IT_RXNE, ENABLE);

    // 2. 配置 NVIC 优先级
    NVIC_PriorityGroupConfig(NVIC_PriorityGroup_2);
    NVIC_InitStruct.NVIC_IRQChannel = DEBUG_USART_IRQ;
    NVIC_InitStruct.NVIC_IRQChannelPreemptionPriority = 1;
    NVIC_InitStruct.NVIC_IRQChannelSubPriority = 1;
    NVIC_InitStruct.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init(&NVIC_InitStruct);
}

/**
 * @brief  TIM2 定时器中断服务函数 (0.5s 周期)
 * @note   用于周期性任务，如数据更新、状态检查等
 */
void TIM2_IRQHandler(void)
{
    if (TIM_GetITStatus(TIM2, TIM_IT_Update) != RESET)
    {
        // 清除中断标志位
        TIM_ClearITPendingBit(TIM2, TIM_IT_Update);

        // 1Hz 节拍（用于主循环非阻塞超时）
        g_tim2_tick_s++;

        // 如果当前处于数据同步状态，则跳过传感器更新
        if (ShoppingState == SHOP_STATE_SYNCING)
        {
            return;
        }

        // printf("[TIM2] Interrupt Triggered.\r\n");
        // 更新温湿度数据
        sensor_data.temper = DS18B20_GetTemperture();
        sensor_data.humidity = DHT11_GetHumidity();

        printf("[Sensor] Temperature: %.2f C, Humidity: %d %%\r\n", sensor_data.temper, sensor_data.humidity);
    }
}

void control_Servo_Door(int open)
{
    // 控制舵机开门的函数实现
    if (open)
    {
        // 打开舵机门
        printf("Servo Door Opened.\r\n");
    }
    else
    {
        // 关闭舵机门
        printf("Servo Door Closed.\r\n");
    }
}
