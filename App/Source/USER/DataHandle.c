/************************************************************************************************
*                                   SRWF-6009
*    (c) Copyright 2015, Software Department, Sunray Technology Co.Ltd
*                               All Rights Reserved
*
* FileName     : DataHandle.c
* Description  :
* Version      :
* Function List:
*------------------------------Revision History--------------------------------------------------
* No.   Version     Date            Revised By      Item            Description
* 1     V1.1        08/12/2015      Zhangxp         SRWF-6009       Original Version
************************************************************************************************/

#define DATAHANDLE_GLOBALS

/************************************************************************************************
*                             Include File Section
************************************************************************************************/
#include "Stm32f10x_conf.h"
#include "ucos_ii.h"
#include "Bsp.h"
#include "Main.h"
#include "Rtc.h"
#include "Timer.h"
#include "SerialPort.h"
#include "Gprs.h"
#include "Flash.h"
#include "Eeprom.h"
#include "DataHandle.h"
#include "Database.h"
#include <string.h>
#include <md5.h>
#include <aes.h>
#include <version.h>


#define OPERATOR_NUMBER	(0x53525746) //SWRF

/************************************************************************************************
*                        Global Variable Declare Section
************************************************************************************************/
uint8 PkgNo;
PORT_NO MonitorPort = Usart_Debug;                      // ��ض˿�
uint8 SubNodesSaveDelayTimer = 0;                       // ������ʱ����ʱ��
uint16 DataUploadTimer = 60;                            // �����ϴ���ʱ��
uint16 DataUpHostTimer = 0;                            // ����������ʱ��
uint16 DataDownloadTimer = 0;  // �����·���ʱ��
uint16 RTCTimingTimer = 60;                             // RTCУʱ����������ʱ��
TASK_STATUS_STRUCT TaskRunStatus;                       // ��������״̬
DATA_HANDLE_TASK DataHandle_TaskArry[MAX_DATA_HANDLE_TASK_NUM];
const uint8 Uart_RfTx_Filter[] = {SYNCWORD1, SYNCWORD2};
const uint8 DayMaskTab[] = {0xF0, 0xE0, 0xC0, 0x80};
uint16 DataDownloadNodeId = 0xABCD;

extern void Gprs_OutputDebugMsg(bool NeedTime, uint8 *StrPtr);

#define PRINT_INFO  0 //  1

uint8 test_print(DATA_FRAME_STRUCT *DataFrmPtr)
{
    uint8 *AsciiBuf = NULL;

    if ((void *)0 == (AsciiBuf = OSMemGetOpt(LargeMemoryPtr, 20, TIME_DELAY_MS(50)))) {
        return -1;
    }
    memset(AsciiBuf, 0, MEM_LARGE_BLOCK_LEN);
    BcdToAscii((uint8 *)DataFrmPtr, AsciiBuf, sizeof(DATA_FRAME_STRUCT), 3);
    Gprs_OutputDebugMsg(TRUE, AsciiBuf);
    OSMemPut(LargeMemoryPtr, AsciiBuf);
    return 0;
}

void DebugOutputLength(uint8 *StrPtr, uint8 SrcLength)
{
    uint16 len;
    uint8 *bufPtr = NULL;

    if ((void *)0 == (bufPtr = OSMemGetOpt(LargeMemoryPtr, 20, TIME_DELAY_MS(50)))) {
        return;
    }
    len = BcdToAscii( StrPtr, (uint8 *)bufPtr, SrcLength, 3);
    DataHandle_OutputMonitorMsg(Gprs_Connect_Msg, bufPtr, len);
    OSMemPut(LargeMemoryPtr, bufPtr);
    return;
}

/************************************************************************************************
*                           Prototype Declare Section
************************************************************************************************/
//void BigDataDebug(uint8 *BufPtr);

/************************************************************************************************
*                           Function Declare Section
************************************************************************************************/

/************************************************************************************************
* Function Name: DataHandle_GetEmptyTaskPtr
* Decription   : ����������������յ�����ָ��
* Input        : ��
* Output       : �����ָ��
* Others       : ��
************************************************************************************************/
DATA_HANDLE_TASK *DataHandle_GetEmptyTaskPtr(void)
{
    uint8 i;

    // ����δ��ռ�õĿռ�,���������ϴ�����
    for (i = 0; i < MAX_DATA_HANDLE_TASK_NUM; i++) {
        if ((void *)0 == DataHandle_TaskArry[i].StkPtr) {
            return (&DataHandle_TaskArry[i]);
        }
    }

    // �������ȫ��ʹ�÷��ؿն���
    return ((void *)0);
}

/************************************************************************************************
* Function Name: DataHandle_SetPkgProperty
* Decription   : ���ð�����ֵ
* Input        : PkgXor-��������Ӫ�̱��벻����־: 0-����� 1-���
*                NeedAck-�Ƿ���Ҫ��ִ 0-�����ִ 1-��Ҫ��ִ
*                PkgType-֡���� 0-����֡ 1-Ӧ��֡
*                Dir-�����б�ʶ 0-���� 1-����
* Output       : ����ֵ
* Others       : ��
************************************************************************************************/
PKG_PROPERTY DataHandle_SetPkgProperty(bool PkgXor, bool NeedAck, bool PkgType, bool Dir)
{
    PKG_PROPERTY pkgProp;

    pkgProp.Content = 0;
	pkgProp.AddrSize = 0x1;
    pkgProp.RxSleep = 0;
    pkgProp.TxSleep = 0;
    pkgProp.PkgXor = PkgXor;
    pkgProp.NeedAck = NeedAck;
    pkgProp.Encrypt = Concentrator.Param.DataEncryptCtrl;
    pkgProp.PkgType = PkgType;
    pkgProp.Direction = Dir;
    return pkgProp;
}

/************************************************************************************************
* Function Name: DataHandle_SetPkgPath
* Decription   : �������ݰ���·��
* Input        : DataFrmPtr-����ָ��
*                ReversePath-�Ƿ���Ҫ��ת·��
* Output       : ��
* Others       : ��
************************************************************************************************/
void DataHandle_SetPkgPath(DATA_FRAME_STRUCT *DataFrmPtr, bool ReversePath)
{
    uint8 i, tmpBuf[LONG_ADDR_SIZE];

    if (0 == memcmp(BroadcastAddrIn, DataFrmPtr->Route[DataFrmPtr->RouteInfo.CurPos], LONG_ADDR_SIZE)) {
        memcpy(DataFrmPtr->Route[DataFrmPtr->RouteInfo.CurPos], Concentrator.LongAddr, LONG_ADDR_SIZE);
    }
    // ·���Ƿ�ת����
    if (REVERSED == ReversePath) {
        DataFrmPtr->RouteInfo.CurPos = DataFrmPtr->RouteInfo.Level - 1 - DataFrmPtr->RouteInfo.CurPos;
        for (i = 0; i < DataFrmPtr->RouteInfo.Level / 2; i++) {
            memcpy(tmpBuf, DataFrmPtr->Route[i], LONG_ADDR_SIZE);
            memcpy(DataFrmPtr->Route[i], DataFrmPtr->Route[DataFrmPtr->RouteInfo.Level - 1 - i], LONG_ADDR_SIZE);
            memcpy(DataFrmPtr->Route[DataFrmPtr->RouteInfo.Level - 1 - i], tmpBuf, LONG_ADDR_SIZE);
        }
    }
}


/************************************************************************************************
* Function Name: DataHandle_ExtractData
* Decription   : ��Э����ȡ�����ݲ��������ݵ���ȷ��
* Input        : BufPtr-ԭ����ָ��
* Output       : �ɹ������˵��
* Others       : ע��-�ɹ����ô˺�����BufPtrָ����ȡ���ݺ���ڴ�
************************************************************************************************/
EXTRACT_DATA_RESULT DataHandle_ExtractData(uint8 *BufPtr)
{
    uint8 i, *msg = NULL, *AesMsg = NULL;
    uint16 tmp;
    PORT_BUF_FORMAT *portBufPtr = NULL;
    DATA_FRAME_STRUCT *dataFrmPtr = NULL;
    uint16 AesPackLen;

    // ��Э���ʽ��ȡ��Ӧ������
    portBufPtr = (PORT_BUF_FORMAT *)BufPtr;
    if (FALSE == portBufPtr->Property.FilterDone) {
        return Error_Data;
    }
    // ����һ���ڴ����ڴ����ȡ�������
    if ((void *)0 == (msg = OSMemGetOpt(LargeMemoryPtr, 10, TIME_DELAY_MS(50)))) {
        return Error_GetMem;
    }
    dataFrmPtr = (DATA_FRAME_STRUCT *)msg;
    dataFrmPtr->PortNo = portBufPtr->Property.PortNo;
    dataFrmPtr->PkgLength = ((uint16 *)portBufPtr->Buffer)[0] & 0x03FF;
    dataFrmPtr->PkgProp.Content = portBufPtr->Buffer[2];

    //���ݽ���
    if( 0x1 == dataFrmPtr->PkgProp.Encrypt){
        // ����һ���ڴ����ڴ�� AES �ӽ��ܵ�����
        if ((void *)0 == (AesMsg = OSMemGetOpt(LargeMemoryPtr, 10, TIME_DELAY_MS(50)))) {
            OSMemPut(LargeMemoryPtr, msg);
            return Error_GetMem;
        }

        //�˴�ʹ�ð�������� md5 ���ݰ�������������Կ
        unsigned char md5Test[10] = {0};
        unsigned char md5Key[16] = {0};
		MD5_CTX md5;
        md5Test[0] = portBufPtr->Buffer[3];

		MD5Init(&md5, OPERATOR_NUMBER);
		MD5Update(&md5,md5Test,strlen((char *)md5Test));//����Ҫ���ܵ����ݺͳ���
		MD5Final(&md5,md5Key);//md5 ���ܳ� 16 �ֽ�����

        AesPackLen = dataFrmPtr->PkgLength - 6;

        //��������Ϊ'������'��'�����ź�ǿ��'.
        AES_DecryptData( &portBufPtr->Buffer[4], AesPackLen, AesMsg, md5Key);
        memcpy(portBufPtr->Buffer, AesMsg, AesPackLen);

        //��־λ��Ϊ�ѽ���
        dataFrmPtr->PkgProp.Encrypt = 0x0;
        portBufPtr->Buffer[2] = dataFrmPtr->PkgProp.Content;

        OSMemPut(LargeMemoryPtr, AesMsg);
    }

    dataFrmPtr->PkgSn = portBufPtr->Buffer[3];
    dataFrmPtr->Command = (COMMAND_TYPE)(portBufPtr->Buffer[4]);

    dataFrmPtr->DeviceType = portBufPtr->Buffer[5];
    dataFrmPtr->Life_Ack.Content = portBufPtr->Buffer[6];
    dataFrmPtr->RouteInfo.Content = portBufPtr->Buffer[7];
    memset(dataFrmPtr->Route[0], 0, (MAX_ROUTER_NUM+1)*LONG_ADDR_SIZE);
    for (i = 0; i < dataFrmPtr->RouteInfo.Level && i < MAX_ROUTER_NUM; i++) {
        memcpy(dataFrmPtr->Route[i], &portBufPtr->Buffer[8 + LONG_ADDR_SIZE * i], LONG_ADDR_SIZE);
    }
    dataFrmPtr->DownRssi = *(portBufPtr->Buffer + dataFrmPtr->PkgLength- 5);
    dataFrmPtr->UpRssi = *(portBufPtr->Buffer + dataFrmPtr->PkgLength - 4);
    //dataFrmPtr->Crc16 = *(portBufPtr->Buffer + dataFrmPtr->PkgLength - 3);
    dataFrmPtr->Crc16 = ((portBufPtr->Buffer[dataFrmPtr->PkgLength - 3] << 8)&0xff00)|(portBufPtr->Buffer[dataFrmPtr->PkgLength - 2]&0xFF);
    tmp = LONG_ADDR_SIZE * dataFrmPtr->RouteInfo.Level + DATA_FIXED_AREA_LENGTH;
    if (dataFrmPtr->PkgLength < tmp || dataFrmPtr->PkgLength > MEM_LARGE_BLOCK_LEN - 1) {
        OSMemPut(LargeMemoryPtr, msg);
        return Error_DataLength;
    }
    dataFrmPtr->DataLen = dataFrmPtr->PkgLength - tmp;
    if (dataFrmPtr->DataLen < MEM_LARGE_BLOCK_LEN - sizeof(DATA_FRAME_STRUCT)) {
        memcpy(dataFrmPtr->DataBuf, portBufPtr->Buffer + 8 + LONG_ADDR_SIZE * dataFrmPtr->RouteInfo.Level, dataFrmPtr->DataLen);
    } else {
        OSMemPut(LargeMemoryPtr, msg);
        return Error_DataOverFlow;
    }

    // ���Crc8�Ƿ���ȷ
    if (dataFrmPtr->Crc16 != CalCrc16(portBufPtr->Buffer, dataFrmPtr->PkgLength - 3) || portBufPtr->Length < dataFrmPtr->PkgLength) {
        OSMemPut(LargeMemoryPtr, msg);
        return Error_DataCrcCheck;
    }

    // ���������Ƿ��� 0x16
    if ( 0x16 != *(portBufPtr->Buffer + dataFrmPtr->PkgLength-1)) {
        OSMemPut(LargeMemoryPtr, msg);
        return Error_Data;
    }

	dataFrmPtr->DownRssi = 0;
	dataFrmPtr->UpRssi = *(portBufPtr->Buffer + dataFrmPtr->PkgLength);
	if( (0x1 == dataFrmPtr->RouteInfo.Content) &&
		(0 == memcmp(Concentrator.LongAddr, dataFrmPtr->Route[dataFrmPtr->RouteInfo.CurPos], LONG_ADDR_SIZE))){
        memcpy(BufPtr, msg, MEM_LARGE_BLOCK_LEN);
        OSMemPut(LargeMemoryPtr, msg);
        return Ok_Data;
	}

    // ����Ƿ�Ϊ�㲥��ַ�򱾻���ַ
    dataFrmPtr->RouteInfo.CurPos += 1;
    if ((0 == memcmp(Concentrator.LongAddr, dataFrmPtr->Route[dataFrmPtr->RouteInfo.CurPos], LONG_ADDR_SIZE) ||
        0 == memcmp(BroadcastAddrIn, dataFrmPtr->Route[dataFrmPtr->RouteInfo.CurPos], LONG_ADDR_SIZE)) &&
        dataFrmPtr->RouteInfo.CurPos < dataFrmPtr->RouteInfo.Level) {
        memcpy(BufPtr, msg, MEM_LARGE_BLOCK_LEN);
        OSMemPut(LargeMemoryPtr, msg);
        return Ok_Data;
    }

    // Ҫ���к�������,���Դ˴�����ȡ�������ݷ���
    memcpy(BufPtr, msg, MEM_LARGE_BLOCK_LEN);
    OSMemPut(LargeMemoryPtr, msg);
    return Error_DstAddress;
}

