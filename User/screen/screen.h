#ifndef _SCREEN_H_
#define _SCREEN_H_

#include "stm32f10x.h"
#include "./screen/tjc_usart_hmi.h"
#include "stdbool.h"       // 用于 bool 类型
#include "stdio.h"
#include "products.h"

#define FRAME_LENGTH 7

// 购物车数据
extern char* myItems[DATA_BUFFER_VOLUME];
extern float myPrices[DATA_BUFFER_VOLUME];
extern int myCounts[DATA_BUFFER_VOLUME];
extern int totalItems;

void Screen_Load_New_Data(MCU_Product_t* list, int _totalItems);
void Screen_Shopping_System_Init(void);
bool Screen_Check_Start_Shopping_Msg(void);
void Screen_Update_HMI_Shopping_List(void);
void Screen_Calculate_And_Send_Total(void);
bool Screen_Wait_For_PayOff_Msg(void);



#endif
