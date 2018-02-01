/**
 * @file				HarwareUart.c
 * @brief			实现平台无关的串口驱动接口
 * @author			fengxun
 * @date			2018年2月1日
*/
#include "../../../include/DhGlobalHead.h"

void HwUartSimpleInit(u1 txPin, u1 rxPin, EnUartBaudrate enBaud)
{
#ifdef HARDWARE_NRF51
    u4 baud;

    baud = NrfUartBaudGet(enBaud);
    NrfUartSimpleInit(rxPin, txPin, baud);
#endif
}

void HwUartSimpleTxData(u1 *pu1Data, u2 len)
{
    u2 index;
#ifdef HARDWARE_NRF51
    for ( index = 0; index < len; index++ )
    {
        NrfUartSimpleTxByte(pu1Data[index]);
    }
#endif
}


void DhPrintf(const char *fmt,...)
{
    u2  index;
    u2  outLen;
    u1  buffer[1024];
    va_list args;
    
    va_start(args,fmt);
    outLen = vsnprintf(buffer, sizeof(buff), fmt, args);
    va_end(args);

    HwUartSimpleTxData(buffer[i], outLen);
}