/************************************************************************************************
* Function Name: DataHandle_CreateTxData
* Decription   : �����������ݰ�
* Input        : DataFrmPtr-�����͵�����
* Output       : �ɹ������
* Others       : �ú���ִ����Ϻ���ͷ�DataBufPtrָ��Ĵ洢��,���Ὣ·�����ĵ�ַ���з�ת
************************************************************************************************/
ErrorStatus DataHandle_CreateTxData(DATA_FRAME_STRUCT *DataFrmPtr)
{
    uint8 err;
    //uint8 *AesMsg = NULL;
    //uint16 AesPackLen;
    uint16 tmp, nodeId;
    PORT_BUF_FORMAT *txPortBufPtr = NULL;
    uint16 crc16;

	//uint8 delay = 0;
    //RTC_TIME rtcTime;
	// �����ʱ����ֹ����������ײ��
    //Rtc_Get(&rtcTime, Format_Bcd);
	//delay = (rtcTime.Second % 3) * 20 + (Concentrator.LongAddr[LONG_ADDR_SIZE-1]&0xf) * 100;
	//if( 0 != delay){
	//	OSTimeDlyHMSM(0, 0, 0, delay);
	//}

    // ������һ���ڴ������м����ݴ���
    if ((void *)0 == (txPortBufPtr = OSMemGetOpt(LargeMemoryPtr, 10, TIME_DELAY_MS(50)))) {
        OSMemPut(LargeMemoryPtr, DataFrmPtr);
        return ERROR;
    }
    txPortBufPtr->Property.PortNo = DataFrmPtr->PortNo;
    txPortBufPtr->Property.FilterDone = 1;
    memcpy(txPortBufPtr->Buffer, Uart_RfTx_Filter, sizeof(Uart_RfTx_Filter));
    txPortBufPtr->Length = sizeof(Uart_RfTx_Filter);

    tmp = txPortBufPtr->Length;
    DataFrmPtr->PkgLength = DataFrmPtr->DataLen + DataFrmPtr->RouteInfo.Level * LONG_ADDR_SIZE + DATA_FIXED_AREA_LENGTH;
    ((uint16 *)(&txPortBufPtr->Buffer[txPortBufPtr->Length]))[0] = DataFrmPtr->PkgLength;
    txPortBufPtr->Length += 2;
    txPortBufPtr->Buffer[txPortBufPtr->Length++] = DataFrmPtr->PkgProp.Content;         // ���ı�ʶ
    txPortBufPtr->Buffer[txPortBufPtr->Length++] = DataFrmPtr->PkgSn;                   // �����
    txPortBufPtr->Buffer[txPortBufPtr->Length++] = DataFrmPtr->Command;                 // ������
    txPortBufPtr->Buffer[txPortBufPtr->Length++] = DataFrmPtr->DeviceType;          	// �豸����
    txPortBufPtr->Buffer[txPortBufPtr->Length++] = DataFrmPtr->Life_Ack.Content;        // �������ں�Ӧ���ŵ�
    txPortBufPtr->Buffer[txPortBufPtr->Length++] = DataFrmPtr->RouteInfo.Content;       // ·����Ϣ
    memcpy(&txPortBufPtr->Buffer[txPortBufPtr->Length], DataFrmPtr->Route[0], DataFrmPtr->RouteInfo.Level * LONG_ADDR_SIZE);
    txPortBufPtr->Length += DataFrmPtr->RouteInfo.Level * LONG_ADDR_SIZE;
    memcpy(txPortBufPtr->Buffer + txPortBufPtr->Length, DataFrmPtr->DataBuf, DataFrmPtr->DataLen);      // ������
    txPortBufPtr->Length += DataFrmPtr->DataLen;
    txPortBufPtr->Buffer[txPortBufPtr->Length++] = 0x55;                                // �����ź�ǿ��
    txPortBufPtr->Buffer[txPortBufPtr->Length++] = 0x55;                                // �����ź�ǿ��
    crc16 = CalCrc16((uint8 *)(&txPortBufPtr->Buffer[tmp]), txPortBufPtr->Length - tmp);     // Crc8У��
    txPortBufPtr->Buffer[txPortBufPtr->Length++] = (uint8)((crc16 >> 8)&0xFF);
    txPortBufPtr->Buffer[txPortBufPtr->Length++] = (uint8)((crc16)&0xFF);
    txPortBufPtr->Buffer[txPortBufPtr->Length++] = TAILBYTE;

    if (CMD_PKG == DataFrmPtr->PkgProp.PkgType) {
        txPortBufPtr->Buffer[txPortBufPtr->Length++] = 0x1E;
        nodeId = Data_FindNodeId(0, DataFrmPtr->Route[DataFrmPtr->RouteInfo.Level - 1]);
        if (DATA_CENTER_ID == nodeId || NULL_U16_ID == nodeId) {
            txPortBufPtr->Buffer[txPortBufPtr->Length++] = DEFAULT_TX_CHANNEL;
        } else {
            txPortBufPtr->Buffer[txPortBufPtr->Length++] = SubNodes[nodeId].RxChannel;
        }
        txPortBufPtr->Buffer[txPortBufPtr->Length++] = (DEFAULT_RX_CHANNEL + CHANNEL_OFFSET);
    } else {
        txPortBufPtr->Buffer[txPortBufPtr->Length++] = 0x00;
        // Ϊ�˺��ѳ����ĵ�һ�������
        if (0 == DataFrmPtr->Life_Ack.AckChannel) {
            txPortBufPtr->Buffer[txPortBufPtr->Length++] = (DEFAULT_RX_CHANNEL + CHANNEL_OFFSET);
        } else {
            txPortBufPtr->Buffer[txPortBufPtr->Length++] = DataFrmPtr->Life_Ack.AckChannel;
        }
        txPortBufPtr->Buffer[txPortBufPtr->Length++] = (DEFAULT_RX_CHANNEL + CHANNEL_OFFSET);
    }
#if 0
	//���ݼ���
	if (0x0 == DataFrmPtr->PkgProp.Encrypt) {

		// ������һ���ڴ������м����ݴ���
		if ((void *)0 == (AesMsg = OSMemGetOpt(LargeMemoryPtr, 10, TIME_DELAY_MS(50)))) {
			OSMemPut(LargeMemoryPtr, DataFrmPtr);
			return ERROR;
		}

		unsigned char md5Test[10] = {0};
		unsigned char md5Key[16] = {0};

		//�˴�ʹ�ð�������� md5 ���ݰ�������������Կ
		md5Test[0] = txPortBufPtr->Buffer[3];

		//md5 ���� 16 �ֽ���Կ
		MD5_EncryptData(md5Test, strlen((char *)md5Test), md5Key, OPERATOR_NUMBER);
		AesPackLen = DataFrmPtr->PkgLength - 6;

		//��������Ϊ'������'��'�����ź�ǿ��'.
		AES_EncryptData( &txPortBufPtr->Buffer[4], AesPackLen, AesMsg, md5Key);
		memcpy(txPortBufPtr->Buffer, AesMsg, AesPackLen);

		//��־λ��Ϊ�Ѽ���
		DataFrmPtr->PkgProp.Encrypt = 0x0;
		txPortBufPtr->Buffer[2] = DataFrmPtr->PkgProp.Content;

		OSMemPut(LargeMemoryPtr, AesMsg);

	}
#endif
    OSMemPut(LargeMemoryPtr, DataFrmPtr);
    if (Uart_Gprs == txPortBufPtr->Property.PortNo) {
        if (FALSE == Gprs.Online ||
            OS_ERR_NONE != OSMboxPost(Gprs.MboxTx, txPortBufPtr)) {
            OSMemPut(LargeMemoryPtr, txPortBufPtr);
            return ERROR;
        } else {
            OSFlagPost(GlobalEventFlag, FLAG_GPRS_TX, OS_FLAG_SET, &err);
            return SUCCESS;
        }
    } else {
        if (txPortBufPtr->Property.PortNo < Port_Total &&
            OS_ERR_NONE != OSMboxPost(SerialPort.Port[txPortBufPtr->Property.PortNo].MboxTx, txPortBufPtr)) {
            OSMemPut(LargeMemoryPtr, txPortBufPtr);
            return ERROR;
        } else {
            OSFlagPost(GlobalEventFlag, (OS_FLAGS)(1 << txPortBufPtr->Property.PortNo + SERIALPORT_TX_FLAG_OFFSET), OS_FLAG_SET, &err);
            return SUCCESS;
        }
    }
}

// ****���ڴ����ݵ���
/*
void BigDataDebug(uint8 *BufPtr)
{
    PORT_BUF_FORMAT *portBufPtr;
    DATA_FRAME_STRUCT *DataFrmPtr;
    uint16 len;
    uint8 *p;

    portBufPtr = (PORT_BUF_FORMAT *)BufPtr;
    len = portBufPtr->Buffer[0] + portBufPtr->Buffer[1] * 256;
    p = portBufPtr->Buffer + len - 4 - 2;
    if (0x39 == *p) {
        return;
    }
    if ((void *)0 == (DataFrmPtr = OSMemGetOpt(LargeMemoryPtr, 10, TIME_DELAY_MS(50)))) {
        return;
    }
    DataFrmPtr->PortNo = Uart_Gprs;
    DataFrmPtr->PkgLength = DATA_FIXED_AREA_LENGTH;
    DataFrmPtr->PkgSn = PkgNo++;
    DataFrmPtr->DeviceType = Dev_Concentrator;
    DataFrmPtr->Life_Ack.Content = 0x0F;
    DataFrmPtr->RouteInfo.CurPos = 0;
    DataFrmPtr->RouteInfo.Level = 2;
    memcpy(DataFrmPtr->Route[0], Concentrator.LongAddr, LONG_ADDR_SIZE);
    memcpy(DataFrmPtr->Route[1], BroadcastAddrOut, LONG_ADDR_SIZE);
    DataFrmPtr->DataLen = 2 + len;
    DataFrmPtr->DataBuf[0] = 0xEE;
    DataFrmPtr->DataBuf[1] = 0xEE;
    memcpy(DataFrmPtr->DataBuf + 2, portBufPtr->Buffer, len);
    DataFrmPtr->PkgProp = DataHandle_SetPkgProperty(XOR_OFF, NONE_ACK, CMD_PKG, UP_DIR);
    DataHandle_SetPkgPath(DataFrmPtr, UNREVERSED);
    DataHandle_CreateTxData(DataFrmPtr);
}
*/
// ****���ڴ����ݵ���

/************************************************************************************************
* Function Name: DataHandle_DataDelaySaveProc
* Decription   : ������ʱ���洦����
* Input        : ��
* Output       : ��
* Others       : ������������Ҫ����ʱ����һ����ʱ����,���ӳ�Flash������
************************************************************************************************/
void DataHandle_DataDelaySaveProc(void)
{
    SubNodesSaveDelayTimer = 0;
    Flash_SaveSubNodesInfo();
    Flash_SaveConcentratorInfo();
}

/************************************************************************************************
* Function Name: DataHandle_OutputMonitorMsg
* Decription   : ������������������Ϣ
* Input        : MsgType-��Ϣ������,MsgPtr-�����Ϣָ��,MsgLen-��Ϣ�ĳ���
* Output       : ��
* Others       : ��
************************************************************************************************/
void DataHandle_OutputMonitorMsg(MONITOR_MSG_TYPE MsgType, uint8 *MsgPtr, uint16 MsgLen)
{
    DATA_FRAME_STRUCT *dataFrmPtr = NULL;

    if ((void *)0 == (dataFrmPtr = OSMemGetOpt(LargeMemoryPtr, 20, TIME_DELAY_MS(50)))) {
        return;
    }
    dataFrmPtr->PortNo = MonitorPort;
    dataFrmPtr->PkgProp = DataHandle_SetPkgProperty(XOR_OFF, NONE_ACK, CMD_PKG, UP_DIR);
    dataFrmPtr->PkgSn = PkgNo++;
    dataFrmPtr->Command = Output_Monitior_Msg_Cmd;
    dataFrmPtr->DeviceType = Dev_Concentrator;
    dataFrmPtr->Life_Ack.Content = 0x0F;
    dataFrmPtr->RouteInfo.CurPos = 0;
    dataFrmPtr->RouteInfo.Level = 2;
    memcpy(dataFrmPtr->Route[0], Concentrator.LongAddr, LONG_ADDR_SIZE);
    memcpy(dataFrmPtr->Route[1], BroadcastAddrOut, LONG_ADDR_SIZE);
    dataFrmPtr->DataBuf[0] = MsgType;
    memcpy(&dataFrmPtr->DataBuf[1], MsgPtr, MsgLen);
    dataFrmPtr->DataLen = 1 + MsgLen;
    DataHandle_SetPkgPath(dataFrmPtr, UNREVERSED);
    DataHandle_CreateTxData(dataFrmPtr);
    return;
}


/************************************************************************************************
* Function Name: DataHandle_LockStatusDataSaveProc
* Decription   : ����״̬���ݱ��洦����
* Input        : DataFrmPtr-ָ������֡��ָ��
* Output       : �ɹ���ʧ��
* Others       : ���ڱ������״̬���ݺ��������ź�ǿ��
************************************************************************************************/
uint8 DataHandle_LockStatusDataSaveProc(DATA_FRAME_STRUCT *DataFrmPtr)
{
    uint16 nodeId;
    METER_DATA_SAVE_FORMAT *meterBufPtr = NULL;
    uint8 nodeAddr;

    nodeId = Data_FindNodeId(0, DataFrmPtr->Route[0]);
    if (NULL_U16_ID == nodeId) {
        return OP_Failure;
    }
    // ����������Ƿ���ȷ + ��������򳤶��Ƿ���ȷ
    if ((DataFrmPtr->Command != Lock_Status_Report_Cmd) ||
        ( DataFrmPtr->DataLen != LOCK_STATUS_DATA_SIZE - UPDOWN_RSSI_SIZE )){
        return OP_Failure;
    }
    // ����ռ����ڱ������״̬����
    if ((void *)0 == (meterBufPtr = OSMemGetOpt(SmallMemoryPtr, 10, TIME_DELAY_MS(50)))) {
        return OP_Failure;
    }

    // ��ȡ�� Eeprom �б���ĵ���״̬���ݡ�
    memset((uint8 *)meterBufPtr, 0 , MEM_SMALL_BLOCK_LEN);
    Eeprom_ReadWrite((uint8 *)meterBufPtr, nodeId * NODE_INFO_SIZE, NODE_INFO_SIZE, READ_ATTR);

    // ����ڵ��ַ�������ʼ���ýڵ�
    if (0 != memcmp(SubNodes[nodeId].LongAddr, meterBufPtr->Address, LONG_ADDR_SIZE)) {
        Data_MeterDataInit(meterBufPtr, nodeId, NODE_INFO_SIZE - sizeof(METER_DATA_SAVE_FORMAT));
    }

    // ����ȡ���ĵ������ݵĸ��µ����һ��
    if( meterBufPtr->MeterData[0] >= LOCK_STATUS_DATA_NUM){
        if(DataFrmPtr->PkgProp.TxSleep == 0x0){
            DataDownloadTimer = 3;
        }
        DataUploadTimer = 2;
        DataDownloadNodeId = nodeId;
        OSMemPut(SmallMemoryPtr, meterBufPtr);
        return ERROR;
    }
    nodeAddr = meterBufPtr->MeterData[0] * LOCK_STATUS_DATA_SIZE + 1;
    memcpy(&meterBufPtr->MeterData[nodeAddr], DataFrmPtr->DataBuf, LOCK_STATUS_DATA_SIZE-UPDOWN_RSSI_SIZE);
	meterBufPtr->MeterData[nodeAddr + LOCK_STATUS_DATA_SIZE-UPDOWN_RSSI_SIZE] = DataFrmPtr->UpRssi;//�����ź�ǿ��

	// �������һ���ֽڴ�����ݰ� ���������Ϊ 10���������ֲ��豣�棬ֱ�Ӷ���
    meterBufPtr->MeterData[0] += 1;
    meterBufPtr->RxLastDataNum = meterBufPtr->MeterData[0];// ������һ����������λ�ã����ڼ�������λ�������ȡ����

    meterBufPtr->Property.CurRouteNo = SubNodes[nodeId].Property.CurRouteNo;
    meterBufPtr->Property.LastResult = SubNodes[nodeId].Property.LastResult = 1;

    // �ýڵ���շ��ŵ�λ������ĩβ��4���ֽ�(Ҫ���ǵ��շ��ŵ�������)
    SubNodes[nodeId].RxChannel = DataFrmPtr->DataBuf[LOCK_STATUS_DATA_SIZE + 5];

    //����״̬�仯�����̱��沢�ϱ�������
    meterBufPtr->Crc8MeterData = CalCrc8(meterBufPtr->MeterData, NODE_INFO_SIZE - sizeof(METER_DATA_SAVE_FORMAT));
    meterBufPtr->Property.UploadData = SubNodes[nodeId].Property.UploadData = FALSE;
    meterBufPtr->Property.UploadPrio = SubNodes[nodeId].Property.UploadPrio = HIGH;
    Eeprom_ReadWrite((uint8 *)meterBufPtr, nodeId * NODE_INFO_SIZE, NODE_INFO_SIZE, WRITE_ATTR);

    DataUploadTimer = 2;
    if(DataFrmPtr->PkgProp.TxSleep == 0x0){
        DataDownloadTimer = 3;
    }
    if( 0xABCD == DataDownloadNodeId){
        DataDownloadNodeId = nodeId;
    }

    OSMemPut(SmallMemoryPtr, meterBufPtr);
    return OP_Succeed;
}


