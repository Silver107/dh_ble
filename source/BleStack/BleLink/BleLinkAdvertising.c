/**
 * @file			BleLinkAdvertising.c
 * @brief			处理链路层广播态下的radio数据
 * @author			fengxun
 * @date			2017年12月12日
*/
#include "../../include/DhGlobalHead.h"

#define     nBLE_LINK_ADV_DEBUG

#if !defined(BLE_LINK_ADV_DEBUG)
#undef DEBUG_INFO
#define DEBUG_INFO(...)

#else
char *ADV_SUB_STATE[5] = {"adv_idle","adv_tx","adv_rx","adv_txscanrsp","adv_rxtimeout"};
#endif

	
#define INVAILD_DATA						(0xFFFF)				/* 无效数据值*/
#define CHANNEL_ENABLE						(1)						/* 通道为使能状态*/
#define ADV_ROUND_OVER						(0xFE)					/* 广播一轮结束，每次广播周期到期后都在37,38,39通道上广播一次*/
#define ADV_CHANNEL_SWITCH_TO_NEXT			0						/* 切换到下一个广播通道开始广播*/
#define ADV_CHANNEL_SWITCH_TO_FIRST			1						/* 强制从第一个通道开始广播*/
#define ADV_RX_WAIT_TIMEOUT					(800)					/* 每个通道上广播完后等待扫描请求或者连接请求的超时时间*/


/* ble规范相关的一些定义 */
#define HEADER_PDU_TYPE_POS				(0)
#define HEADER_PDU_TYPE_MASK			(0x0f)
#define HEADER_TXADD_POS				(6)
#define HEADER_TXADD_MASK				(0x01)
#define HEADER_RXADD_POS				(7)
#define HEADER_RXADD_MASK				(0x01)

#define BLE_ADV_RX_TIMER                BLE_HA_TIMER0


typedef enum
{
	ADV_IDLE = 0,	/* 空闲子状态，一轮广播结束，等待下次广播周期到期 */
	ADV_TX,			/* 发送子状态，发送广播数据 */
	ADV_RX,			/* 接收子状态等待接收状态，可能有连接请求或扫描请求 */
	ADV_TX_SCANRSP, /* 发送响应子状态，针对扫描请求发送扫描响应 */
	ADV_RX_TIMEOUT,	/* 等待接收超时*/
}EnAdvSubState;	/* 广播态子状态*/


typedef struct 
{
	EnAdvSubState		m_enAdvSubState;				            /* 广播状态子状态 */
	BlkAdvChannelOn		m_blkChannels;				                /* 使能的广播通道 */
	BlkBleAddrInfo		m_blkAddrInfo;				                /* 地址信息 */
	
	u2 					m_u2AdvInterval;			                /* 广播间隔,ms为单位        20ms-10.24s */
	u1					m_u1CurrentChannel;			                /* 当前广播通道 */
	u1					m_u1AdvType;				                /* 广播类型 */

	u1					m_pu1LinkTxData[BLE_PDU_LENGTH];			/* 存放待发送数据 */
	u2                  m_u2AdvLen;                                 /* 除去2字节头的长度 */
	u1					m_pu1LinkTxDataBP[BLE_PDU_LENGTH];			/* 存放待发送数据备份，为了支持动态调整广播，如果正在广播的时候设置广播数据，
																			则数据存放在该buff中，广播结束后再重新设置广播数据。*/
	u1					m_pu1LinkScanRspData[BLE_PDU_LENGTH];		/* 扫描响应数据 */
    u2                  m_u2ScanRspLen;                             /* 除去2字节头的长度 */
	u1					m_pu1LinkRxData[BLE_PDU_LENGTH];			/* 存放接收数据buff*/
}BlkAdvStateInfo;

BlkAdvStateInfo s_blkAdvStateInfo;
/**
 *@brief: 		NextAdvChannelGet
 *@details:		获取下一个广播通道

 *@retval:		下一个广播通道
 */
