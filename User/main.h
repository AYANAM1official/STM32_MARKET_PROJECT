#include "stm32f10x.h"
#include "bsp_usart_dma.h"
#include "protocol.h"
#include "products.h"
#include "stdbool.h"

#include "delay.h" // 引用延时模块
#include "sg90.h"  // 引用舵机模块
#include "ds18b20.h"         //温度
#include "./screen/screen.h" //串口屏
#include "./DHT11/DHT11.h"   //DHT11温湿度（温度数据刷新慢且精度略低，我们这里只使用湿度数据）

//================全程都在同时扫描环形缓冲区和处理状态机======================
void TIM2_IRQHandler(void);
// ==========================================
//商店全局状态
// ==========================================
typedef enum {
    SHOP_STATE_IDLE = 0,        // 空闲状态，等待扫码
    SHOP_STATE_WORKING,         // 工作中，等待扫码等操作
    SHOP_STATE_SCANNING,        // 扫码中，收集扫码数据，用于更新上位机和串口屏
    SHOP_STATE_UPDATING_HMI,    // 更新串口屏显示
    SHOP_STATE_UPDATING_MASTER, // 更新上位机数据同步
    SHOP_STATE_WAITING_PAYOFF,  // 等待结算指令，这里计时等待，超过一定时间（和串口屏约定好），回到SHOP_STATE_SCANNING
    SHOP_STATE_PROCESSING_PAYOFF, // 处理结算，计算总价并串口屏显示，上传给上位机
    SHOP_STATE_EMMERGENCY,   // 紧急状态，打开舵机门，闪灯，响蜂鸣器
    SHOP_STATE_SYNCING        // 数据同步中，暂停模块通信
} ShoppingState_t;
ShoppingState_t last_ShoppingState = SHOP_STATE_IDLE; // 上一个状态
ShoppingState_t ShoppingState = SHOP_STATE_IDLE; // 初始状态为空闲
void Shop_transtate(ShoppingState_t _ShoppingState){
    last_ShoppingState = ShoppingState;
    ShoppingState = _ShoppingState;
}

// ==========================================
//上位机数据同步所需全局变量
// ==========================================
typedef enum {
    SYS_STATE_IDLE = 0,     // 空闲模式：正常响应扫码指令
    SYS_STATE_SYNC_START,   // 同步启动：正在擦除 Flash
    SYS_STATE_SYNC_ING,     // 同步进行中：正在接收并写入数据
} SlaveState_t;
SlaveState_t SlaveState = SYS_STATE_IDLE; //从机状态定义
void Slave_transtate(SlaveState_t _SlaveState){
    SlaveState = _SlaveState;
}
ParsedPacket_t rx_packet;       // 存放协议解析出的数据包
uint32_t sync_received_cnt = 0;   // 已接收到的商品数量计数器，这个变量在每次同步时重置
uint32_t sync_expect_total = 0;   // 上位机告知的预期总数

// ==========================================
//多模块读取到的数据
// ==========================================
#define MAX_TEMPER 35
#define MAX_HUMIDITY 100
typedef struct {
    float temper; // 温度
    uint8_t humidity; // 湿度
} SensorData_t;

SensorData_t sensor_data = {0.0f, 0.0f}; // 初始化传感器数据

void Setup_USART_Interrupt(void);

MCU_Product_t shopping_car[DATA_BUFFER_VOLUME];
int total_products2paid = 0;
void refresh_MCU_products_list(void);

// 根据id添加商品到购物车，如果已存在则数量+1，否则添加新商品（需要从flash读取商品信息）
void add_product_to_shopping_car(uint32_t id){
    if(total_products2paid >= DATA_BUFFER_VOLUME){
        return; // 购物车已满
    }
    int i;
    for(i=0; i<total_products2paid; i++){
        if(shopping_car[i].product.id == id){
            shopping_car[i].num += 1;
            break;
        }
    }
    if(i==total_products2paid){
        // 未找到，添加新种类的商品到购物车
        total_products2paid++;
        // 接下来从flash读取商品信息
        Product_Item_t item;
        if (Product_Find_By_ID(id, &item))
        {
            shopping_car[i].product = item;
            shopping_car[i].num = 1;
        }
    }
}

void clear_shopping_car(void){
    total_products2paid = 0;
}

// 把购物车数据同步到串口屏显示
void refresh_MCU_products_list(void){
    // 同步到串口屏显示类
    Screen_Load_New_Data(shopping_car, total_products2paid);
}