/************************************************************************************************
* Function Name: DataHandle_LockGprsDataSaveProc
* Decription   : �������·���������溯��
* Input        : DataFrmPtr-ָ������֡��ָ��
* Output       : �ɹ���ʧ��
* Others       : ���ڱ������״̬���ݺ��������ź�ǿ��
************************************************************************************************/
bool DataHandle_LockGprsDataSaveProc(DATA_FRAME_STRUCT *DataFrmPtr)
{
    uint16 nodeId;
    METER_DATA_SAVE_FORMAT *meterBufPtr = NULL;
    uint8 *msg = NULL;
    uint8 statusSize, flag, OpResult;

    // ����Ƿ�������ڵ�
    nodeId = Data_FindNodeId(0, DataFrmPtr->Route[0]);
    if (NULL_U16_ID == nodeId) {
        return ERROR;
    }
    // ����������Ƿ���ȷ + ��������򳤶��Ƿ���ȷ
    if ( !((DataFrmPtr->Command == Lock_Status_Issued_Cmd)&&(DataFrmPtr->DataLen == LOCK_GPRS_DATA_SIZE)) &&
         !((DataFrmPtr->Command == Lock_Key_Updata_Cmd)&&(DataFrmPtr->DataLen == LOCK_KEY_DATA_SIZE))) {
        return ERROR;
    }


    // ����ռ����ڱ������״̬����
    if ((void *)0 == (meterBufPtr = OSMemGetOpt(SmallMemoryPtr, 10, TIME_DELAY_MS(50)))) {
        return ERROR;
    }
    // ����ռ����ڱ����м�����
    if ((void *)0 == (msg = OSMemGetOpt(SmallMemoryPtr, 10, TIME_DELAY_MS(50)))) {
        OSMemPut(SmallMemoryPtr, meterBufPtr);
        return ERROR;
    }

    // ��ȡ�� Eeprom �б���ĵ���״̬���ݡ�
    memset((uint8 *)meterBufPtr, 0 , MEM_SMALL_BLOCK_LEN);
    Eeprom_ReadWrite((uint8 *)meterBufPtr, nodeId * NODE_INFO_SIZE, NODE_INFO_SIZE, READ_ATTR);

    // ����ڵ��ַ�������ʼ���ýڵ�
    if (0 != memcmp(SubNodes[nodeId].LongAddr, meterBufPtr->Address, LONG_ADDR_SIZE)) {
        Data_MeterDataInit(meterBufPtr, nodeId, NODE_INFO_SIZE - sizeof(METER_DATA_SAVE_FORMAT));
    }
    // statusSize ���ȵ�ַ�������״̬����
    statusSize = LOCK_STATUS_DATA_NUM * LOCK_STATUS_DATA_SIZE + 1;
    flag = DataFrmPtr->DataBuf[0]; // �·����ݵı�־�����ݱ�־���в�ͬ����

    // ����ظ���������Ҫ������һ������
    if( 0x80 == (flag & 0x80)){
        // ��ԭ����(������1 + ����״̬1 + ���ݳ���1 + �·�����n)�����ڻ����У��Ա�������Ĵ���
        memcpy(msg, &meterBufPtr->MeterData[statusSize], meterBufPtr->MeterData[statusSize + 2] + 3);
    }

    // 0x0:δ�·��� 0x1 ���ɹ� ��0x2 ��ʧ��
	if ( 0x1 == (flag & 0x1)){
        // ���Ǳ����·�����
            // ����ȡ���ķ������·�������������ݸ���
            meterBufPtr->MeterData[statusSize] = DataFrmPtr->Command; // ���������
            // ��Ŵ˰����ݵ�״̬���Ƿ��Ѿ����͸�������
            // 0x0:δ�·� 0x1 ���ɹ� 0x2 ��ʧ��
            meterBufPtr->MeterData[statusSize + 1] = 0x0; // ��Ŵ˰����ݵ�״̬���Ƿ��Ѿ����͸�������
            meterBufPtr->MeterData[statusSize + 2] = DataFrmPtr->DataLen; // ������ݳ���
            //	�����·�����
            memcpy(&meterBufPtr->MeterData[statusSize + 3], DataFrmPtr->DataBuf, DataFrmPtr->DataLen);
            OpResult = OP_CmdRevokeSucceed;
	} else if( (0x1 == meterBufPtr->MeterData[statusSize + 1]) ||
        (0x0 == meterBufPtr->MeterData[statusSize + 1] && 0x0 == meterBufPtr->MeterData[statusSize + 2])){
        // �����·�����

        // ����ȡ���ķ������·�������������ݸ���
        meterBufPtr->MeterData[statusSize] = DataFrmPtr->Command; // ���������
        // ��Ŵ˰����ݵ�״̬���Ƿ��Ѿ����͸�������
        // 0x0:δ�·� 0x1 ���ɹ� 0x2 ��ʧ��
        meterBufPtr->MeterData[statusSize + 1] = 0x0; // ��Ŵ˰����ݵ�״̬���Ƿ��Ѿ����͸�������
        meterBufPtr->MeterData[statusSize + 2] = DataFrmPtr->DataLen; // ������ݳ���
        //	�����·�����
        memcpy(&meterBufPtr->MeterData[statusSize + 3], DataFrmPtr->DataBuf, DataFrmPtr->DataLen);
        OpResult = OP_Succeed;
    } else {
        // �·�ʧ�ܣ� eeprom ���ݲ���
        OpResult = OP_Failure;
    }

    //bit7�� 1 ���ظ������а����������洢����һ����������� = �� �������1 + ������1 + ����״̬1 + �����1 + ������n ����
    // 	     0 : ֻ�ظ��������һ���ֽڡ�
    if( 0x80 == (flag & 0x80)){
        DataFrmPtr->DataLen = msg[2] + 4;
        DataFrmPtr->DataBuf[0] = OpResult;
        memcpy(&DataFrmPtr->DataBuf[1], msg, DataFrmPtr->DataLen-1);
    } else {
        DataFrmPtr->DataLen = 1;
        DataFrmPtr->DataBuf[0] = OpResult;
    }

    //����״̬�仯�����̱��沢�ϱ�������
    meterBufPtr->Crc8MeterData = CalCrc8(meterBufPtr->MeterData, NODE_INFO_SIZE - sizeof(METER_DATA_SAVE_FORMAT));

    meterBufPtr->Property.DownloadPrio = SubNodes[nodeId].Property.DownloadPrio = HIGH;
    Eeprom_ReadWrite((uint8 *)meterBufPtr, nodeId * NODE_INFO_SIZE, NODE_INFO_SIZE, WRITE_ATTR);

    OSMemPut(SmallMemoryPtr, meterBufPtr);
    OSMemPut(SmallMemoryPtr, msg);
    return SUCCESS;
}


/************************************************************************************************
* Function Name: DataHandle_LockSensorStatusUp
* Decription   : ����������״̬��Ϣ�ϴ�������
* Input        : DataFrmPtr-ָ������֡��ָ��
* Output       : �ɹ���ʧ��
************************************************************************************************/
uint8 DataHandle_LockSensorStatusUp(DATA_FRAME_STRUCT *DataFrmPtr)
{
    uint16 nodeId;
    DATA_FRAME_STRUCT *sensorBufPtr = NULL;

    // ����Ƿ�������ڵ�
    nodeId = Data_FindNodeId(0, DataFrmPtr->Route[0]);
    if (NULL_U16_ID == nodeId) {
        return OP_Failure;
    }
    // ����������Ƿ���ȷ + ��������򳤶��Ƿ���ȷ
    if ( (DataFrmPtr->Command != Lock_Sensor_Status_Cmd) ||
        (DataFrmPtr->DataLen != LOCK_SENSOR_DATA_SIZE)) {
        return OP_Failure;
    }
    // ����ռ����ڱ������״̬����
    if ((void *)0 == (sensorBufPtr = OSMemGetOpt(LargeMemoryPtr, 10, TIME_DELAY_MS(50)))) {
        return OP_Failure;
    }
	memcpy(sensorBufPtr, DataFrmPtr, MEM_LARGE_BLOCK_LEN);

	sensorBufPtr->PortNo = Uart_Gprs;
	sensorBufPtr->DeviceType = Dev_Concentrator;
	memcpy(&sensorBufPtr->DataBuf[0], SubNodes[nodeId].LongAddr, LONG_ADDR_SIZE);
	memcpy(&sensorBufPtr->DataBuf[LONG_ADDR_SIZE], &DataFrmPtr->DataBuf[0], LOCK_SENSOR_DATA_SIZE);

	sensorBufPtr->DataLen = LONG_ADDR_SIZE + LOCK_SENSOR_DATA_SIZE;
	sensorBufPtr->RouteInfo.CurPos = 0;
	sensorBufPtr->RouteInfo.Level = 1;
	memcpy(sensorBufPtr->Route[0], Concentrator.LongAddr, LONG_ADDR_SIZE);
	sensorBufPtr->PkgProp = DataHandle_SetPkgProperty(XOR_OFF, NONE_ACK, CMD_PKG, DOWN_DIR);
	DataHandle_SetPkgPath(sensorBufPtr, REVERSED);
	DataHandle_CreateTxData(sensorBufPtr);

    return OP_Succeed;
}


/************************************************************************************************
* Function Name: DataHandle_ReadLockDataProc
* Decription   : ��ȡ�����������������һ������״̬��Ϣ
* Input        : DataFrmPtr-ָ������֡��ָ��
* Output       : ��
* Others       : ����:����ַ(8)
*                ������������:����״̬(1)+����ַ(8)+������������(N)
*                ����״̬(1)+����ַ(8)+0x72data(32) + 0x73data(3+28)
*                ����״̬(1)+����ַ(8)+0x72data(32) + 0x74data(3+21+7)
************************************************************************************************/
void DataHandle_ReadLockDataProc(DATA_FRAME_STRUCT *DataFrmPtr)
{
    COMMAND_TYPE cmd;
    uint8 i, dataLen, *dataBufPtr = NULL, locknum, locklen, statusSize;
    uint16 nodeId;
    METER_DATA_SAVE_FORMAT *meterBufPtr = NULL;

    cmd = DataFrmPtr->Command;
    dataLen = 0;
    nodeId = Data_FindNodeId(0, DataFrmPtr->DataBuf);
    for (i = LONG_ADDR_SIZE; i > 0; i--) {
        DataFrmPtr->DataBuf[i] = DataFrmPtr->DataBuf[i - 1];
    }
    dataBufPtr = DataFrmPtr->DataBuf + 1 + LONG_ADDR_SIZE;

    // û�д˽ڵ����������������뼯�����Ĺ���ģʽ��һ�»������ڴ�ʧ�ܵ����
    if (NULL_U16_ID == nodeId) {
        DataFrmPtr->DataBuf[0] = OP_ObjectNotExist;
    } else if ((void *)0 == (meterBufPtr = OSMemGetOpt(SmallMemoryPtr, 10, TIME_DELAY_MS(50)))) {
        DataFrmPtr->DataBuf[0] = OP_Failure;
    } else if (Read_Lock_Data_Cmd == cmd) {
        dataLen = LOCK_STATUS_DATA_SIZE + 3 + LOCK_GPRS_DATA_SIZE;
        Eeprom_ReadWrite((uint8 *)meterBufPtr, nodeId * NODE_INFO_SIZE, NODE_INFO_SIZE, READ_ATTR);
        if (0 != memcmp(SubNodes[nodeId].LongAddr, meterBufPtr->Address, LONG_ADDR_SIZE)) {
            Data_MeterDataInit(meterBufPtr, nodeId, NODE_INFO_SIZE - sizeof(METER_DATA_SAVE_FORMAT));
        }
        if (meterBufPtr->Crc8MeterData == CalCrc8(meterBufPtr->MeterData, NODE_INFO_SIZE - sizeof(METER_DATA_SAVE_FORMAT))) {
            DataFrmPtr->DataBuf[0] = OP_Succeed;
            memcpy(&DataFrmPtr->DataBuf[1], SubNodes[nodeId].LongAddr, LONG_ADDR_SIZE);
            locknum = meterBufPtr->RxLastDataNum - 1; // �����������������һ������������λ��
            if( 0 == locknum+1 ){
                // ���û�����ݣ�ȫ����ֵ0
                memset(&DataFrmPtr->DataBuf[1+LONG_ADDR_SIZE], 0, LOCK_STATUS_DATA_SIZE);
            }else {
                // ����һ������
                memcpy(&DataFrmPtr->DataBuf[1+LONG_ADDR_SIZE], &meterBufPtr->MeterData[1 + locknum * LOCK_STATUS_DATA_SIZE], LOCK_STATUS_DATA_SIZE);
            }
            statusSize = LOCK_STATUS_DATA_NUM * LOCK_STATUS_DATA_SIZE + 1;
            locklen = meterBufPtr->MeterData[statusSize + 2]; // �������·�����ĳ���
            if( (0 != locklen) &&
				( Lock_Status_Issued_Cmd == meterBufPtr->MeterData[statusSize] ||
				  Lock_Key_Updata_Cmd == meterBufPtr->MeterData[statusSize])){
                memcpy(&DataFrmPtr->DataBuf[1+LONG_ADDR_SIZE + LOCK_STATUS_DATA_SIZE], &meterBufPtr->MeterData[statusSize], 3+LOCK_GPRS_DATA_SIZE);
            } else {// �����ݻ����ݲ�����
            	memset(&DataFrmPtr->DataBuf[1+LONG_ADDR_SIZE + LOCK_STATUS_DATA_SIZE], 0, 3+LOCK_GPRS_DATA_SIZE);
            }
        } else {
            DataFrmPtr->DataBuf[0] = OP_ErrorMeterData;
            memset(dataBufPtr, 0, dataLen);
        }
        OSMemPut(SmallMemoryPtr, meterBufPtr);
    }

    DataFrmPtr->DataLen = 1 + LONG_ADDR_SIZE + dataLen;
}


/************************************************************************************************
* Function Name: DataHandle_ReadLockDataProc
* Decription   : ������ȡ�����������������һ������״̬��Ϣ
* Input        : DataFrmPtr-ָ������֡��ָ��
* Output       : ��
* Others       : ����:��ʼ�ڵ����(2)+��ȡ������(1)
*                ������������:
*	�ڵ�������(2)+���η��ص�����N(1)+N*[����״̬(1)+����ַ(8)+������������(M)]
*
************************************************************************************************/
void DataHandle_BatchReadLockDataProc(DATA_FRAME_STRUCT *DataFrmPtr)
{
    COMMAND_TYPE cmd;
    uint8 readCount, ackCount, dataLen, blockLen;
    uint8 *dataBufPtr = NULL, *opStatusPtr = NULL;
    uint16 nodeId, startId, totalNodes;
    METER_DATA_SAVE_FORMAT *meterBufPtr = NULL;
    uint8 locknum, statusSize, locklen;

    if ((void *)0 == (meterBufPtr = OSMemGetOpt(SmallMemoryPtr, 10, TIME_DELAY_MS(50)))) {
        DataFrmPtr->DataBuf[0] = OP_Failure;
        DataFrmPtr->DataLen = 1;
        return;
    }
    cmd = DataFrmPtr->Command;
    if ((Batch_Read_Lock_Data_Cmd != cmd)) {
        DataFrmPtr->DataBuf[0] = OP_NoFunction;
        DataFrmPtr->DataLen = 1;
        OSMemPut(SmallMemoryPtr, meterBufPtr);
        return;
    }
    // �������ݵĳ���
    dataLen = LOCK_STATUS_DATA_SIZE + 3 + LOCK_GPRS_DATA_SIZE;
    startId = ((uint16 *)DataFrmPtr->DataBuf)[0];
    readCount = DataFrmPtr->DataBuf[2];
    ackCount = 0;
    totalNodes = 0;
    dataBufPtr = DataFrmPtr->DataBuf + 3;
    blockLen = dataLen + 1 + LONG_ADDR_SIZE;
    for (nodeId = 0; nodeId < Concentrator.MaxNodeId; nodeId++) {
        if (0 == memcmp(SubNodes[nodeId].LongAddr, NullAddress, LONG_ADDR_SIZE)) {
            continue;
        } else if ((SubNodes[nodeId].DevType & 0xF0) == 0xF0) {
            continue;
        } else {
            totalNodes++;
            if (totalNodes > startId && ackCount < readCount && dataBufPtr - DataFrmPtr->DataBuf + blockLen < GPRS_DATA_MAX_DATA) {
                ackCount++;
                opStatusPtr = dataBufPtr++;
                memcpy(dataBufPtr, SubNodes[nodeId].LongAddr, LONG_ADDR_SIZE);
                dataBufPtr += LONG_ADDR_SIZE;
                Eeprom_ReadWrite((uint8 *)meterBufPtr, nodeId * NODE_INFO_SIZE, NODE_INFO_SIZE, READ_ATTR);
                if (0 != memcmp(SubNodes[nodeId].LongAddr, meterBufPtr->Address, LONG_ADDR_SIZE)) {
                    Data_MeterDataInit(meterBufPtr, nodeId, NODE_INFO_SIZE - sizeof(METER_DATA_SAVE_FORMAT));
                }
                if (meterBufPtr->Crc8MeterData == CalCrc8(meterBufPtr->MeterData, NODE_INFO_SIZE - sizeof(METER_DATA_SAVE_FORMAT))) {
                    *opStatusPtr = OP_Succeed;

                    //memcpy(&dataBufPtr[1], SubNodes[nodeId].LongAddr, LONG_ADDR_SIZE);
                    locknum = meterBufPtr->RxLastDataNum - 1; // �����������������һ������������λ��
                    if( 0 == locknum+1 ){
                        // ���û�����ݣ�ȫ����ֵ0
                        memset(&dataBufPtr[0], 0, LOCK_STATUS_DATA_SIZE);
                    }else {
                        // ����һ������
                        memcpy(&dataBufPtr[0], &meterBufPtr->MeterData[1 + locknum * LOCK_STATUS_DATA_SIZE], LOCK_STATUS_DATA_SIZE);
                    }
                    statusSize = LOCK_STATUS_DATA_NUM * LOCK_STATUS_DATA_SIZE + 1;
                    locklen = meterBufPtr->MeterData[statusSize + 2]; // �������·�����ĳ���
                    if( (0 != locklen) &&
                        ( Lock_Status_Issued_Cmd == meterBufPtr->MeterData[statusSize] ||
                          Lock_Key_Updata_Cmd == meterBufPtr->MeterData[statusSize])){
                        memcpy(&dataBufPtr[LOCK_STATUS_DATA_SIZE], &meterBufPtr->MeterData[statusSize], 3+LOCK_GPRS_DATA_SIZE);
                    } else {// �����ݻ����ݲ�����
                        memset(&dataBufPtr[LOCK_STATUS_DATA_SIZE], 0, 3+LOCK_GPRS_DATA_SIZE);
                    }
                } else {
                    *opStatusPtr = OP_ErrorMeterData;
                    memset(dataBufPtr, 0, dataLen);
                }
                dataBufPtr += dataLen;
            }
        }
    }
    DataFrmPtr->DataBuf[0] = (uint8)totalNodes;
    DataFrmPtr->DataBuf[1] = (uint8)(totalNodes >> 8);
    DataFrmPtr->DataBuf[2] = ackCount;
    DataFrmPtr->DataLen = dataBufPtr - DataFrmPtr->DataBuf;
    OSMemPut(SmallMemoryPtr, meterBufPtr);
    return;
}

/************************************************************************************************
* Function Name: DataHandle_LockStatusReportProc
* Decription   : �����ϱ�״̬���ݴ�����
* Input        : DataFrmPtr-���յ�������ָ��
* Output       : �����Ƿ���ҪӦ����
* Others       : ���ڴ�������������״̬�ı�����
************************************************************************************************/
bool DataHandle_LockStatusReportProc(DATA_FRAME_STRUCT *DataFrmPtr)
{
	uint8 result;
	result = DataHandle_LockStatusDataSaveProc(DataFrmPtr);

	// ���ݲ��ԣ����ڵ����������Ӧ��ֻ���ա�
	if (CURRENT_VERSION == SRWF_CTP_TEST){
		OSMemPut(LargeMemoryPtr, DataFrmPtr);
		return NONE_ACK;
	}

    // �����ϱ�״̬���ݴ����������� + ��Ӧ���� + �ϱ�������
    // �ɹ��������ݳ��Ȳ�����Ӧ��
    // ����������ռ���������Ӧ��
    if (OP_Succeed == result) {
        // ������������
        DataFrmPtr->DeviceType = Dev_Concentrator;
        DataFrmPtr->DataBuf[0] = OP_Succeed;
        DataFrmPtr->DataLen = 1;
        DataFrmPtr->PkgProp = DataHandle_SetPkgProperty(XOR_OFF, NONE_ACK, ACK_PKG, DOWN_DIR);
        DataHandle_SetPkgPath(DataFrmPtr, REVERSED);
        DataHandle_CreateTxData(DataFrmPtr);
    } else if (OP_Failure == result){
        DataFrmPtr->DeviceType = Dev_Concentrator;
        DataFrmPtr->DataBuf[0] = OP_Failure;
        DataFrmPtr->DataLen = 1;
        DataFrmPtr->PkgProp = DataHandle_SetPkgProperty(XOR_OFF, NONE_ACK, ACK_PKG, DOWN_DIR);
        DataHandle_SetPkgPath(DataFrmPtr, REVERSED);
        DataHandle_CreateTxData(DataFrmPtr);
    } else {
		OSMemPut(LargeMemoryPtr, DataFrmPtr);
	}

    return NONE_ACK;
}