__INLINE static u1 NextAdvChannelGet(void)
{
	u1	currentChannel = s_blkAdvStateInfo.m_u1CurrentChannel;
	u1	nextChannel = ADV_ROUND_OVER;
	
	if( BLE_ADV_CHANNEL_37 == currentChannel )
	{
		if( CHANNEL_ENABLE == s_blkAdvStateInfo.m_blkChannels.m_ChannelOn_38)
		{
			nextChannel = BLE_ADV_CHANNEL_38;
		}
		else if(CHANNEL_ENABLE == s_blkAdvStateInfo.m_blkChannels.m_ChannelOn_39)
		{
			nextChannel = BLE_ADV_CHANNEL_39;
		}
		else
		{
			nextChannel = ADV_ROUND_OVER;
		}
	}
	else if(BLE_ADV_CHANNEL_38 == currentChannel )
	{
		if(CHANNEL_ENABLE == s_blkAdvStateInfo.m_blkChannels.m_ChannelOn_39)
		{
			nextChannel = BLE_ADV_CHANNEL_39;
		}
		else
		{
			nextChannel = ADV_ROUND_OVER;
		}
	}
	else if(BLE_ADV_CHANNEL_39 == currentChannel )
	{
		nextChannel = ADV_ROUND_OVER;
	}
	
	return nextChannel;
}


/**
 *@brief: 		LinkFirstAdvChannelGet
 *@details:		获取第一个广播通道

 *@retval:		广播通道
 */
__INLINE static u1 FirstAdvChannelGet(void)
{
	u1 advChannel = BLE_INVALID_CHANNEL;

	/* 获取第一个广播通道 */
	if( CHANNEL_ENABLE == s_blkAdvStateInfo.m_blkChannels.m_ChannelOn_37 )
	{
		advChannel = BLE_ADV_CHANNEL_37;
	}
	else if( CHANNEL_ENABLE == s_blkAdvStateInfo.m_blkChannels.m_ChannelOn_38 )
	{
		advChannel = BLE_ADV_CHANNEL_38;
	}
	else if( CHANNEL_ENABLE == s_blkAdvStateInfo.m_blkChannels.m_ChannelOn_39 )
	{
		advChannel = BLE_ADV_CHANNEL_39;
	}
	return  advChannel;
}

/**
 *@brief: 		LinkAdvSubStateSwitch
 *@details:		切换链路广播态的子状态

 *@retval:		void
 */
__INLINE static void LinkAdvSubStateSwitch(EnAdvSubState state)
{
	DEBUG_INFO("advsub:%s",ADV_SUB_STATE[state]);
	s_blkAdvStateInfo.m_enAdvSubState = state;
	
}

/**
 *@brief: 		SwitchToNextChannel
 *@details:		切换到下一个广播通道广播
 *@param[in]	startFlag  	1为第一个通道开始广播，0为从当前通道的下一个通道广播
 
 *@retval:		void
 */
__INLINE static void  SwitchToNextChannel( u1 startFlag )
{
	u1 channel;
	u1 whiteIv;

	if( startFlag )
	{
		channel = FirstAdvChannelGet();
	}
	else
	{
		channel = NextAdvChannelGet();
	}

	if( ADV_ROUND_OVER != channel )
	{
	
		whiteIv = GetChannelWhiteIv(channel);									    // 配置白化初值
    	BleRadioWhiteIvCfg(whiteIv);
    	s_blkAdvStateInfo.m_u1CurrentChannel = channel;
    	BleAutoToRxEnable();
    	BleRadioTxData(channel, s_blkAdvStateInfo.m_pu1LinkTxData, BLE_PDU_LENGTH);	// 长度字段实际没有作用	
    	LinkAdvSubStateSwitch(ADV_TX);
		DEBUG_INFO("tx:%d", channel);
    	return ;
    }
    /* 没有下一个通道则不广播，为广播空闲态*/
    LinkAdvSubStateSwitch(ADV_IDLE);	
}



