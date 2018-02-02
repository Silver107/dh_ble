#ifndef __DH_DEBUG_H__
#define __DH_DEBUG_H__

#ifdef DEBUG_LOG
#include "SEGGER_RTT_Conf.h"
#include "SEGGER_RTT.h"
#include "rtt_log.h"
#include "DhDebug.h"
/*
	不要在协议栈的内部时序部分代码中添加日志打印，调试信息的处理比较耗时可能需要一百多us，这已经可以影响
	BLE协议栈底层的时序精度要求了。
*/

#if defined(DEBUG_LOG_USE_RTT)
	#define DEBUG_INFO			logi
	#define DEBUG_WARNING		logw
	#define DEBUG_ERROR			loge
#elif defined(DEBUG_LOG_USE_UART)
	#define DEBUG_INFO			DhPrintfLineFeed
	#define DEBUG_WARNING		DhPrintfLineFeed
	#define DEBUG_ERROR			DhPrintfLineFeed
#endif


#define DEBUG_DATA			DebugData
#define DEBUG_ASCII			DebugAscii


void DebugData(u1 *pu1Data, u2 len);
void DebugAscii(u1 *pu1Data, u2 len);
#else
#define DEBUG_INFO(...)		
#define DEBUG_WARNING(...)	
#define DEBUG_ERROR(...)		
#define DEBUG_DATA(...)
#define DEBUG_ASCII(...)			

#endif


#endif	/* __DEBUG_H__ */