/************************************************************************************************
* Function Name: DataHandle_LockStatusIssuedProc
* Decription   : �������·����������������
* Input        : DataFrmPtr-���յ�������ָ��
* Output       : �����Ƿ���ҪӦ����
* Others       : ���ڴ�������������״̬�ı�����
************************************************************************************************/
bool DataHandle_LockStatusIssuedProc(DATA_FRAME_STRUCT *DataFrmPtr)
{
	// �������·�������������沢��Ӧ
    if (SUCCESS == DataHandle_LockGprsDataSaveProc(DataFrmPtr)) {
        // ������������
        DataFrmPtr->PortNo = Uart_Gprs;
        DataFrmPtr->DeviceType = Dev_Concentrator;
        DataFrmPtr->RouteInfo.CurPos = 0;
        DataFrmPtr->RouteInfo.Level = 1;
        memcpy(DataFrmPtr->Route[0], Concentrator.LongAddr, LONG_ADDR_SIZE);
        DataFrmPtr->PkgProp = DataHandle_SetPkgProperty(XOR_OFF, NONE_ACK, ACK_PKG, DOWN_DIR);
        DataHandle_SetPkgPath(DataFrmPtr, REVERSED);
        DataHandle_CreateTxData(DataFrmPtr);
    } else {
        // ������������
        DataFrmPtr->PortNo = Uart_Gprs;
        DataFrmPtr->DeviceType = Dev_Concentrator;
        DataFrmPtr->DataBuf[0] = OP_Failure;
        DataFrmPtr->DataLen = 1;
        DataFrmPtr->RouteInfo.CurPos = 0;
        DataFrmPtr->RouteInfo.Level = 1;
        memcpy(DataFrmPtr->Route[0], Concentrator.LongAddr, LONG_ADDR_SIZE);
        DataFrmPtr->PkgProp = DataHandle_SetPkgProperty(XOR_OFF, NONE_ACK, ACK_PKG, DOWN_DIR);
        DataHandle_SetPkgPath(DataFrmPtr, REVERSED);
        DataHandle_CreateTxData(DataFrmPtr);
    }
    return NONE_ACK;
}

/************************************************************************************************
* Function Name: DataHandle_LockKeyUpdataProc
* Decription   : �������·�����������Կ�������
* Input        : DataFrmPtr-���յ�������ָ��
* Output       : �����Ƿ���ҪӦ����
* Others       : ���ڴ����·�����������Կ�������
************************************************************************************************/
bool DataHandle_LockKeyUpdataProc(DATA_FRAME_STRUCT *DataFrmPtr)
{
	// �������·�������Կ��������沢��Ӧ
    if (SUCCESS == DataHandle_LockGprsDataSaveProc(DataFrmPtr)) {
        // ������������
        DataFrmPtr->PortNo = Uart_Gprs;
        DataFrmPtr->DeviceType = Dev_Concentrator;
        DataFrmPtr->RouteInfo.CurPos = 0;
        DataFrmPtr->RouteInfo.Level = 1;
        memcpy(DataFrmPtr->Route[0], Concentrator.LongAddr, LONG_ADDR_SIZE);
        DataFrmPtr->PkgProp = DataHandle_SetPkgProperty(XOR_OFF, NONE_ACK, ACK_PKG, DOWN_DIR);
        DataHandle_SetPkgPath(DataFrmPtr, REVERSED);
        DataHandle_CreateTxData(DataFrmPtr);
    } else {
        // ������������
        DataFrmPtr->PortNo = Uart_Gprs;
        DataFrmPtr->DeviceType = Dev_Concentrator;
        DataFrmPtr->DataBuf[0] = OP_Failure;
        DataFrmPtr->DataLen = 1;
        DataFrmPtr->RouteInfo.CurPos = 0;
        DataFrmPtr->RouteInfo.Level = 1;
        memcpy(DataFrmPtr->Route[0], Concentrator.LongAddr, LONG_ADDR_SIZE);
        DataFrmPtr->PkgProp = DataHandle_SetPkgProperty(XOR_OFF, NONE_ACK, ACK_PKG, DOWN_DIR);
        DataHandle_SetPkgPath(DataFrmPtr, REVERSED);
        DataHandle_CreateTxData(DataFrmPtr);
    }
    return NONE_ACK;
}

/************************************************************************************************
* Function Name: DataHandle_LockSensorStatusProc
* Decription   : ����������״̬��Ϣ������
* Input        : DataFrmPtr-���յ�������ָ��
************************************************************************************************/
bool DataHandle_LockSensorStatusProc(DATA_FRAME_STRUCT *DataFrmPtr)
{
	uint8 result;

	result = DataHandle_LockSensorStatusUp(DataFrmPtr);

	// ����������״̬��Ϣֱ���ϴ�������
    if (OP_Succeed == result) {
		// ������������
		DataFrmPtr->DeviceType = Dev_Concentrator;
		DataFrmPtr->DataBuf[0] = OP_Succeed;
		DataFrmPtr->DataLen = 1;
		DataFrmPtr->PkgProp = DataHandle_SetPkgProperty(XOR_OFF, NONE_ACK, ACK_PKG, DOWN_DIR);
		DataHandle_SetPkgPath(DataFrmPtr, REVERSED);
		DataHandle_CreateTxData(DataFrmPtr);
	} else if(OP_Failure == result) {
		DataFrmPtr->DeviceType = Dev_Concentrator;
		DataFrmPtr->DataBuf[0] = OP_Failure;
		DataFrmPtr->DataLen = 1;
		DataFrmPtr->PkgProp = DataHandle_SetPkgProperty(XOR_OFF, NONE_ACK, ACK_PKG, DOWN_DIR);
		DataHandle_SetPkgPath(DataFrmPtr, REVERSED);
		DataHandle_CreateTxData(DataFrmPtr);
	}

    return NONE_ACK;
}


/************************************************************************************************
* Function Name: DataHandle_RTCTimingTask
* Decription   : ʵʱʱ��Уʱ��������
* Input        : *p_arg-����ָ��
* Output       : ��
* Others       : ��
************************************************************************************************/
void DataHandle_RTCTimingTask(void *p_arg)
{
    uint8 err;
    DATA_HANDLE_TASK *taskPtr = NULL;
    DATA_FRAME_STRUCT *txDataFrmPtr = NULL, *rxDataFrmPtr = NULL;

    // ��������Уʱ���ݰ�
    TaskRunStatus.RTCTiming = TRUE;
    if ((void *)0 != (txDataFrmPtr = OSMemGetOpt(LargeMemoryPtr, 10, TIME_DELAY_MS(50)))) {
        txDataFrmPtr->PortNo = Uart_Gprs;
        txDataFrmPtr->PkgLength = DATA_FIXED_AREA_LENGTH;
        txDataFrmPtr->PkgSn = PkgNo++;
        txDataFrmPtr->Command = CONC_RTC_Timing;
        txDataFrmPtr->DeviceType = Dev_Concentrator;
        txDataFrmPtr->Life_Ack.Content = 0x0F;
        txDataFrmPtr->RouteInfo.CurPos = 0;
        txDataFrmPtr->RouteInfo.Level = 1;
        memcpy(txDataFrmPtr->Route[0], Concentrator.LongAddr, LONG_ADDR_SIZE);
        txDataFrmPtr->DataLen = 0;

        taskPtr = (DATA_HANDLE_TASK *)p_arg;
        taskPtr->Command = txDataFrmPtr->Command;
        taskPtr->NodeId = DATA_CENTER_ID;
        taskPtr->PkgSn = txDataFrmPtr->PkgSn;

        // �����������ݰ�
        txDataFrmPtr->PkgProp = DataHandle_SetPkgProperty(XOR_OFF, NEED_ACK, CMD_PKG, UP_DIR);
        DataHandle_SetPkgPath(txDataFrmPtr, UNREVERSED);
        DataHandle_CreateTxData(txDataFrmPtr);

        // �ȴ���������Ӧ��
        rxDataFrmPtr = OSMboxPend(taskPtr->Mbox, GPRS_WAIT_ACK_OVERTIME, &err);
        if ((void *)0 == rxDataFrmPtr) {
            RTCTimingTimer = 300;               // �����ʱ��5���Ӻ�����
        } else {
            if (SUCCESS == Rtc_Set(*(RTC_TIME *)(rxDataFrmPtr->DataBuf), Format_Bcd)) {
                RTCTimingTimer = RTCTIMING_INTERVAL_TIME;
            } else {
                RTCTimingTimer = 5;             // ���Уʱʧ����5�������
            }
            OSMemPut(LargeMemoryPtr, rxDataFrmPtr);
        }

    }

    // ���ٱ�����,�˴������Ƚ�ֹ�������,�����޷��ͷű�����ռ�õ��ڴ�ռ�
    OSMboxDel(taskPtr->Mbox, OS_DEL_ALWAYS, &err);
    OSSchedLock();
    OSTaskDel(OS_PRIO_SELF);
    OSMemPut(LargeMemoryPtr, taskPtr->StkPtr);
    taskPtr->StkPtr = (void *)0;
    TaskRunStatus.RTCTiming = FALSE;
    OSSchedUnlock();
}

/************************************************************************************************
* Function Name: DataHandle_RTCTimingProc
* Decription   : ������ʵʱʱ������Уʱ������
* Input        : ��
* Output       : ��
* Others       : ÿ��һ��ʱ�������һ��Уʱ����
************************************************************************************************/
void DataHandle_RTCTimingProc(void)
{
    uint8 err;
    DATA_HANDLE_TASK *taskPtr = NULL;

    // ���Gprs�Ƿ����߻��������Ƿ�����������
    if (FALSE == Gprs.Online || TRUE == TaskRunStatus.RTCTiming) {
        RTCTimingTimer = 60;
        return;
    }

    if ((void *)0 == (taskPtr = DataHandle_GetEmptyTaskPtr())) {
        return;
    }
    if ((void *)0 == (taskPtr->StkPtr = (OS_STK *)OSMemGetOpt(LargeMemoryPtr, 10, TIME_DELAY_MS(50)))) {
        return;
    }
    taskPtr->Mbox = OSMboxCreate((void *)0);
    taskPtr->Msg = (void *)0;
    if (OS_ERR_NONE != OSTaskCreate(DataHandle_RTCTimingTask, taskPtr,
        taskPtr->StkPtr + MEM_LARGE_BLOCK_LEN / sizeof(OS_STK) - 1, taskPtr->Prio)) {
        OSMemPut(LargeMemoryPtr, taskPtr->StkPtr);
        taskPtr->StkPtr = (void *)0;
        OSMboxDel(taskPtr->Mbox, OS_DEL_ALWAYS, &err);
    }
}


/************************************************************************************************
* Function Name: DataHandle_DataUploadTask
* Decription   : �����ϴ���������
* Input        : *p_arg-����ָ��
* Output       : ��
* Others       : ����Ҫ�ϴ������ݹ�һ��ʱ,����ʵʱ�ϴ�ʱ,��һ������ʱ,���������ϴ�����������
************************************************************************************************/
void DataHandle_DataUploadTask(void *p_arg)
{
    COMMAND_TYPE cmd;
    uint8 i, retry, err, count, dataLen, meterDataLen;
    uint16 nodeId, highPrioDataCount, rxMeterDataCount, uploadMaxCountOnePkg, *record = NULL;
    RTC_TIME rtcTime;
    DATA_HANDLE_TASK *taskPtr = NULL;
    DATA_FRAME_STRUCT *txDataFrmPtr = NULL, *rxDataFrmPtr = NULL;
    METER_DATA_SAVE_FORMAT *meterBufPtr = NULL;
	uint8 lockStatusNum;

    // ����δ�ϱ��Ľڵ������
    taskPtr = (DATA_HANDLE_TASK *)p_arg;
    TaskRunStatus.DataUpload = TRUE;
    meterBufPtr = (void *)0;
    record = (void *)0;
    Rtc_Get(&rtcTime, Format_Bcd);

    retry = 5;
    while (retry-- && FALSE == TaskRunStatus.DataForward) {
        DataUploadTimer = 60;
        highPrioDataCount = 0;
        rxMeterDataCount = 0;
        for (nodeId = 0; nodeId < Concentrator.MaxNodeId; nodeId++) {
            if (0 == memcmp(SubNodes[nodeId].LongAddr, NullAddress, LONG_ADDR_SIZE)) {
                continue;
            }
            if ((SubNodes[nodeId].DevType & 0xF0) == 0xF0) {
                continue;
            }
            if (TRUE == SubNodes[nodeId].Property.UploadData) {
                continue;
            }
            if (HIGH == SubNodes[nodeId].Property.UploadPrio) {
                highPrioDataCount++;
            }
            rxMeterDataCount++;
        }

        // ���û�������ϴ�������
        if (0 == highPrioDataCount && (0 == rxMeterDataCount || 0 == Concentrator.Param.DataUploadCtrl)) {
            break;
        }

        uploadMaxCountOnePkg = (GPRS_DATA_MAX_DATA - DATA_FIXED_AREA_LENGTH) / (LONG_ADDR_SIZE + LOCK_STATUS_DATA_SIZE);
        dataLen = LOCK_STATUS_DATA_SIZE;
        cmd = Lock_Status_Report_Cmd;

        meterDataLen = NODE_INFO_SIZE - sizeof(METER_DATA_SAVE_FORMAT);

        if ((void *)0 == meterBufPtr) {
            if ((void *)0 == (meterBufPtr = OSMemGetOpt(SmallMemoryPtr, 10, TIME_DELAY_MS(50)))) {
                break;
            }
        }
        if ((void *)0 == record) {
            if ((void *)0 == (record = OSMemGetOpt(SmallMemoryPtr, 10, TIME_DELAY_MS(50)))) {
                break;
            }
        }
        if ((void *)0 == (txDataFrmPtr = OSMemGetOpt(LargeMemoryPtr, 10, TIME_DELAY_MS(50)))) {
            break;
        }
        txDataFrmPtr->PortNo = Uart_Gprs;
        txDataFrmPtr->PkgLength = DATA_FIXED_AREA_LENGTH;
        txDataFrmPtr->PkgSn = PkgNo++;
        txDataFrmPtr->Command = cmd;
        txDataFrmPtr->DeviceType = Dev_Concentrator;
        txDataFrmPtr->Life_Ack.LifeCycle = 0x0F;
        txDataFrmPtr->Life_Ack.AckChannel = DEFAULT_RX_CHANNEL;
        txDataFrmPtr->RouteInfo.CurPos = 0;
        txDataFrmPtr->RouteInfo.Level = 1;
        memcpy(txDataFrmPtr->Route[0], Concentrator.LongAddr, LONG_ADDR_SIZE);
        txDataFrmPtr->DataLen = 1;
        count = 0;
        for (nodeId = 0; nodeId < Concentrator.MaxNodeId; nodeId++) {
            if (0 == memcmp(SubNodes[nodeId].LongAddr, NullAddress, LONG_ADDR_SIZE)) {
                continue;
            }
            if ((SubNodes[nodeId].DevType & 0xF0) == 0xF0) {
                continue;
            }
            if (TRUE == SubNodes[nodeId].Property.UploadData) {
                continue;
            }
            if ((highPrioDataCount > 0 && HIGH == SubNodes[nodeId].Property.UploadPrio) || 0 == highPrioDataCount) {
                Eeprom_ReadWrite((uint8 *)meterBufPtr, nodeId * NODE_INFO_SIZE, NODE_INFO_SIZE, READ_ATTR);
                uint8 crctest = CalCrc8(meterBufPtr->MeterData, NODE_INFO_SIZE - sizeof(METER_DATA_SAVE_FORMAT));
                if (0 != memcmp(meterBufPtr->Address, SubNodes[nodeId].LongAddr, LONG_ADDR_SIZE) ||
                    meterBufPtr->Crc8MeterData != crctest) {
                    Data_MeterDataInit(meterBufPtr, nodeId, meterDataLen);
                    Eeprom_ReadWrite((uint8 *)meterBufPtr, nodeId * NODE_INFO_SIZE, meterDataLen, WRITE_ATTR);
                    continue;
                } else {
                	lockStatusNum = meterBufPtr->MeterData[0];
                	while(lockStatusNum-- > 0){
	                    memcpy(txDataFrmPtr->DataBuf + txDataFrmPtr->DataLen, SubNodes[nodeId].LongAddr, LONG_ADDR_SIZE);
	                    txDataFrmPtr->DataLen += LONG_ADDR_SIZE;
	                    memcpy(txDataFrmPtr->DataBuf + txDataFrmPtr->DataLen, &meterBufPtr->MeterData[LOCK_STATUS_DATA_SIZE*lockStatusNum + 1], dataLen);
                            txDataFrmPtr->DataLen += dataLen;
	                    *(record + count++) = nodeId;
	                    if (count >= uploadMaxCountOnePkg) {
	                        break;
	                    }
                        }
                    if (count >= uploadMaxCountOnePkg) {
                        break;
                    }
                }
            }
        }

        if (0 == count) {
            OSMemPut(LargeMemoryPtr, txDataFrmPtr);
            break;
        }
#if PRINT_INFO
        uint8 test[10] = {0 };
        Gprs_OutputDebugMsg(0,"\n--�����ϴ���cmd+len+count = ");
        test[0] = cmd;
        test[1] = txDataFrmPtr->DataLen;
        test[2] = count;
        DebugOutputLength(&test[0], 3);
#endif
        txDataFrmPtr->DataBuf[0] = count;
        taskPtr->Command = txDataFrmPtr->Command;
        taskPtr->NodeId = DATA_CENTER_ID;
        taskPtr->PkgSn = txDataFrmPtr->PkgSn;

        // �����������ݰ�
        txDataFrmPtr->PkgProp = DataHandle_SetPkgProperty(XOR_OFF, NEED_ACK, CMD_PKG, UP_DIR);
        DataHandle_SetPkgPath(txDataFrmPtr, UNREVERSED);
        DataHandle_CreateTxData(txDataFrmPtr);

        // �ȴ���������Ӧ��
        rxDataFrmPtr = OSMboxPend(taskPtr->Mbox, GPRS_WAIT_ACK_OVERTIME, &err);
        if ((void *)0 != rxDataFrmPtr) {
            // �����Ǹ��ڵ㲢����״̬������
            if (1 == rxDataFrmPtr->DataLen && OP_Succeed == rxDataFrmPtr->DataBuf[0]) {
                retry = 5;
                for (i = 0; i < count; i++) {
                    nodeId = *(record + i);
                    Eeprom_ReadWrite((uint8 *)meterBufPtr, nodeId * NODE_INFO_SIZE, NODE_INFO_SIZE, READ_ATTR);
                    if (0 == memcmp(meterBufPtr->Address, SubNodes[nodeId].LongAddr, LONG_ADDR_SIZE)) {
                        meterBufPtr->MeterData[0] -= 1;
                        if( 0 == meterBufPtr->MeterData[0] ){
                            meterBufPtr->Property.UploadData = SubNodes[nodeId].Property.UploadData = TRUE;
                            meterBufPtr->Property.UploadPrio = SubNodes[nodeId].Property.UploadPrio = LOW;
                        }
                        meterBufPtr->Crc8MeterData = CalCrc8(meterBufPtr->MeterData, NODE_INFO_SIZE - sizeof(METER_DATA_SAVE_FORMAT));
                        Eeprom_ReadWrite((uint8 *)meterBufPtr, nodeId * NODE_INFO_SIZE, NODE_INFO_SIZE, WRITE_ATTR);
                    }
                }
            }
            OSMemPut(LargeMemoryPtr, rxDataFrmPtr);
        }
    }

    // ���ٱ�����,�˴������Ƚ�ֹ�������,�����޷��ͷű�����ռ�õ��ڴ�ռ�
    if ((void *)0 != meterBufPtr) {
        OSMemPut(SmallMemoryPtr, meterBufPtr);
    }
    if ((void *)0 != record) {
        OSMemPut(SmallMemoryPtr, record);
    }
    DataUploadTimer = 0;
    OSMboxDel(taskPtr->Mbox, OS_DEL_ALWAYS, &err);
    OSSchedLock();
    OSTaskDel(OS_PRIO_SELF);
    OSMemPut(LargeMemoryPtr, taskPtr->StkPtr);
    taskPtr->StkPtr = (void *)0;
    TaskRunStatus.DataUpload = FALSE;
    OSSchedUnlock();
}