__INLINE static void AdvRxWaitTimeoutHandler(void *pvalue)
{
	// 等待接收超时则关闭接收并切换到下一个通道广播
	BleRadioDisable();
	LinkAdvSubStateSwitch(ADV_RX_TIMEOUT);

	/*
        这里不立刻做切换下一个广播的动作，nrf51芯片对底层radio的状态机限定比较严格，
        如果在radio不是disable的状态下启动发射会导致radio出错，所以这里只做关闭接收操作。
        等收到disabled事件后才启动下个通过的广播。
	*/
}


/**
 *@brief: 		AdvTxScanRsp
 *@details:		发送扫描响应数据
 *@param[in]	void  
 *@param[out]	Ϟ
 *@retval:		__INLINE
 */
__INLINE static void AdvTxScanRsp(void)
{
	u1 channel;

	channel = s_blkAdvStateInfo.m_u1CurrentChannel;				// 获取当前接收到扫描请求的通道
	if( ADV_ROUND_OVER != channel )
	{	
    	BleRadioTxData(channel, s_blkAdvStateInfo.m_pu1LinkScanRspData, BLE_PDU_LENGTH);	// 长度字段实际没有作用	
    	LinkAdvSubStateSwitch(ADV_TX_SCANRSP);
		DEBUG_INFO("tx scan rsp on channel:%d", channel);
		return ;
    }

    LinkAdvSubStateSwitch(ADV_IDLE);	
}


/**
 *@brief: 		HandleAdvTxDone
 *@details:		处理广播发送结束
 *@retval:		void
 */
__INLINE static void HandleAdvTxDone(void)

{
	EnAdvSubState	advState;

	advState = s_blkAdvStateInfo.m_enAdvSubState;								//获取广播子状态
	if ( ADV_TX == advState )
	{
		// 发送完成后在当前通道上开始接收
		BleRadioSimpleRx(s_blkAdvStateInfo.m_pu1LinkRxData);
		LinkAdvSubStateSwitch(ADV_RX);
		BleHAccuracyTimerStart(BLE_ADV_RX_TIMER, ADV_RX_WAIT_TIMEOUT, AdvRxWaitTimeoutHandler, NULL);		// 启动接收超时定时器
		DEBUG_INFO("rx:%d",s_blkAdvStateInfo.m_u1CurrentChannel);

	}
	else if ( ADV_TX_SCANRSP == advState )
	{
		// 如果发送的是扫描响应，则直接切换到下一个广播通道上开始广播
		SwitchToNextChannel(ADV_CHANNEL_SWITCH_TO_NEXT);
		DEBUG_INFO("tx scanrsp done");
    }
}

__INLINE static void HandleAdvRxDone(void)
{
	u1	*pu1Rx = s_blkAdvStateInfo.m_pu1LinkRxData;
	u1	pduType,RxAddType,selfType,TxAddType;

	pduType = pu1Rx[0]&0x0F;			// PDU Type
	RxAddType = (pu1Rx[0]>>7)&0x01;		// RxAdd Type
	TxAddType = (pu1Rx[0]>>6)&0x01;
	selfType = s_blkAdvStateInfo.m_blkAddrInfo.m_u1AddrType;
		
	if( !IsBleRadioCrcOk() )
	{
		return ;
	}
	/* 
		扫描请求处理:			header(2 octets) ScanA(2 octets) AdvA(6 octets)
		连接请求：			header(2 octets) InitA(6 octets) AdvA(6 octets) LLData(22 octets)

		header:PDU Type(4 bits) RFU(2 bits) TxAdd(1 bit) RxAdd(1 bit) Length(6 bits) RFU(2 bits)
	*/	
	if( selfType==RxAddType && memcmp(pu1Rx+8, s_blkAdvStateInfo.m_blkAddrInfo.m_pu1Addr, BLE_ADDR_LEN)==0 )
	{
		BleHAccuracyTimerStop(BLE_ADV_RX_TIMER);	// 停止广播等待接收超时
		if( PDU_TYPE_SCAN_REQ == pduType )
		{
		    
			AdvTxScanRsp();
			DEBUG_INFO("scan req!!!");
		}
		else if( PDU_TYPE_CONNECT_REQ == pduType )
		{
		
			BleLPowerTimerStop(BLE_LP_TIMER0);	
			LinkAdvSubStateSwitch(ADV_IDLE);
			LinkConnReqHandle(TxAddType, pu1Rx+2, pu1Rx+14);
			DEBUG_INFO("connect req!!!");
		}
	}
}

