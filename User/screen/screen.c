#include "./screen/screen.h"

// 要发送的商品数据缓冲区
char* myItems[DATA_BUFFER_VOLUME];
float myPrices[DATA_BUFFER_VOLUME];
int myCounts[DATA_BUFFER_VOLUME];
int totalItems = 3; // 数组长度

// 加载新的商品数据到缓冲区
// 每次和串口屏交互前，先调用此函数加载最新数据
void Screen_Load_New_Data(MCU_Product_t* list, int _totalItems){
	if(_totalItems > DATA_BUFFER_VOLUME) return ;
	
	totalItems = _totalItems;
	for(int i=0; i < totalItems; i++){
		myItems[i] = list[i].product.name;
		myPrices[i] = list[i].product.price;
		myCounts[i] = list[i].num;
	}
}

// 1. 配置串口函数 & 串口初始化
// 功能：初始化 RingBuffer，配置调试串口(UART1)和屏幕串口(UART2)
void Screen_Shopping_System_Init(void)
{
    // 初始化环形缓冲区
    initRingBuff();

    // 注意：不要在这里初始化/重配 USART1。
    // 项目中 USART1 已被协议/调试串口使用（`USART_Config` + `USART1_IRQHandler`）。
    // 这里再次 `uart1_init()` 会 `USART_DeInit(USART1)` 并重配中断，易导致协议接收异常/主循环看似“卡死”。

    // 初始化串口屏串口 (通常串口屏默认波特率是 9600 或 115200，这里假设 9600)
    // 根据实际屏幕配置修改波特率
    uart2_init(115200); 
    
    // printf("System Init OK\r\n");
}

// 2. 返回bool，检测是否收到开始购物消息 (0x01)
// 功能：检查缓冲区是否有数据，如果有且为0x01，则返回 true，否则返回 false
bool Screen_Check_Start_Shopping_Msg(void)
{
    // 检查缓冲区是否有数据
    if (getRingBuffLenght() > 0)
    {
        // 读取一个字节
        uint8_t data = read1BFromRingBuff(0);
        // 从缓冲区删除这个字节（消费掉）
        deleteRingBuff(1);
        
        // 判断是否为 0x01
        if (data == 0x01)
        {
            // printf("Start Shopping Msg Received!\r\n");
            return true;
        }
    }
    return false;
}

// 3. 接收商品信息并在串口屏 t1 组件显示
// 参数：names-商品名数组, prices-价格数组, counts-数量数组, itemNum-商品总种类数
// 格式：(名字，价格，数量)
void Screen_Update_HMI_Shopping_List(void)
{
    // 1. 先清空 t1 组件的内容
    TJCPrintf("t1.txt=\"\"");
    char** names = myItems;
    float *prices = myPrices;
    int *counts = myCounts;
    const int itemNum = totalItems;
    
    // 2. 遍历数组，拼接字符串并发送
    // 注意：由于 TJCPrintf 内部 buffer 只有 100 字节，我们使用追加模式 (t1.txt+=...)
    // 这样可以避免一次性发送过长字符串导致溢出
    
    for(int i = 0; i < itemNum; i++)
    {
        // 格式化单个商品条目，例如: (Apple, 2.5, 10)\r\n
        // 使用 t1.txt+= "..." 进行追加
        TJCPrintf("t1.txt+=\"%7s,%07.2f,%07d\r\n\"", names[i], prices[i], counts[i]);
        
        // 简单的延时，防止串口发送太快屏幕处理不过来（可选）
        for(int k=0; k<5000; k++); 
    }
}

// 4. 发送总价给串口屏
// 功能：计算总价并显示 (假设显示在 t1 的末尾，或者你可以指定其他组件如 t2)
void Screen_Calculate_And_Send_Total(void)
{
    float totalPrice = 0.0f;
    float* prices = myPrices;
    int* counts = myCounts;
    const int itemNum = totalItems;

    // 计算总价
    for(int i = 0; i < itemNum; i++)
    {
        totalPrice += prices[i] * counts[i];
    }
    
    // 发送总价，这里示例追加显示在 t3中，也可以改为 t3.txt="..."
    TJCPrintf("t3.txt+=\"%.2f\"", totalPrice);
    
    // printf("Total Price Calculated: %.2f\r\n", totalPrice);
}

// 5. 监听 Pay Off (0x02) 消息
// 功能：阻塞等待，直到收到 0x02。如果收到 0x03 则忽略并继续等待。
bool Screen_Wait_For_PayOff_Msg(void)
{
    uint8_t data;
    
    // printf("Waiting for Pay Off (0x02)...\r\n");
    
    // 非阻塞轮询：没有数据就直接返回 false，避免把主循环卡死
    if (getRingBuffLenght() > 0)
    {
        data = read1BFromRingBuff(0);
        deleteRingBuff(1);

        if (data == 0x02)
        {
            return true;
        }
        // 其他字节（包括 0x03 超时）在上层用超时机制处理，这里仅消费并忽略
    }

    return false;
}

/*
// 实验程序
int main(void)
{
    // 1. 初始化
    Screen_Shopping_System_Init();
    
    // 模拟的商品数据
    char *myItems[] = {"Apple", "Banana", "Milk"};
    float myPrices[] = {5.5, 3.0, 12.0};
    int myCounts[] = {2, 5, 1};
    int totalItems = 3; // 数组长度
    
    while(1)
    {
        // 2. 轮询检测是否开始购物 (0x01)
        if(Screen_Check_Start_Shopping_Msg() == true)
        {
            // 3. 收到开始信号，发送商品列表到屏幕 t1 组件
            Screen_Update_HMI_Shopping_List(myItems, myPrices, myCounts, totalItems);
            
            // 4. 计算并发送总价
            Screen_Calculate_And_Send_Total(myPrices, myCounts, totalItems);
            
            // 5. 进入结账等待环节 (死循环等待 0x02)
            // 如果收到 0x03 会继续等，直到收到 0x02 才函数返回
            if(Screen_Wait_For_PayOff_Msg() == true)
            {
                // 结账完成后的逻辑
                TJCPrintf("page 1"); // 例如：跳转到支付成功页面
                // 或者清空数据等待下一次
            }
        }
    }
}
*/