/************************************************************************************************
* Function Name: DataHandle_DataDownloadTask
* Decription   : �����·���������
* Input        : *p_arg-����ָ��
* Output       : ��
* Others       : ������Ҫ�·�������ʱ,���������·�����
************************************************************************************************/
void DataHandle_DataDownloadTask(void *p_arg)
{
    COMMAND_TYPE cmd;
    uint8 retry, err, dataLen, meterDataLen;
    uint16 nodeId;
    RTC_TIME rtcTime;
    DATA_HANDLE_TASK *taskPtr;
    DATA_FRAME_STRUCT *txDataFrmPtr = NULL, *rxDataFrmPtr = NULL, *gprsDataFrmPtr = NULL;
    METER_DATA_SAVE_FORMAT *meterBufPtr = NULL;
    uint8 statusSize, cmdStatus;

    // ����δ�ϱ��Ľڵ������
    taskPtr = (DATA_HANDLE_TASK *)p_arg;
    TaskRunStatus.DataDownload = TRUE;
    meterBufPtr = (void *)0;
    Rtc_Get(&rtcTime, Format_Bcd);

    retry = 1;
    while (retry-- && FALSE == TaskRunStatus.DataForward) {
        // ��ȡ�������·����ݵĵ����ڵ��
        nodeId = DataDownloadNodeId;

        if ((0 == memcmp(SubNodes[nodeId].LongAddr, NullAddress, LONG_ADDR_SIZE)) ||
            (LOW == SubNodes[nodeId].Property.DownloadPrio)) {
            break;
        }
        if ( (void *)0 == meterBufPtr){
            if ((void *)0 == (meterBufPtr = OSMemGetOpt(SmallMemoryPtr, 10, TIME_DELAY_MS(50)))) {
                break;
            }
        }
        if ((void *)0 == (txDataFrmPtr = OSMemGetOpt(LargeMemoryPtr, 10, TIME_DELAY_MS(50)))) {
            break;
        }
        if ( (void *)0 == gprsDataFrmPtr){
            if ((void *)0 == (gprsDataFrmPtr = OSMemGetOpt(LargeMemoryPtr, 10, TIME_DELAY_MS(50)))) {
                break;
            }
        }
        txDataFrmPtr->PortNo = Usart_Rf;
        txDataFrmPtr->PkgLength = DATA_FIXED_AREA_LENGTH;
        txDataFrmPtr->PkgSn = PkgNo++;
        txDataFrmPtr->DeviceType = Dev_Concentrator;
        txDataFrmPtr->Life_Ack.LifeCycle = 0x0F;
        txDataFrmPtr->Life_Ack.AckChannel = DEFAULT_RX_CHANNEL;
        txDataFrmPtr->RouteInfo.CurPos = 0;
        txDataFrmPtr->RouteInfo.Level = 2;
        memcpy(txDataFrmPtr->Route[0], Concentrator.LongAddr, LONG_ADDR_SIZE);
        memcpy(txDataFrmPtr->Route[1], SubNodes[nodeId].LongAddr, LONG_ADDR_SIZE);

        statusSize = LOCK_STATUS_DATA_NUM * LOCK_STATUS_DATA_SIZE + 1;
        meterDataLen = NODE_INFO_SIZE - sizeof(METER_DATA_SAVE_FORMAT);

        Eeprom_ReadWrite((uint8 *)meterBufPtr, nodeId * NODE_INFO_SIZE, NODE_INFO_SIZE, READ_ATTR);
        cmdStatus = meterBufPtr->MeterData[statusSize+1];
        if(0x1 == cmdStatus){
			OSMemPut(SmallMemoryPtr, meterBufPtr);
			OSMemPut(LargeMemoryPtr, gprsDataFrmPtr);
			OSMemPut(LargeMemoryPtr, txDataFrmPtr);
			break;
        }
        cmd = (COMMAND_TYPE)meterBufPtr->MeterData[statusSize];
        dataLen = meterBufPtr->MeterData[statusSize + 2];
		// ��ֹ������յ������һ���ϴ����ݳ����·�������
		if( (dataLen == 0x0) || (cmd == 0x0) ){
			OSMemPut(SmallMemoryPtr, meterBufPtr);
			OSMemPut(LargeMemoryPtr, gprsDataFrmPtr);
			OSMemPut(LargeMemoryPtr, txDataFrmPtr);
			break;
		}
        if (0 != memcmp(meterBufPtr->Address, SubNodes[nodeId].LongAddr, LONG_ADDR_SIZE) ||
            meterBufPtr->Crc8MeterData != CalCrc8(meterBufPtr->MeterData, NODE_INFO_SIZE - sizeof(METER_DATA_SAVE_FORMAT))) {
            Data_MeterDataInit(meterBufPtr, nodeId, meterDataLen);
            Eeprom_ReadWrite((uint8 *)meterBufPtr, nodeId * NODE_INFO_SIZE, meterDataLen, WRITE_ATTR);
            break;
        } else {
            memcpy(txDataFrmPtr->DataBuf, &meterBufPtr->MeterData[statusSize + 3], dataLen);
            txDataFrmPtr->DataLen = dataLen;
        }

#if PRINT_INFO
        uint8 test[10] = {0};
        Gprs_OutputDebugMsg(0,"\n--�����·���cmd+len+addr = ");
        test[0] = cmd;
        test[1] = txDataFrmPtr->DataLen;
		memcpy(&test[2], &SubNodes[nodeId].LongAddr[3], 5);
        DebugOutputLength(&test[0], 7);
#endif
        txDataFrmPtr->Command = cmd;
        taskPtr->Command = txDataFrmPtr->Command;
        taskPtr->NodeId = nodeId;
        taskPtr->PkgSn = txDataFrmPtr->PkgSn;

        memcpy(gprsDataFrmPtr, txDataFrmPtr, MEM_LARGE_BLOCK_LEN);
        // �����������ݰ�
        txDataFrmPtr->PkgProp = DataHandle_SetPkgProperty(XOR_OFF, NEED_ACK, CMD_PKG, UP_DIR);
        DataHandle_SetPkgPath(txDataFrmPtr, UNREVERSED);
        DataHandle_CreateTxData(txDataFrmPtr);

        // �ȴ���������Ӧ��
        rxDataFrmPtr = OSMboxPend(taskPtr->Mbox, GPRS_WAIT_ACK_OVERTIME, &err);
        if ((void *)0 != rxDataFrmPtr) {
            // �����Ǹ��ڵ㲢����״̬������
            if (1 == rxDataFrmPtr->DataLen && OP_Succeed == rxDataFrmPtr->DataBuf[0]) {
                retry = 0;
                Eeprom_ReadWrite((uint8 *)meterBufPtr, nodeId * NODE_INFO_SIZE, NODE_INFO_SIZE, READ_ATTR);
                // 0x0:δ�·��� 0x1 ���·��ɹ� ��0x2 ���·�ʧ��
                meterBufPtr->MeterData[statusSize + 1] = 0x1;
                meterBufPtr->Property.DownloadPrio = SubNodes[nodeId].Property.DownloadPrio = LOW;
                meterBufPtr->Crc8MeterData = CalCrc8(meterBufPtr->MeterData, NODE_INFO_SIZE - sizeof(METER_DATA_SAVE_FORMAT));
                Eeprom_ReadWrite((uint8 *)meterBufPtr, nodeId * NODE_INFO_SIZE, NODE_INFO_SIZE, WRITE_ATTR);
				if( Lock_Key_Updata_Cmd == gprsDataFrmPtr->Command){
					gprsDataFrmPtr->Command = Lock_Key_Updata_Status_Cmd;
	                // �����·��ɹ����ϴ���������
	                gprsDataFrmPtr->PortNo = Uart_Gprs;
	                gprsDataFrmPtr->PkgProp = DataHandle_SetPkgProperty(XOR_OFF, NONE_ACK, CMD_PKG, UP_DIR);
	                DataHandle_SetPkgPath(gprsDataFrmPtr, UNREVERSED);
	                DataHandle_CreateTxData(gprsDataFrmPtr);
				}else{
					OSMemPut(LargeMemoryPtr, gprsDataFrmPtr);
				}
            } else {
				OSMemPut(LargeMemoryPtr, gprsDataFrmPtr);
			}
            OSMemPut(LargeMemoryPtr, rxDataFrmPtr);
        } else{
            retry = 0;
            Eeprom_ReadWrite((uint8 *)meterBufPtr, nodeId * NODE_INFO_SIZE, NODE_INFO_SIZE, READ_ATTR);
            // 0x0:δ�·��� 0x1 ���·��ɹ� ��0x2 ���·�ʧ��
            meterBufPtr->MeterData[statusSize + 1] = 0x2;
            meterBufPtr->Crc8MeterData = CalCrc8(meterBufPtr->MeterData, NODE_INFO_SIZE - sizeof(METER_DATA_SAVE_FORMAT));
            Eeprom_ReadWrite((uint8 *)meterBufPtr, nodeId * NODE_INFO_SIZE, NODE_INFO_SIZE, WRITE_ATTR);
			OSMemPut(LargeMemoryPtr, gprsDataFrmPtr);
		}
    }

    // ���ٱ�����,�˴������Ƚ�ֹ�������,�����޷��ͷű�����ռ�õ��ڴ�ռ�
    if ((void *)0 != meterBufPtr) {
        OSMemPut(SmallMemoryPtr, meterBufPtr);
    }
    DataDownloadTimer = 0;
    DataDownloadNodeId = 0xABCD;
    OSMboxDel(taskPtr->Mbox, OS_DEL_ALWAYS, &err);
    OSSchedLock();
    OSTaskDel(OS_PRIO_SELF);
    OSMemPut(LargeMemoryPtr, taskPtr->StkPtr);
    taskPtr->StkPtr = (void *)0;
    TaskRunStatus.DataDownload = FALSE;
    OSSchedUnlock();
}


/************************************************************************************************
* Function Name: DataHandle_DataUploadProc
* Decription   : �����ϴ�������������
* Input        : ��
* Output       : ��
* Others       : �ж��Ƿ���������Ҫ�ϴ��������ϴ�����
************************************************************************************************/
void DataHandle_DataUploadProc(void)
{
    uint8 err;
    DATA_HANDLE_TASK *taskPtr = NULL;

    DataUploadTimer = 60;

    // Gprs��������,�����ϴ�����û������
    if (FALSE == Gprs.Online || TRUE == TaskRunStatus.DataUpload || TRUE == TaskRunStatus.DataForward) {
        return;
    }
    // ����δ��ռ�õĿռ�,���������ϴ�����
    if ((void *)0 == (taskPtr = DataHandle_GetEmptyTaskPtr())) {
        return;
    }
    if ((void *)0 == (taskPtr->StkPtr = (OS_STK *)OSMemGetOpt(LargeMemoryPtr, 10, TIME_DELAY_MS(50)))) {
        return;
    }
    taskPtr->Mbox = OSMboxCreate((void *)0);
    taskPtr->Msg = (void *)0;
    if (OS_ERR_NONE != OSTaskCreate(DataHandle_DataUploadTask, taskPtr,
        taskPtr->StkPtr + MEM_LARGE_BLOCK_LEN / sizeof(OS_STK) - 1, taskPtr->Prio)) {
        OSMemPut(LargeMemoryPtr, taskPtr->StkPtr);
        taskPtr->StkPtr = (void *)0;
        OSMboxDel(taskPtr->Mbox, OS_DEL_ALWAYS, &err);
    }
}

/************************************************************************************************
* Function Name: DataHandle_DataDownloadProc
* Decription   : �����·�����������
* Input        : ��
* Output       : ��
* Others       : �ж��Ƿ���������Ҫ�·��������·�����
************************************************************************************************/
void DataHandle_DataDownloadProc(void)
{
    uint8 err;
    DATA_HANDLE_TASK *taskPtr = NULL;

    // �·�����û������
    if (TRUE == TaskRunStatus.DataDownload || TRUE == TaskRunStatus.DataForward) {
        return;
    }
    // ����δ��ռ�õĿռ�,���������ϴ�����
    if ((void *)0 == (taskPtr = DataHandle_GetEmptyTaskPtr())) {
        return;
    }
    if ((void *)0 == (taskPtr->StkPtr = (OS_STK *)OSMemGetOpt(LargeMemoryPtr, 10, TIME_DELAY_MS(50)))) {
        return;
    }
    taskPtr->Mbox = OSMboxCreate((void *)0);
    taskPtr->Msg = (void *)0;
    if (OS_ERR_NONE != OSTaskCreate(DataHandle_DataDownloadTask, taskPtr,
        taskPtr->StkPtr + MEM_LARGE_BLOCK_LEN / sizeof(OS_STK) - 1, taskPtr->Prio)) {
        OSMemPut(LargeMemoryPtr, taskPtr->StkPtr);
        taskPtr->StkPtr = (void *)0;
        OSMboxDel(taskPtr->Mbox, OS_DEL_ALWAYS, &err);
    }
}

/************************************************************************************************
* Function Name: DataHandle_ResetHostProc
* Decription   : ��λ����ģ��
************************************************************************************************/
bool DataHandle_ResetHostProc(void)
{
    uint8 err;
    PORT_BUF_FORMAT *txPortBufPtr = NULL;

    // ������һ���ڴ������м����ݴ���
    if ((void *)0 == (txPortBufPtr = OSMemGetOpt(LargeMemoryPtr, 10, TIME_DELAY_MS(50)))) {
        return ERROR;
    }
    txPortBufPtr->Property.PortNo = Usart_Rf;
    txPortBufPtr->Property.FilterDone = 1;
	txPortBufPtr->Length = 0;

    txPortBufPtr->Buffer[txPortBufPtr->Length++] = 0x55;
    txPortBufPtr->Buffer[txPortBufPtr->Length++] = 0xAA;
    txPortBufPtr->Buffer[txPortBufPtr->Length++] = 0x0A;
    txPortBufPtr->Buffer[txPortBufPtr->Length++] = 0x17;
    txPortBufPtr->Buffer[txPortBufPtr->Length++] = 0x00;
    txPortBufPtr->Buffer[txPortBufPtr->Length++] = 0x01;
    txPortBufPtr->Buffer[txPortBufPtr->Length++] = 0x03;
    txPortBufPtr->Buffer[txPortBufPtr->Length++] = 0xBB;
    txPortBufPtr->Buffer[txPortBufPtr->Length++] = 0x54;
    txPortBufPtr->Buffer[txPortBufPtr->Length++] = TAILBYTE;

	if (txPortBufPtr->Property.PortNo < Port_Total &&
		OS_ERR_NONE != OSMboxPost(SerialPort.Port[txPortBufPtr->Property.PortNo].MboxTx, txPortBufPtr)) {
		OSMemPut(LargeMemoryPtr, txPortBufPtr);
        return ERROR;
	} else {
		OSFlagPost(GlobalEventFlag, (OS_FLAGS)(1 << txPortBufPtr->Property.PortNo + SERIALPORT_TX_FLAG_OFFSET), OS_FLAG_SET, &err);
        return SUCCESS;
	}

}