static void LinkAdvRadioEvtHandler(EnBleRadioEvt evt)
{
	if( BLE_RADIO_EVT_TRANS_DONE == evt )	/* 发送完成或接收完成*/
	{
		if( ADV_TX == s_blkAdvStateInfo.m_enAdvSubState ||  ADV_TX_SCANRSP == s_blkAdvStateInfo.m_enAdvSubState)
		{
		    /*
		        收到接收中断说明发送完成了，前面设置了radio disable后自动切换到接收，所以这里radio，已经开始接收了
                所以这里要关掉AutoToRx 的机制，不然接收完 radio进入disable后又会再进入rx
		    */
		    BleAutoToRxDisable();
			//发送完成
			HandleAdvTxDone();
		}
		else if( ADV_RX == s_blkAdvStateInfo.m_enAdvSubState)
		{
			//接收完成
			HandleAdvRxDone();
		}
		else if( ADV_RX_TIMEOUT == s_blkAdvStateInfo.m_enAdvSubState )
		{
            SwitchToNextChannel(ADV_CHANNEL_SWITCH_TO_NEXT);
		}
	}
}





/**
 *@brief: 		AdvIntervalTimeoutHandler
 *@details:		广播间隔到期超时，开始新一轮广播
 *@retval:		void
 */
static __INLINE void AdvIntervalTimeoutHandler(void *pvalue)
{
	DEBUG_INFO("start new adv");
	SwitchToNextChannel(ADV_CHANNEL_SWITCH_TO_FIRST);		// 从第一个广播通道重新开始广播

	BleLPowerTimerStart(BLE_LP_TIMER0, s_blkAdvStateInfo.m_u2AdvInterval*1000, AdvIntervalTimeoutHandler, NULL);		// 启动广播间隔定时器
}



/**
 *@brief: 		LinkAdvParamsCfg
 *@details:		配置广播参数
 *@param[in]	channel  	广播通道配置
 				interval	广播间隔配置,Ms为单位              
 *@retval:		void
 */
void LinkAdvParamsCfg(BlkAdvChannelOn channels, u2 intervalMs)
{
	s_blkAdvStateInfo.m_blkChannels = channels;
	s_blkAdvStateInfo.m_u2AdvInterval = intervalMs;
}


/**
 *@brief: 		LinkAddrInfoCfg
 *@details:		配置BLE地址信息

 *@retval:		void
 */
void LinkAdvAddrInfoCfg(BlkBleAddrInfo addr)
{
	s_blkAdvStateInfo.m_blkAddrInfo = addr;
}

BlkBleAddrInfo LinkAdvAddrInfoGet(void)
{
    return s_blkAdvStateInfo.m_blkAddrInfo;
}

/**
 *@brief: 		LinkAdvDataCfg
 *@details:		配置广播数据
 *@param[in]	pu1Data		广播数据
 *@param[in]	u2Offset	设置数据偏移
 *@param[in]	len			数据长度
 
 *@retval:		u4
 */
u4 LinkAdvDataCfg(u1 *pu1Data, u2 u2Offset, u2	len)
{
	if( (len+u2Offset) > BLE_PDU_LENGTH )
	{
		return ERR_LINK_INVALID_LEN;
	}
	
	memcpy(s_blkAdvStateInfo.m_pu1LinkTxData+u2Offset, pu1Data, len);
	if( BLE_PDU_HEADER_LENGTH == u2Offset )
	{
	    s_blkAdvStateInfo.m_u2AdvLen = len;
	}
	return 	DH_SUCCESS;
}