//�����ڿ����л� RF ���ڲ�����
static void Uart_RF_Config(PORT_NO UartNo,
                         PARITY_TYPE Parity,
                         DATABITS_TYPE DataBits,
                         STOPBITS_TYPE StopBits,
                         uint32 Baudrate)
{
    USART_InitTypeDef USART_InitStruct;
    GPIO_InitTypeDef GPIO_InitStruct;
    NVIC_InitTypeDef NVIC_InitStruct;

    switch (UartNo) {
        case Usart_Rf:
            RCC_APB1PeriphClockCmd(RCC_APB1Periph_USART3, ENABLE);
            RCC_APB2PeriphClockCmd(RCC_APB2Periph_AFIO | RCC_APB2Periph_GPIOB, ENABLE); // ʹ��Usart3ʱ��

            GPIO_InitStruct.GPIO_Pin = GPIO_Pin_10;                                     // USART3_Tx PB10
            GPIO_InitStruct.GPIO_Speed = GPIO_Speed_50MHz;
            GPIO_InitStruct.GPIO_Mode =  GPIO_Mode_AF_PP;                               // GPIO_Mode_AF_PP ����������� �޸�Ϊ���ÿ�©���
            GPIO_Init(GPIOB, &GPIO_InitStruct);
            GPIO_InitStruct.GPIO_Pin = GPIO_Pin_11;                                     // USART3_Rx PB11
            GPIO_InitStruct.GPIO_Mode = GPIO_Mode_IN_FLOATING;                          // ��ͨ����ģʽ(����)
            GPIO_Init(GPIOB, &GPIO_InitStruct);

            USART_InitStruct.USART_BaudRate = Baudrate;                                 // ������
            USART_InitStruct.USART_WordLength = DataBits;                               // λ��
            USART_InitStruct.USART_StopBits = StopBits;                                 // ֹͣλ��
            USART_InitStruct.USART_Parity = Parity;                                     // ��żУ��
            USART_InitStruct.USART_Mode = USART_Mode_Rx | USART_Mode_Tx;
            USART_InitStruct.USART_HardwareFlowControl = USART_HardwareFlowControl_None;// ������Ϊ��
            USART_Init(USART3, &USART_InitStruct);                                      // ���ô��ڲ���

            NVIC_InitStruct.NVIC_IRQChannel = USART3_IRQn;                              // Usart3�ж�����
            NVIC_InitStruct.NVIC_IRQChannelPreemptionPriority = 1;
            NVIC_InitStruct.NVIC_IRQChannelSubPriority = 1;
            NVIC_InitStruct.NVIC_IRQChannelCmd = ENABLE;
            NVIC_Init(&NVIC_InitStruct);

            USART_Cmd(USART3, ENABLE);
            USART_ITConfig(USART3, USART_IT_RXNE, ENABLE);                              // ��ʹ�ܽ����ж�
            USART_ITConfig(USART3, USART_IT_TXE, DISABLE);                              // �Ƚ�ֹ�����ж�,����ʹ�ܷ����жϣ�����ܻ��ȷ�00
            break;
        default:
            break;
    }

    // Uart��������
    // Uart_ParameterInit(UartNo, Baudrate);
}

/************************************************************************************************
* Function Name: DataHandle_Updata_HostTask
* Decription   : ��������ģ�鴦������
************************************************************************************************/
void DataHandle_Updata_HostTask(void *p_arg)
{
    uint8 err;
    DATA_HANDLE_TASK *taskPtr = NULL;
	uint8 *rxDataFrmPtr = NULL;
    uint16 crc16, pkgCodeLen, dataCrc;
    uint32 writeAddr, codeLength;
    uint8 buf[13] = {0};
	uint8 upHostError = 0;

    // ����δ�ϱ��Ľڵ������
    taskPtr = (DATA_HANDLE_TASK *)p_arg;
    TaskRunStatus.DataUpHost = TRUE;

	PORT_BUF_FORMAT *txPortBufPtr = NULL;


    DataUpHostTimer = 3600;

    // ������Ϣ�����ʽ: Crc16(2)+�����ļ�����λ��(4)+���������ܳ���(4)+Crc16(2)
    memcpy(buf, (uint8 *)FLASH_MODULE_UPGRADE_INFO_START, 12);
    if (((uint16 *)(&buf[10]))[0] != CalCrc16(buf, 10))
    {
        goto UPHOST_ERROR;
    }

    // ��ȡ����
    crc16 = ((uint16 *)buf)[0]; // ���������У����
    writeAddr = 0;
    codeLength = ((uint32 *)(&buf[6]))[0]; // ���������ܳ���
    pkgCodeLen = 1000;
	if( 0 == codeLength ){
        goto UPHOST_ERROR;
	}

	while( upHostError < 10){

		// ����һ���ڴ������м����ݴ���
		if ((void *)0 == (txPortBufPtr = OSMemGetOpt(LargeMemoryPtr, 10, TIME_DELAY_MS(50)))) {
			goto UPHOST_ERROR;
		}

// 0x55AA + ���ļ�Crc16(2)+���������ܳ���(4)+�����ļ�����λ��(4)+�������볤��(2)+������������(N)+ �˰�����Crc16(2)
		pkgCodeLen = (codeLength - writeAddr > 1000) ? 1000 : (codeLength - writeAddr);// �����ĳ���
		txPortBufPtr->Property.PortNo = Usart_Rf;
		txPortBufPtr->Property.FilterDone = 1;
		txPortBufPtr->Length = 0;
		txPortBufPtr->Buffer[txPortBufPtr->Length++] = 0x55;
		txPortBufPtr->Buffer[txPortBufPtr->Length++] = 0xAA;
		txPortBufPtr->Buffer[txPortBufPtr->Length++] = (uint8)crc16;
		txPortBufPtr->Buffer[txPortBufPtr->Length++] = (uint8)(crc16 >> 8);
		txPortBufPtr->Buffer[txPortBufPtr->Length++] = (uint8)codeLength;
		txPortBufPtr->Buffer[txPortBufPtr->Length++] = (uint8)(codeLength >> 8);
		txPortBufPtr->Buffer[txPortBufPtr->Length++] = (uint8)(codeLength >> 16);
		txPortBufPtr->Buffer[txPortBufPtr->Length++] = (uint8)(codeLength >> 24);
		txPortBufPtr->Buffer[txPortBufPtr->Length++] = (uint8)writeAddr;
		txPortBufPtr->Buffer[txPortBufPtr->Length++] = (uint8)(writeAddr >> 8);
		txPortBufPtr->Buffer[txPortBufPtr->Length++] = (uint8)(writeAddr >> 16);
		txPortBufPtr->Buffer[txPortBufPtr->Length++] = (uint8)(writeAddr >> 24);
		txPortBufPtr->Buffer[txPortBufPtr->Length++] = (uint8)pkgCodeLen;
		txPortBufPtr->Buffer[txPortBufPtr->Length++] = (uint8)(pkgCodeLen >> 8);
		memcpy(&txPortBufPtr->Buffer[txPortBufPtr->Length],
			(uint8 *)(FLASH_MODULE_UPGRADECODE_START_ADDR + writeAddr),
			pkgCodeLen);
		txPortBufPtr->Length += pkgCodeLen;
		dataCrc = CalCrc16((uint8 *)(&txPortBufPtr->Buffer[2]), txPortBufPtr->Length-2);	 // Crc16У��
		txPortBufPtr->Buffer[txPortBufPtr->Length++] = (uint8)((dataCrc)&0xFF);
		txPortBufPtr->Buffer[txPortBufPtr->Length++] = (uint8)((dataCrc >> 8)&0xFF);

        taskPtr->PkgSn = txPortBufPtr->Buffer[2];
        taskPtr->Command = txPortBufPtr->Buffer[3];
        taskPtr->PortNo = Usart_Rf;

		// ��һ��������Ҫ�Ƚ�������λ���� boot ģʽ���������ڲ������е� 115200
		if( 0 == writeAddr ){
			OS_ENTER_CRITICAL();
			Uart_RF_Config(Usart_Rf, Parity_Even, DataBits_9, StopBits_1, 9600); 			// RF���ڳ�ʼ��
			OS_EXIT_CRITICAL();
			DataHandle_ResetHostProc(); // ��λ������ʹ���� bootģʽ��
			OSTimeDlyHMSM(0, 0, 0, 300);
			OS_ENTER_CRITICAL();
			Uart_RF_Config(Usart_Rf, Parity_Even, DataBits_9, StopBits_1, 115200); 			// RF���ڳ�ʼ��
			OS_EXIT_CRITICAL();
		}

		if (txPortBufPtr->Property.PortNo < Port_Total &&
			OS_ERR_NONE != OSMboxPost(SerialPort.Port[txPortBufPtr->Property.PortNo].MboxTx, txPortBufPtr)) {
			OSMemPut(LargeMemoryPtr, txPortBufPtr);
		} else {
			OSFlagPost(GlobalEventFlag, (OS_FLAGS)(1 << txPortBufPtr->Property.PortNo + SERIALPORT_TX_FLAG_OFFSET), OS_FLAG_SET, &err);
		}

		// �ȴ���������Ӧ��
		rxDataFrmPtr = OSMboxPend(taskPtr->Mbox, TIME_DELAY_MS(1000), &err);
		if ((void *)0 != rxDataFrmPtr) {
			// �����Ǹ��ڵ㲢����״̬������
			if ( UpHost_Success == rxDataFrmPtr[14] ) {
				writeAddr += pkgCodeLen;
				upHostError = 0;
			} else if ( rxDataFrmPtr[14] > UpHost_Success) {
				upHostError++;
			}
			OSMemPut(LargeMemoryPtr, rxDataFrmPtr);

			// ������Ҫ�� flash ��ղ��ϱ�������"����ģ�������ɹ�����"
			if ( writeAddr >= codeLength ) {
				upHostError = 100;
				//Gprs_OutputDebugMsg(0,"\n-- �����ɹ� --\n");
				break;
			}
		}else {
			upHostError++;
		}
	}


UPHOST_ERROR:

	// �������л���������ģʽ��
	OS_ENTER_CRITICAL();
	Uart_RF_Config(Usart_Rf, Parity_Even, DataBits_9, StopBits_1, 9600);			// RF���ڳ�ʼ��
	OS_EXIT_CRITICAL();

    DataUpHostTimer = 0;
    OSMboxDel(taskPtr->Mbox, OS_DEL_ALWAYS, &err);
    OSSchedLock();
    OSTaskDel(OS_PRIO_SELF);
    OSMemPut(LargeMemoryPtr, taskPtr->StkPtr);
    taskPtr->StkPtr = (void *)0;
    TaskRunStatus.DataUpHost = FALSE;
    OSSchedUnlock();
}


/************************************************************************************************
* Function Name: DataHandle_Updata_HostProc
* Decription   : ��������ģ��
************************************************************************************************/
void DataHandle_Updata_HostProc(void)
{
    uint8 err;
    DATA_HANDLE_TASK *taskPtr = NULL;

    DataUpHostTimer = 60;

    // ȷ��û���ϴ�����û������
    if (TRUE == TaskRunStatus.DataUpload || TRUE == TaskRunStatus.DataForward ||
		TRUE == TaskRunStatus.DataDownload || TRUE == TaskRunStatus.DataReplenish ||
		TRUE == TaskRunStatus.DataUpHost ) {
        return;
    }
    // ����δ��ռ�õĿռ�,����������������
    if ((void *)0 == (taskPtr = DataHandle_GetEmptyTaskPtr())) {
        return;
    }
    if ((void *)0 == (taskPtr->StkPtr = (OS_STK *)OSMemGetOpt(LargeMemoryPtr, 10, TIME_DELAY_MS(50)))) {
        return;
    }
    taskPtr->Mbox = OSMboxCreate((void *)0);
    taskPtr->Msg = (void *)0;
    if (OS_ERR_NONE != OSTaskCreate(DataHandle_Updata_HostTask, taskPtr,
        taskPtr->StkPtr + MEM_LARGE_BLOCK_LEN / sizeof(OS_STK) - 1, taskPtr->Prio)) {
        OSMemPut(LargeMemoryPtr, taskPtr->StkPtr);
        taskPtr->StkPtr = (void *)0;
        OSMboxDel(taskPtr->Mbox, OS_DEL_ALWAYS, &err);
    }
}


/************************************************************************************************
* Function Name: Data_Module_SwUpdate
* Decription   : ����ģ���������뱣�沢������
* Input        : DataFrmPtr-ָ������֡��ָ��
* Output       : ��
* Others       : ����: Crc16(2)+д���ַ(4)+���������ܳ���(4)+�����������볤��(2)+��������(N)
*                ����: Crc16(2)+д���ַ(4)+�������(1)
************************************************************************************************/
void Data_Module_SwUpdate(DATA_FRAME_STRUCT *DataFrmPtr)
{
    uint16 crc16, pkgCodeLen;
    uint32 writeAddr, codeLength;
    uint8 *codeBufPtr = NULL, *dataBufPtr = NULL;
    uint8 buf[12];

    // ��ȡ����
    dataBufPtr = DataFrmPtr->DataBuf;
    crc16 = ((uint16 *)dataBufPtr)[0];
    writeAddr = ((uint32 *)(dataBufPtr + 2))[0];
    codeLength = ((uint32 *)(dataBufPtr + 6))[0];
    pkgCodeLen = ((uint16 *)(dataBufPtr + 10))[0];
    codeBufPtr = dataBufPtr + 12;

    // ����������볤�ȴ���
    if (codeLength > FLASH_MODULE_UPGRADECODE_SIZE * FLASH_PAGE_SIZE) {
        *(dataBufPtr + 6) = OP_ParameterError;
        DataFrmPtr->DataLen = 7;
        return;
    }

    // ����յ���д���ַΪ0,��ʾ��һ���µ�����Ҫ����
    if (0 == writeAddr) {
        Flash_Erase(FLASH_MODULE_UPGRADECODE_START_ADDR, FLASH_MODULE_UPGRADECODE_SIZE);
        Flash_Erase(FLASH_MODULE_UPGRADE_INFO_START, FLASH_MODULE_UPGRADE_INFO_SIZE);
        // ������Ϣ�����ʽ: Crc16(2)+�����ļ�����λ��(4)+���������ܳ���(4)+Crc16(2)
        memcpy(buf, dataBufPtr, sizeof(buf));
        ((uint32 *)(&buf[2]))[0] = 0; //FLASH_MODULE_UPGRADECODE_START_ADDR;
        ((uint16 *)(&buf[10]))[0] = CalCrc16(buf, 10);
        Flash_Write(buf, 16, FLASH_MODULE_UPGRADE_INFO_START);
    }

    // ��������У���ֽڻ����������ܳ��ȴ����򷵻ش���
    if (crc16 != ((uint16 *)FLASH_MODULE_UPGRADE_INFO_START)[0] ||
        codeLength != ((uint32 *)(FLASH_MODULE_UPGRADE_INFO_START + 6))[0]) {
        *(dataBufPtr + 6) = OP_ParameterError;
        DataFrmPtr->DataLen = 7;
        return;
    }

    // д����������
    if (codeLength >= writeAddr + pkgCodeLen) {
        if (0 != memcmp(codeBufPtr, (uint8 *)(FLASH_MODULE_UPGRADECODE_START_ADDR + writeAddr), pkgCodeLen)) {
            Flash_Write(codeBufPtr, pkgCodeLen, FLASH_MODULE_UPGRADECODE_START_ADDR + writeAddr);
            if (0 != memcmp(codeBufPtr, (uint8 *)(FLASH_MODULE_UPGRADECODE_START_ADDR + writeAddr), pkgCodeLen)) {
                *(dataBufPtr + 6) = OP_Failure;
                DataFrmPtr->DataLen = 7;
                return;
            }
        }
    } else {
        *(dataBufPtr + 6) = OP_ParameterError;
        DataFrmPtr->DataLen = 7;
        return;
    }

    // ����Ƿ������һ��
    if (writeAddr + pkgCodeLen >= codeLength) {
        if (crc16 == CalCrc16((uint8 *)FLASH_MODULE_UPGRADECODE_START_ADDR, codeLength)) {
            *(dataBufPtr + 6) = OP_Succeed;
			DataUpHostTimer = 3; // ��������ģ��
        } else {
            *(dataBufPtr + 6) = OP_Failure;
        }
    } else {
        *(dataBufPtr + 6) = OP_Succeed;
    }
    DataFrmPtr->DataLen = 7;
    return;
}

/************************************************************************************************
* Function Name: DataHandle_ReadHostChannelTask
* Decription   : ��ȡ����ģ���ŵ���������
* Input        : p_arg-����ԭ�����ݵ�ָ��
* Output       : ��
* Others       : �ú����ô���PC,����,�ֳֻ��������ݺ�ĵȴ�����
************************************************************************************************/
void DataHandle_ReadHostChannelTask(void *p_arg)
{
    uint8 err;
    DATA_HANDLE_TASK *taskPtr;
    DATA_FRAME_STRUCT *txDataFrmPtr;
	uint8 *rxDataFrmPtr;
	uint8 len = 0, dataLen;
	uint16 crc16;

    taskPtr = (DATA_HANDLE_TASK *)p_arg;
    txDataFrmPtr = (DATA_FRAME_STRUCT *)(taskPtr->Msg);

    TaskRunStatus.DataForward = TRUE;
    rxDataFrmPtr = OSMboxPend(taskPtr->Mbox, TIME_DELAY_MS(DELAYTIME_ONE_LAYER), &err);
    if ((void *)0 == rxDataFrmPtr) {
        txDataFrmPtr->DataBuf[0] = OP_Failure;
        txDataFrmPtr->DataLen = 1;
    } else {
    	len = 3;
		if(0x55 == rxDataFrmPtr[len] && 0xAA == rxDataFrmPtr[len+1]){
			dataLen = rxDataFrmPtr[len + 2];
			crc16 = CalCrc16((uint8 *)(&rxDataFrmPtr[len]), dataLen-3); 	// Crc16У��
			if( (uint8)((crc16)&0xFF) == rxDataFrmPtr[len+dataLen-3] &&
				(uint8)((crc16>>8)&0xFF) == rxDataFrmPtr[len+dataLen-2]){
				txDataFrmPtr->DataBuf[0] = OP_Succeed;
				txDataFrmPtr->DataBuf[1] = rxDataFrmPtr[len + 11];//ģ��Ƶ�Σ�2B (433 MHz)��2F (470MHz)��56 (868MHz)��5B (915MHz)
				txDataFrmPtr->DataBuf[2] = rxDataFrmPtr[len + 13];//�ŵ��ţ�����0F��ʾ��ģ����ŵ�Ϊ15
				txDataFrmPtr->DataLen = 3;
			}else{
				txDataFrmPtr->DataBuf[0] = OP_Failure;
				txDataFrmPtr->DataLen = 1;
			}
		}
        OSMemPut(LargeMemoryPtr, rxDataFrmPtr);
    }
	DataHandle_ResetHostProc();//��λ����ģ��

    // ����Ӧ�����ݰ�
    txDataFrmPtr->PkgProp = DataHandle_SetPkgProperty(XOR_OFF, NONE_ACK, ACK_PKG, UP_DIR);
    DataHandle_SetPkgPath(txDataFrmPtr, REVERSED);
    DataHandle_CreateTxData(txDataFrmPtr);

    // ���ٱ�����,�˴������Ƚ�ֹ�������,�����޷��ͷű�����ռ�õ��ڴ�ռ�
    OSMboxDel(taskPtr->Mbox, OS_DEL_ALWAYS, &err);
    OSSchedLock();
    OSTaskDel(OS_PRIO_SELF);
    OSMemPut(LargeMemoryPtr, taskPtr->StkPtr);
    taskPtr->StkPtr = (void *)0;
    TaskRunStatus.DataForward = FALSE;
    OSSchedUnlock();
}

/************************************************************************************************
* Function Name: DataHandle_ReadHostChannelProc
* Decription   : ��ȡ����ģ����ŵ�
* Input        : DataFrmPtr-���յ�������ָ��
* Output       : �Ƿ���Ҫ��������
* Others       : ����:��
*                ����:
************************************************************************************************/
bool DataHandle_ReadHostChannelProc(DATA_FRAME_STRUCT *DataFrmPtr)
{
    uint8 err;
    DATA_HANDLE_TASK *taskPtr;

    // �ڵ㲻���ڼ����������л��������ڴ�����
    if (TRUE == TaskRunStatus.DataForward) {
        if (NEED_ACK == DataFrmPtr->PkgProp.NeedAck) {
            DataFrmPtr->DataBuf[0] = OP_Failure;
            DataFrmPtr->DataLen = 1;
            return NEED_ACK;
        }
        OSMemPut(LargeMemoryPtr, DataFrmPtr);
        return NONE_ACK;
    }

    PORT_BUF_FORMAT *txPortBufPtr = NULL;

    // ������һ���ڴ������м����ݴ���
    if ((void *)0 == (txPortBufPtr = OSMemGetOpt(LargeMemoryPtr, 10, TIME_DELAY_MS(50)))) {
        OSMemPut(LargeMemoryPtr, DataFrmPtr);
        return ERROR;
    }
    txPortBufPtr->Property.PortNo = Usart_Rf;
    txPortBufPtr->Property.FilterDone = 1;
	txPortBufPtr->Length = 0;

    txPortBufPtr->Buffer[txPortBufPtr->Length++] = 0x55;
    txPortBufPtr->Buffer[txPortBufPtr->Length++] = 0xAA;
    txPortBufPtr->Buffer[txPortBufPtr->Length++] = 0x0C;
    txPortBufPtr->Buffer[txPortBufPtr->Length++] = 0x05;
    txPortBufPtr->Buffer[txPortBufPtr->Length++] = 0x00;
    txPortBufPtr->Buffer[txPortBufPtr->Length++] = 0x01;
    txPortBufPtr->Buffer[txPortBufPtr->Length++] = 0x00;
    txPortBufPtr->Buffer[txPortBufPtr->Length++] = 0x10;
    txPortBufPtr->Buffer[txPortBufPtr->Length++] = 0x0E;
    uint16 crc16 = CalCrc16((uint8 *)(&txPortBufPtr->Buffer[0]), txPortBufPtr->Length);     // Crc16У��
    txPortBufPtr->Buffer[txPortBufPtr->Length++] = (uint8)((crc16)&0xFF);
    txPortBufPtr->Buffer[txPortBufPtr->Length++] = (uint8)((crc16 >> 8)&0xFF);
    txPortBufPtr->Buffer[txPortBufPtr->Length++] = TAILBYTE;

    // �����ҪӦ��,�򴴽�Ӧ������
    if (NEED_ACK == DataFrmPtr->PkgProp.NeedAck) {
        if ((void *)0 == (taskPtr = DataHandle_GetEmptyTaskPtr())) {
            goto DATA_FORWARD_FAILED;
        }
        if ((void *)0 == (taskPtr->StkPtr = (OS_STK *)OSMemGetOpt(LargeMemoryPtr, 10, TIME_DELAY_MS(50)))) {
            goto DATA_FORWARD_FAILED;
        }
        taskPtr->PkgSn = txPortBufPtr->Buffer[2];
        taskPtr->Command = txPortBufPtr->Buffer[3];
        taskPtr->PortNo = Usart_Rf;
        taskPtr->Mbox = OSMboxCreate((void *)0);
        taskPtr->Msg = (uint8 *)DataFrmPtr;
        if (OS_ERR_NONE != OSTaskCreate(DataHandle_ReadHostChannelTask, taskPtr, taskPtr->StkPtr + MEM_LARGE_BLOCK_LEN / sizeof(OS_STK) - 1, taskPtr->Prio)) {
            OSMemPut(LargeMemoryPtr, taskPtr->StkPtr);
            taskPtr->StkPtr = (void *)0;
            OSMboxDel(taskPtr->Mbox, OS_DEL_ALWAYS, &err);
            goto DATA_FORWARD_FAILED;
        }
    }

	if (txPortBufPtr->Property.PortNo < Port_Total &&
		OS_ERR_NONE != OSMboxPost(SerialPort.Port[txPortBufPtr->Property.PortNo].MboxTx, txPortBufPtr)) {
		OSMemPut(LargeMemoryPtr, txPortBufPtr);
		goto DATA_FORWARD_FAILED;
	} else {
		OSFlagPost(GlobalEventFlag, (OS_FLAGS)(1 << txPortBufPtr->Property.PortNo + SERIALPORT_TX_FLAG_OFFSET), OS_FLAG_SET, &err);
	}

    return NONE_ACK;

DATA_FORWARD_FAILED:
    if (NEED_ACK == DataFrmPtr->PkgProp.NeedAck) {
        DataFrmPtr->DataBuf[0] = OP_Failure;
        DataFrmPtr->DataLen = 1;
        return NEED_ACK;
    }
    return NONE_ACK;
}

/************************************************************************************************
* Function Name: DataHandle_WriteHostChannelTask
* Decription   : д����ģ���ŵ���������
* Input        : p_arg-����ԭ�����ݵ�ָ��
* Output       : ��
* Others       : �ú����ô���PC,����,�ֳֻ��������ݺ�ĵȴ�����
************************************************************************************************/
void DataHandle_WriteHostChannelTask(void *p_arg)
{
    uint8 err;
    DATA_HANDLE_TASK *taskPtr;
    DATA_FRAME_STRUCT *txDataFrmPtr;
	uint8 *rxDataFrmPtr;
	uint8 len = 0, dataLen;
	uint16 crc16;

    taskPtr = (DATA_HANDLE_TASK *)p_arg;
    txDataFrmPtr = (DATA_FRAME_STRUCT *)(taskPtr->Msg);

    TaskRunStatus.DataForward = TRUE;
    rxDataFrmPtr = OSMboxPend(taskPtr->Mbox, TIME_DELAY_MS(DELAYTIME_ONE_LAYER), &err);
    if ((void *)0 == rxDataFrmPtr) {
        txDataFrmPtr->DataBuf[0] = OP_Failure;
        txDataFrmPtr->DataLen = 1;
    } else {
    	len = 3;
		if(0x55 == rxDataFrmPtr[len] && 0xAA == rxDataFrmPtr[len+1]){
			dataLen = rxDataFrmPtr[len + 2];
			crc16 = CalCrc16((uint8 *)(&rxDataFrmPtr[len]), dataLen-3); 	// Crc16У��
			if( (uint8)((crc16)&0xFF) == rxDataFrmPtr[len+dataLen-3] &&
				(uint8)((crc16>>8)&0xFF) == rxDataFrmPtr[len+dataLen-2]){
				txDataFrmPtr->DataBuf[0] = OP_Succeed;
				txDataFrmPtr->DataLen = 1;
			}else{
				txDataFrmPtr->DataBuf[0] = OP_Failure;
				txDataFrmPtr->DataLen = 1;
			}
		}
        OSMemPut(LargeMemoryPtr, rxDataFrmPtr);
    }
	DataHandle_ResetHostProc();//��λ����ģ��

    // ����Ӧ�����ݰ�
    txDataFrmPtr->PkgProp = DataHandle_SetPkgProperty(XOR_OFF, NONE_ACK, ACK_PKG, UP_DIR);
    DataHandle_SetPkgPath(txDataFrmPtr, REVERSED);
    DataHandle_CreateTxData(txDataFrmPtr);

    // ���ٱ�����,�˴������Ƚ�ֹ�������,�����޷��ͷű�����ռ�õ��ڴ�ռ�
    OSMboxDel(taskPtr->Mbox, OS_DEL_ALWAYS, &err);
    OSSchedLock();
    OSTaskDel(OS_PRIO_SELF);
    OSMemPut(LargeMemoryPtr, taskPtr->StkPtr);
    taskPtr->StkPtr = (void *)0;
    TaskRunStatus.DataForward = FALSE;
    OSSchedUnlock();
}

/************************************************************************************************
* Function Name: DataHandle_WriteHostChannelProc
* Decription   : д����ģ����ŵ�
* Input        : DataFrmPtr-���յ�������ָ��
* Output       : �Ƿ���Ҫ��������
* Others       : ����:��
*                ����:
************************************************************************************************/
bool DataHandle_WriteHostChannelProc(DATA_FRAME_STRUCT *DataFrmPtr)
{
    uint8 err;
    DATA_HANDLE_TASK *taskPtr;

    // �ڵ㲻���ڼ����������л��������ڴ�����
    if (TRUE == TaskRunStatus.DataForward) {
        if (NEED_ACK == DataFrmPtr->PkgProp.NeedAck) {
            DataFrmPtr->DataBuf[0] = OP_Failure;
            DataFrmPtr->DataLen = 1;
            return NEED_ACK;
        }
        OSMemPut(LargeMemoryPtr, DataFrmPtr);
        return NONE_ACK;
    }

    PORT_BUF_FORMAT *txPortBufPtr = NULL;

    // ������һ���ڴ������м����ݴ���
    if ((void *)0 == (txPortBufPtr = OSMemGetOpt(LargeMemoryPtr, 10, TIME_DELAY_MS(50)))) {
        OSMemPut(LargeMemoryPtr, DataFrmPtr);
        return ERROR;
    }
    txPortBufPtr->Property.PortNo = Usart_Rf;
    txPortBufPtr->Property.FilterDone = 1;
	txPortBufPtr->Length = 0;

    txPortBufPtr->Buffer[txPortBufPtr->Length++] = 0x55;
    txPortBufPtr->Buffer[txPortBufPtr->Length++] = 0xAA;
    txPortBufPtr->Buffer[txPortBufPtr->Length++] = 0x0E;
    txPortBufPtr->Buffer[txPortBufPtr->Length++] = 0x05;
    txPortBufPtr->Buffer[txPortBufPtr->Length++] = 0x01;
    txPortBufPtr->Buffer[txPortBufPtr->Length++] = 0x01;
    txPortBufPtr->Buffer[txPortBufPtr->Length++] = 0x06;
    txPortBufPtr->Buffer[txPortBufPtr->Length++] = 0x10;
    txPortBufPtr->Buffer[txPortBufPtr->Length++] = 0x02;
    txPortBufPtr->Buffer[txPortBufPtr->Length++] = 0x00;
	txPortBufPtr->Buffer[txPortBufPtr->Length++] = DataFrmPtr->DataBuf[0];
    uint16 crc16 = CalCrc16((uint8 *)(&txPortBufPtr->Buffer[0]), txPortBufPtr->Length);     // Crc16У��
    txPortBufPtr->Buffer[txPortBufPtr->Length++] = (uint8)((crc16)&0xFF);
    txPortBufPtr->Buffer[txPortBufPtr->Length++] = (uint8)((crc16 >> 8)&0xFF);
    txPortBufPtr->Buffer[txPortBufPtr->Length++] = TAILBYTE;

    // �����ҪӦ��,�򴴽�Ӧ������
    if (NEED_ACK == DataFrmPtr->PkgProp.NeedAck) {
        if ((void *)0 == (taskPtr = DataHandle_GetEmptyTaskPtr())) {
            goto DATA_FORWARD_FAILED;
        }
        if ((void *)0 == (taskPtr->StkPtr = (OS_STK *)OSMemGetOpt(LargeMemoryPtr, 10, TIME_DELAY_MS(50)))) {
            goto DATA_FORWARD_FAILED;
        }
        taskPtr->PkgSn = txPortBufPtr->Buffer[2];
        taskPtr->Command = txPortBufPtr->Buffer[3];
        taskPtr->PortNo = Usart_Rf;
        taskPtr->Mbox = OSMboxCreate((void *)0);
        taskPtr->Msg = (uint8 *)DataFrmPtr;
        if (OS_ERR_NONE != OSTaskCreate(DataHandle_WriteHostChannelTask, taskPtr, taskPtr->StkPtr + MEM_LARGE_BLOCK_LEN / sizeof(OS_STK) - 1, taskPtr->Prio)) {
            OSMemPut(LargeMemoryPtr, taskPtr->StkPtr);
            taskPtr->StkPtr = (void *)0;
            OSMboxDel(taskPtr->Mbox, OS_DEL_ALWAYS, &err);
            goto DATA_FORWARD_FAILED;
        }
    }

	if (txPortBufPtr->Property.PortNo < Port_Total &&
		OS_ERR_NONE != OSMboxPost(SerialPort.Port[txPortBufPtr->Property.PortNo].MboxTx, txPortBufPtr)) {
		OSMemPut(LargeMemoryPtr, txPortBufPtr);
		goto DATA_FORWARD_FAILED;
	} else {
		OSFlagPost(GlobalEventFlag, (OS_FLAGS)(1 << txPortBufPtr->Property.PortNo + SERIALPORT_TX_FLAG_OFFSET), OS_FLAG_SET, &err);
	}


    return NONE_ACK;

DATA_FORWARD_FAILED:
    if (NEED_ACK == DataFrmPtr->PkgProp.NeedAck) {
        DataFrmPtr->DataBuf[0] = OP_Failure;
        DataFrmPtr->DataLen = 1;
        return NEED_ACK;
    }
    return NONE_ACK;
}