u2 LinkAdvDataLenGet(void)
{
    return s_blkAdvStateInfo.m_u2AdvLen;
}

/**
 *@brief: 		LinkScanRspCfg
 *@details:		配置扫描响应数据
 *@param[in]	pu1Data		广播数据
 *@param[in]	u2Offset	设置数据偏移
 *@param[in]	len			数据长度

 *@retval:		void
 */
u4	LinkScanRspCfg(u1 *pu1Data, u2 u2Offset, u2	len)
{
	if( (len+u2Offset) > BLE_PDU_LENGTH )
	{
		return ERR_LINK_INVALID_LEN;
	}
	
	memcpy(s_blkAdvStateInfo.m_pu1LinkScanRspData+u2Offset, pu1Data, len);
	if( BLE_PDU_HEADER_LENGTH == u2Offset )
	{
	    s_blkAdvStateInfo.m_u2ScanRspLen = len;
	}
	return 	DH_SUCCESS;
}

u2 LinkScanRspLenGet(void)
{
    return s_blkAdvStateInfo.m_u2ScanRspLen;
}
/**
 *@brief: 		LinkAdvStateInit
 *@details:		链路层广播状态初始化

 *@retval:		void
 */
void LinkAdvStateInit(void)
{
	s_blkAdvStateInfo.m_u2AdvInterval = INVAILD_DATA;
	s_blkAdvStateInfo.m_enAdvSubState = ADV_IDLE;
	BleLinkStateHandlerReg(BLE_LINK_ADVERTISING, LinkAdvRadioEvtHandler);
}

void LinkAdvStateReset(void)
{
    BleLPowerTimerStop(BLE_LP_TIMER0);
    BleHAccuracyTimerStop(BLE_HA_TIMER0);
    s_blkAdvStateInfo.m_enAdvSubState = ADV_IDLE;
}

/**
 *@brief: 		LinkAdvStart
 *@details:		启动广播
				配置crc初值，配置通道，白化初值
 *@retval:		u4
 */
u4 LinkAdvStart(void)
{
	u1 whiteIv;
	u1 channel;
	
	DEBUG_INFO("start adv!!");
	channel = FirstAdvChannelGet();
	if( BLE_INVALID_CHANNEL == channel )
	{
		return ERR_LINK_START_ERROR;
	}
	DEBUG_INFO("adv on channel:%d", channel);

	BleRadioCrcInit(BLE_ADV_CHANNEL_CRC_INIT);					//广播态下crc初值为固定值
	/*
    	先只考虑一个连接的情况吧，都用逻辑地址0
    */
    BleRadioTxRxAddrCfg(0, BLE_ADV_ACCESS_ADDR);
	whiteIv = GetChannelWhiteIv(channel);						// 配置白化初值
    BleRadioWhiteIvCfg(whiteIv);
    s_blkAdvStateInfo.m_u1CurrentChannel = channel;
    BleLinkStateSwitch(BLE_LINK_ADVERTISING);					// 链路状态切换到广播态

    /* 
        发送结束后硬件自动切换成接收，不然等发送结束的时候执行处理代码后才开启接收，nrf51接收开启本身需要130us左右延迟
        这样可能会延误接收扫描响应或者连接请求，让硬件自动打开接收，利用其130us的延迟执行代码可以充分利用时间
    */
    BleAutoToRxEnable();
    BleRadioTxData(channel, s_blkAdvStateInfo.m_pu1LinkTxData, BLE_PDU_LENGTH);	// 长度字段实际没有作用

    /* 启动广播间隔定时器，规范要求间隔应该加上一个 0-10ms的随机延迟，不过这里不实现了 */
   	BleLPowerTimerStart(BLE_LP_TIMER0, s_blkAdvStateInfo.m_u2AdvInterval*1000, AdvIntervalTimeoutHandler, NULL);
	LinkAdvSubStateSwitch(ADV_TX);
 	return DH_SUCCESS;
}