/************************************************************************************************
* Function Name: DataHandle_RxCmdProc
* Decription   : ���ݴ�������,ֻ������յ��������¼�
* Input        : DataFrmPtr-����֡��ָ��
* Output       : ��
* Others       : �ú����������Ա�˻��������PC�����ֳֻ����͹�����ָ�����ָ����Ӧ��
************************************************************************************************/
void DataHandle_RxCmdProc(DATA_FRAME_STRUCT *DataFrmPtr)
{
    bool postHandle, reversePath;
    PKG_PROPERTY ackPkgProperty;

    postHandle = DataFrmPtr->PkgProp.NeedAck;
    reversePath = REVERSED;
    ackPkgProperty = DataHandle_SetPkgProperty(XOR_OFF, NONE_ACK, ACK_PKG, UP_DIR);
    switch (DataFrmPtr->Command) {

        // ���������汾��Ϣ 0x40
        case Read_CONC_Version:
            // ����:��������
            // ����:����汾(2)+Ӳ���汾(2)+Э��汾(2)
            DataFrmPtr->DataLen = 0;
            DataFrmPtr->DataBuf[DataFrmPtr->DataLen++] = (uint8)(SW_VERSION >> 8);
            DataFrmPtr->DataBuf[DataFrmPtr->DataLen++] = (uint8)SW_VERSION;
            DataFrmPtr->DataBuf[DataFrmPtr->DataLen++] = (uint8)(HW_VERSION >> 8);
            DataFrmPtr->DataBuf[DataFrmPtr->DataLen++] = (uint8)HW_VERSION;
            DataFrmPtr->DataBuf[DataFrmPtr->DataLen++] = (uint8)(PT_VERSION >> 8);
            DataFrmPtr->DataBuf[DataFrmPtr->DataLen++] = (uint8)PT_VERSION;
            break;

        // ��������ID 0x41
        case Read_CONC_ID:
            // ����:��������
            // ����:������ID��BCD��(6)
            memcpy(DataFrmPtr->DataBuf, Concentrator.LongAddr, LONG_ADDR_SIZE);
            DataFrmPtr->DataLen = LONG_ADDR_SIZE;
            break;

        // д������ID 0x42
        case Write_CONC_ID:
            Data_SetConcentratorAddr(DataFrmPtr);
            break;

        // ��������ʱ�� 0x43
        case Read_CONC_RTC:
            // ����:��������
            // ����:������ʱ��(7)
            Rtc_Get((RTC_TIME *)DataFrmPtr->DataBuf, Format_Bcd);
            DataFrmPtr->DataLen = 7;
            break;

        // д������ʱ�� 0x44
        case Write_CONC_RTC:
            // ����:������ʱ��(7)
            // ����:����״̬(1)
            if (SUCCESS == Rtc_Set(*(RTC_TIME *)(DataFrmPtr->DataBuf), Format_Bcd)) {
                DataFrmPtr->DataBuf[0] = OP_Succeed;
                RTCTimingTimer = RTCTIMING_INTERVAL_TIME;
            } else {
                DataFrmPtr->DataBuf[0] = OP_TimeAbnormal;
            }
            DataFrmPtr->DataLen = 1;
            break;

        // ��Gprs���� 0x45
        case Read_GPRS_Param:
            Data_GprsParameter(DataFrmPtr);
            break;

        // дGprs���� 0x46
        case Write_GPRS_Param:
            Data_GprsParameter(DataFrmPtr);
            break;

        // ��Gprs�ź�ǿ�� 0x47
        case Read_GPRS_RSSI:
            // ����:��
            // ����:�ź�ǿ��
            DataFrmPtr->DataBuf[0] = Gprs_GetCSQ();
            DataFrmPtr->DataBuf[1] = Gprs.Online ? 0x01 : 0x00;
            DataFrmPtr->DataLen = 2;
            DataFrmPtr->DataLen += Gprs_GetIMSI(&DataFrmPtr->DataBuf[DataFrmPtr->DataLen]);
            DataFrmPtr->DataLen += Gprs_GetGMM(&DataFrmPtr->DataBuf[DataFrmPtr->DataLen]);
            break;

        // ��������ʼ�� 0x48
        case Initial_CONC_Cmd:
            // ����:�������
            // ����:�������+����״̬
            DataFrmPtr->DataBuf[1] = OP_Succeed;
            if (0 == DataFrmPtr->DataBuf[0]) {
                Data_ClearDatabase();
            } else if(1 == DataFrmPtr->DataBuf[0]) {
                Data_ClearMeterData();
            } else {
                DataFrmPtr->DataBuf[1] = OP_Failure;
            }
            DataFrmPtr->DataLen = 2;
            break;

        // �������������� 0x4C
        case Restart_CONC_Cmd:
            // ����:��
            // ����:����״̬
            DataFrmPtr->DataBuf[0] = OP_Succeed;
            DataFrmPtr->DataLen = 1;
            DevResetTimer = 5000;
            break;

        // ��ߵ����������� 0x50
        case Read_Meter_Total_Number:
            Data_ReadNodesCount(DataFrmPtr);
            break;

        // ��ȡ��ߵ�����Ϣ 0x51
        case Read_Meters_Doc_Info:
            Data_ReadNodes(DataFrmPtr);
            break;

        // д���ߵ�����Ϣ 0x52
        case Write_Meters_Doc_Info:
            Data_WriteNodes(DataFrmPtr);
            break;

        // ɾ����ߵ�����Ϣ 0x53
        case Delete_Meters_Doc_Info:
            Data_DeleteNodes(DataFrmPtr);
            break;

        // �޸ı�ߵ�����Ϣ 0x54
        case Modify_Meter_Doc_Info:
            Data_ModifyNodes(DataFrmPtr);
            break;

	    // ����״̬ 0x72
        case Lock_Status_Report_Cmd:
            postHandle = DataHandle_LockStatusReportProc(DataFrmPtr);
            break;

        // �������·������������� 0x73
        case Lock_Status_Issued_Cmd:
            postHandle = DataHandle_LockStatusIssuedProc(DataFrmPtr);
            break;

        // �������·�����������Կ���� 0x74
        case Lock_Key_Updata_Cmd:
            postHandle = DataHandle_LockKeyUpdataProc(DataFrmPtr);
            break;

        // ����������״̬��Ϣ���ϴ������� 0x75
        case Lock_Sensor_Status_Cmd:
            postHandle = DataHandle_LockSensorStatusProc(DataFrmPtr);
            break;

        // ��ȡ�����������������һ������״̬��Ϣ 0x77
        case Read_Lock_Data_Cmd:
            DataHandle_ReadLockDataProc(DataFrmPtr);
            break;

        // ������ȡ�����������������һ������״̬��Ϣ 0x78
        case Batch_Read_Lock_Data_Cmd:
            DataHandle_BatchReadLockDataProc(DataFrmPtr);
            break;

		// ���������Ĺ������� 0x79
		case Lock_Read_CONC_Work_Param:
			Data_RdWrConcentratorParam(DataFrmPtr);
			break;

		// д�������Ĺ������� 0x7A
		case Lock_Write_CONC_Work_Param:
			Data_RdWrConcentratorParam(DataFrmPtr);
			break;

		// �����������ŵ� 0x7B
		case Lock_Read_Host_Channel_Param:
            postHandle = DataHandle_ReadHostChannelProc(DataFrmPtr);
			break;

		// д���������ŵ� 0x7C
		case Lock_Write_Host_Channel_Param:
            postHandle = DataHandle_WriteHostChannelProc(DataFrmPtr);
			break;

        // �������������� 0xF1
        case Software_Update_Cmd:
            Data_SwUpdate(DataFrmPtr);
            break;

        // Eeprom��� 0xF3
        case Eeprom_Check_Cmd:
            Data_EepromCheckProc(DataFrmPtr);
            break;

		// ����ģ������ 0xF4
		case Module_Software_Update_Cmd:
			Data_Module_SwUpdate(DataFrmPtr);
			break;

        // ����ָ�֧��
        default:
#if PRINT_INFO
            Gprs_OutputDebugMsg(TRUE, "--��ָ���ݲ�֧��--\n");
#endif
            postHandle = NONE_ACK;
            OSMemPut(LargeMemoryPtr, DataFrmPtr);
            break;
    }

    if (NEED_ACK == postHandle) {
        DataFrmPtr->PkgProp = ackPkgProperty;
        DataFrmPtr->DeviceType = Dev_Concentrator;
        DataHandle_SetPkgPath(DataFrmPtr, reversePath);
        DataHandle_CreateTxData(DataFrmPtr);
    }
}

/************************************************************************************************
* Function Name: DataHandle_RxAckProc
* Decription   : ���ݴ�������,ֻ������յ���Ӧ���¼�
* Input        : DataBufPtr-��������ָ��
* Output       : ��
* Others       : �ú����������Ա�˻��������PC�����ֳֻ����͹�����Ӧ��
************************************************************************************************/
void DataHandle_RxAckProc(DATA_FRAME_STRUCT *DataFrmPtr)
{
    uint8 i;
    uint16 nodeId;
    DATA_HANDLE_TASK *taskPtr = NULL;

    // ����Ӧ��ڵ��Ƿ��ڱ�������
    nodeId = Data_FindNodeId(0, DataFrmPtr->Route[0]);

    // �жϸ�Ӧ��֡Ӧ�ô��ݸ�˭
    for (i = 0; i < MAX_DATA_HANDLE_TASK_NUM; i++) {
        taskPtr = &DataHandle_TaskArry[i];
        if ((void *)0 != taskPtr->StkPtr &&
            taskPtr->NodeId == nodeId &&
            taskPtr->Command == DataFrmPtr->Command &&
            taskPtr->PkgSn == DataFrmPtr->PkgSn) {
            if (OS_ERR_NONE != OSMboxPost(taskPtr->Mbox, DataFrmPtr)) {
                OSMemPut(LargeMemoryPtr, DataFrmPtr);
            }
            return;
        }
    }
    OSMemPut(LargeMemoryPtr, DataFrmPtr);
}

/************************************************************************************************
* Function Name: DataHandle_PassProc
* Decription   : ͸����������
* Input        : DataFrmPtr-���յ�������ָ��
* Output       : TRUE-�Ѿ�����,FALSE-û�д���
* Others       : Ŀ���ַ�����Լ�ʱ,���ݵ���һ���ڵ�
************************************************************************************************/
bool DataHandle_PassProc(DATA_FRAME_STRUCT *DataFrmPtr)
{
    static uint8 lastInPort = End_Port;

    // �����Ŀ��ڵ���������һ������
    if (DataFrmPtr->RouteInfo.CurPos == DataFrmPtr->RouteInfo.Level - 1) {
        return FALSE;
    }
    // ������������ǵ����ڶ��������豸����ѡ��ͨѶ�˿�,�����������RF�˿�
    if (UP_DIR == DataFrmPtr->PkgProp.Direction &&
        DataFrmPtr->RouteInfo.CurPos == DataFrmPtr->RouteInfo.Level - 2) {
        DataFrmPtr->PortNo = lastInPort;
        DataFrmPtr->Life_Ack.AckChannel = DEFAULT_TX_CHANNEL;
    } else {
        lastInPort = DataFrmPtr->PortNo;
        DataFrmPtr->Life_Ack.AckChannel = DEFAULT_RX_CHANNEL;
        DataFrmPtr->PortNo = Usart_Rf;
    }
    DataHandle_CreateTxData(DataFrmPtr);
    return TRUE;
}

/************************************************************************************************
* Function Name: DataHandle_WriteNodes
* Decription   : ���ݿ�д�ڵ���Ϣ
* Input        : DataFrmPtr-������һ����ʱ��������
************************************************************************************************/
uint8 DataHandle_WriteNodes(DATA_FRAME_STRUCT *DataFrmPtr)
{
    uint8 err;
    uint16 i;

    // ��ڵ��б������ӽڵ�
    if (NULL_U16_ID != Data_FindNodeId(0, DataFrmPtr->Route[0])) {
        return 0;
    } else {
        if (Concentrator.MaxNodeId >= MAX_NODE_NUM) {
            return 0;
        } else {
            // Ѱ�ҿ�λ��
            for (i = 0; i < Concentrator.MaxNodeId; i++) {
                if (0 == memcmp(SubNodes[i].LongAddr, NullAddress, LONG_ADDR_SIZE)) {
                    break;
                }
            }
            if (i >= Concentrator.MaxNodeId) {
                Concentrator.MaxNodeId++;
            }
            memcpy(SubNodes[i].LongAddr, DataFrmPtr->Route[0], LONG_ADDR_SIZE);
            SubNodes[i].DevType = (DEVICE_TYPE)DataFrmPtr->DeviceType;
            SubNodes[i].Property.LastResult = 2;
            SubNodes[i].Property.UploadData = TRUE;
            SubNodes[i].Property.UploadPrio = LOW;
            SubNodes[i].RxLastDataNum = 0;
            SubNodes[i].RxChannel = DEFAULT_TX_CHANNEL;
        }
    }

    // ��ʱ�󱣴�,������ʱͬ����ʱ��,������������ݲ�һ�µ����
    OSFlagPost(GlobalEventFlag, (OS_FLAGS)FLAG_DELAY_SAVE_TIMER, OS_FLAG_SET, &err);

    return 1;
}

/************************************************************************************************
* Function Name: DataHandle_Task
* Decription   : ���ݴ�������,ֻ��������¼�
* Input        : *p_arg-����ָ��
* Output       : ��
* Others       : ��
************************************************************************************************/
void DataHandle_Task(void *p_arg)
{
    uint8 i, err, *dat = NULL;
    OS_FLAGS eventFlag;
    DATA_FRAME_STRUCT *dataFrmPtr = NULL;
    EXTRACT_DATA_RESULT ret;
	DATA_HANDLE_TASK *taskPtr = NULL;

    // ��ʼ������
    (void)p_arg;
    PkgNo = CalCrc8(Concentrator.LongAddr, LONG_ADDR_SIZE);
    for (i = 0; i < MAX_DATA_HANDLE_TASK_NUM; i++) {
        DataHandle_TaskArry[i].Prio = TASK_DATAHANDLE_DATA_PRIO + i;
        DataHandle_TaskArry[i].StkPtr = (void *)0;
    }
    TaskRunStatus.DataForward = FALSE;
    TaskRunStatus.DataReplenish = FALSE;
    TaskRunStatus.DataUpload = FALSE;
    TaskRunStatus.RTCService = FALSE;
    TaskRunStatus.RTCTiming = FALSE;

    // ���ݳ�ʼ��
    Data_Init();
    if(Concentrator.Param.DataNodeSave > 0x1){
        Concentrator.Param.DataNodeSave = 0x1;
    }

    while (TRUE) {
        // ��ȡ�������¼�����
        eventFlag = OSFlagPend(GlobalEventFlag, (OS_FLAGS)DATAHANDLE_EVENT_FILTER, (OS_FLAG_WAIT_SET_ANY | OS_FLAG_CONSUME), TIME_DELAY_MS(5000), &err);

        // ������Щ����
        while (eventFlag != (OS_FLAGS)0) {
            dat = (void *)0;
            if (eventFlag & FLAG_USART_RF_RX) {
                // Rfģ���յ�������
                dat = OSMboxAccept(SerialPort.Port[Usart_Rf].MboxRx);
                eventFlag &= ~FLAG_USART_RF_RX;
            } else if (eventFlag & FLAG_GPRS_RX) {
                // Gprsģ���յ�������
                dat = OSMboxAccept(Gprs.MboxRx);
                eventFlag &= ~FLAG_GPRS_RX;
            } else if (eventFlag & FLAG_USB_RX) {
                // Usb�˿��յ�������
                dat = OSMboxAccept(SerialPort.Port[Usb_Port].MboxRx);
                eventFlag &= ~FLAG_USB_RX;
            } else if (eventFlag & FLAG_USART_DEBUG_RX) {
                // Debug�˿��յ�������
                dat = OSMboxAccept(SerialPort.Port[Usart_Debug].MboxRx);
                eventFlag &= ~FLAG_USART_DEBUG_RX;
            } else if (eventFlag & FLAG_UART_RS485_RX) {
                // 485�˿��յ�������
                dat = OSMboxAccept(SerialPort.Port[Uart_Rs485].MboxRx);
                eventFlag &= ~FLAG_UART_RS485_RX;
            } else if (eventFlag & FLAG_USART_IR_RX) {
                // Ir�˿��յ�������
                dat = OSMboxAccept(SerialPort.Port[Usart_Ir].MboxRx);
                eventFlag &= ~FLAG_USART_IR_RX;
            } else if (eventFlag & FLAG_DELAY_SAVE_TIMER) {
                // ������ʱ����
                eventFlag &= ~FLAG_DELAY_SAVE_TIMER;
                DataHandle_DataDelaySaveProc();
            } else if (eventFlag & FLAG_DATA_DOWNLOAD_TIMER) {
                // �����·�����
                eventFlag &= ~FLAG_DATA_DOWNLOAD_TIMER;
                DataHandle_DataDownloadProc();
            } else if (eventFlag & FLAG_DATA_UPLOAD_TIMER) {
                // �����ϴ�����
                eventFlag &= ~FLAG_DATA_UPLOAD_TIMER;
                DataHandle_DataUploadProc();
            } else if (eventFlag & FLAG_DATA_UPHOST_TIMER) {
                // ������������
                eventFlag &= ~FLAG_DATA_UPHOST_TIMER;
                DataHandle_Updata_HostProc();
            } else if (eventFlag & FLAG_RTC_TIMING_TIMER) {
                // ʱ������Уʱ����
                eventFlag &= ~FLAG_RTC_TIMING_TIMER;
                DataHandle_RTCTimingProc();
            }
            if ((void *)0 == dat) {
                continue;
            }

            // ��ԭ��������ȡ����
            if(Ok_Data != DataHandle_ExtractData(dat)){
                if(Error_DstAddress == ret){

                }

				// ��������ģ����ŵ�Ƶ�ʶ�ȡ������
				if( 0x55 == dat[3] && 0xAA == dat[4] ){
					// �жϸ�Ӧ��֡Ӧ�ô��ݸ�˭
					for (i = 0; i < MAX_DATA_HANDLE_TASK_NUM; i++) {
						taskPtr = &DataHandle_TaskArry[i];
						if ((void *)0 != taskPtr->StkPtr &&	taskPtr->Command == dat[6] &&
							(taskPtr->PkgSn == dat[5] || taskPtr->PkgSn+0xB == dat[5])) {
							if (OS_ERR_NONE != OSMboxPost(taskPtr->Mbox, dat)) {
								OSMemPut(LargeMemoryPtr, dat);
							}
							break;
						}
					}
					continue;
				}else{
					OSMemPut(LargeMemoryPtr, dat);
					continue;
				}
            }

            dataFrmPtr = (DATA_FRAME_STRUCT *)dat;

            if( Concentrator.Param.DataNodeSave ){
                // �յ���ȷ�ı�����ݺ������������û�е�����ֱ�ӱ��浵����Ϣ�ͱ���Ϣ����������
                DataHandle_WriteNodes(dataFrmPtr);
            } else {
              // ����ڵ㲻�ڵ����У����еĹ���ģʽΪ"�ֶ����ýڵ㵽����"
              // ���ڵ����� 0x72 0x75 ������Ӧ��
                if( (NULL_U16_ID == Data_FindNodeId(0, dataFrmPtr->Route[0])) &&
                    (Lock_Status_Report_Cmd == dataFrmPtr->Command ||
                     Lock_Sensor_Status_Cmd == dataFrmPtr->Command) ){
                    OSMemPut(LargeMemoryPtr, dat);
                    continue;
                }
            }

            // ȷ�������Ϣ�ϴ���ͨ��
            if (Usart_Debug == dataFrmPtr->PortNo || Usb_Port == dataFrmPtr->PortNo) {
                MonitorPort = (PORT_NO)(dataFrmPtr->PortNo);
            }

            // ���Ŀ���ַ�����Լ���ת��
            if (TRUE == DataHandle_PassProc(dataFrmPtr)) {
                continue;
            }

            // �ֱ�������֡��Ӧ��ָ֡��
            if (CMD_PKG == dataFrmPtr->PkgProp.PkgType) {
                // ���������֡
                DataHandle_RxCmdProc(dataFrmPtr);
            } else {
                // �����Ӧ��֡
                DataHandle_RxAckProc(dataFrmPtr);
            }
        }

        OSTimeDlyHMSM(0, 0, 0, 50);
    }
}

/***************************************End of file*********************************************/


