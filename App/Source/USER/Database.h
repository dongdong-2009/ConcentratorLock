/************************************************************************************************
*                                   SRWF-6009
*    (c) Copyright 2015, Software Department, Sunray Technology Co.Ltd
*                               All Rights Reserved
*
* FileName     : Database.h
* Description  :
* Version      :
* Function List:
*------------------------------Revision History--------------------------------------------------
* No.   Version     Date            Revised By      Item            Description
* 1     V1.1        08/11/2015      Zhangxp         SRWF-6009       Original Version
************************************************************************************************/

#ifndef  DATABASE_H
#define  DATABASE_H

#ifdef   DATABASE_GLOBALS
#define  DATABASE_EXT
#else
#define  DATABASE_EXT  extern
#endif

/************************************************************************************************
*                               Pubilc Macro Define Section
************************************************************************************************/
#define NULL_U32_ID                             0xFFFFFFFF  // 32λ��Ч����
#define NULL_U16_ID                             0xFFFF      // 16λ��Ч����
#define NULL_U12_ID                             0xFFF       // 12λ��Ч����
#define NULL_U10_ID                             0x3FF       // 10λ��Ч����
#define NULL_U8_ID                              0xFF        // 8λ��Ч����
#define NULL_U4_ID                              0xF         // 4λ��Ч����

#define MAX_NODE_NUM                            512        // �ڵ㵵������
#define MAX_NEIGHBOUR_NUM                       3           // ����ھ���
#define MAX_CUSTOM_ROUTE_LEVEL                  5           // �Զ���·�������ļ���
#define MAX_CUSTOM_ROUTES                       2           // ÿ���ڵ����ɶ����·����

#define DATA_CENTER_ID                          2048        // ���Ľڵ�ID���
#define DATA_SAVE_DELAY_TIMER                   3           // ���ݱ�����ʱʱ��(��)

#define UPDOWN_RSSI_SIZE                        2           // �����г�ǿ���С
#define REALTIME_DATA_AREA_SIZE                 27          // ��ʱ������������(��5���ֽڵ�ʱ��������г�ǿ)
#define FREEZE_DATA_AREA_SIZE                   115         // ������������(�������г�ǿ)


#define NODE_INFO_SIZE                          256 // 128    // ÿ���ڵ���Ϣռ�ݵĴ洢����С
#define LOCK_STATUS_DATA_SIZE                   32          // ����״̬����(�������г�ǿ)
#define LOCK_STATUS_DATA_NUM                    6           // ����״̬��������

#define LOCK_GPRS_DATA_SIZE                   	28          // �������·����������������ݳ���
#define LOCK_KEY_DATA_SIZE                   	21          // �������·�����������Կ�������ݳ���

#define LOCK_SENSOR_DATA_SIZE                   5           // ����������״̬��Ϣ

/************************************************************************************************
*                                   Enum Define Section
************************************************************************************************/
// �������������Ͷ���
typedef enum {
    RealTimeDataMode = 0,                                   // ��ʱ��������ģʽ
    FreezeDataMode,                                         // �������ݹ���ģʽ
} WORK_TYPE;

// �豸����
typedef enum {
    Dev_AllMeter = 0x00,                                    // ȫ��������

	Dev_LOCK = 0x44,										// ����
	Dev_Server = 0xFA,										// ������
    Dev_Concentrator = 0xFC,                                // ������

	Dev_Empty = 0xFF										// ������
} DEVICE_TYPE;

/************************************************************************************************
*                                   Union Define Section
************************************************************************************************/

/************************************************************************************************
*                                  Struct Define Section
************************************************************************************************/
// GPRSģ�����
typedef struct {
    uint8 PriorDscIp[4];                                    // ��ѡ��������IP��ַ
    uint16 PriorDscPort;                                    // ��ѡ�������Ķ˿ں�
    uint8 BackupDscIp[4];                                   // ���÷�������IP��ַ
    uint16 BackupDscPort;                                   // ���÷������Ķ˿ں�
    char Apn[12 + 1];                                       // ���ӵ�APN,���һ��Ϊ0
    char Username[12 + 1];                                  // ����APN���û���,���һ��Ϊ0
    char Password[12 + 1];                                  // ����APN������,���һ��Ϊ0
    uint8 HeatBeatPeriod;                                   // ���������,��λΪ10��
} GPRS_PARAMETER;

// ��������������
typedef struct {
    WORK_TYPE WorkType;                                     // ��������������
    uint8 DataReplenishCtrl: 1;                             // ���ݲ�������λ:1Ϊ��,0Ϊ�ر�
    uint8 DataUploadCtrl: 1;                                // �����ϴ�����λ:1Ϊ��,0Ϊ�ر�
    uint8 DataUploadMode: 1;                                // �����ϴ����Ϳ���λ:1Ϊ��������,0Ϊ��ʱ��������
    uint8 DataEncryptCtrl: 1;                               // ���ݼ��ܿ���:1Ϊ����,0Ϊ������
    uint8 DataNodeSave;                                     // �ڵ㱣�����: 1 Ϊ���棬 0Ϊ������
    uint8 DataReplenishDay[4];                              // ���ݲ���������
    uint8 DataReplenishHour;                                // ���ݲ�����ʱ��:BCD��ʽ
    uint8 DataReplenishCount;                               // ���ݲ���ʧ��ʱ�ظ������Ĵ���
} WORK_PARAM_STRUCT;

// �Զ���·����Ϣ(��С����Ϊż��)
typedef struct {
    uint16 AddrCrc16;                                       // �ڵ㳤��ַ��CRCֵ
    uint16 RouteNode[MAX_CUSTOM_ROUTES][MAX_CUSTOM_ROUTE_LEVEL];    // �м̽ڵ�
} CUSTOM_ROUTE_INFO;

// �ڵ����Զ���
typedef struct {
    uint8 LastResult: 2;                                    // ���һ�γ�����:0-ʧ��,1-�ɹ�,����-δ֪
    uint8 CurRouteNo: 2;                                    // ��ǰ·����,����ֵ����CUST_ROUTES_PART��ʱ��ʹ�õ����Զ���·��
    uint8 UploadData: 1;                                    // �ڵ��ϴ�������
    uint8 UploadPrio: 1;                                    // �˽ڵ���ϴ����ȼ���
    uint8 DownloadPrio: 1;                                  // �˽ڵ���·����ȼ���
} SUBNODE_PROPERETY;

// �ڵ���Ϣ����(��С����Ϊż��)
typedef struct {
    uint8 LongAddr[LONG_ADDR_SIZE];                         // �ڵ㳤��ַ(������Flash��)
    DEVICE_TYPE DevType;                                    // �豸����(������Flash��)
    SUBNODE_PROPERETY Property;                             // �豸����(������Eeprom��)
    uint8 RxLastDataNum;                                   // ���һ�����ݴ��λ��
    uint8 RxChannel;                                        // ����ʹ�õ��ŵ�
} SUBNODE_INFO;

// ������������Ϣ����(��С����Ϊż��)
typedef struct {
    uint16 Fcs;                                             // FcsΪ��������ַ��У��ֵ,������֤��������Ϣ�Ƿ���ȷ
    uint8 LongAddr[LONG_ADDR_SIZE];                         // �������ĳ���ַ,Bcd��
    uint16 MaxNodeId;                                       // ���ڵ������,���Ǳ���ڵ�Ĵ洢�����λ��
    uint8 CustomRouteSaveRegion: 1;                         // �Զ���·�����������
    WORK_PARAM_STRUCT Param;                                // �����Ĳ���
    GPRS_PARAMETER GprsParam;                               // GPRSģ�����
} CONCENTRATOR_INFO;

// �����ݱ����ʽ����
typedef struct {
    uint8 Address[LONG_ADDR_SIZE];                          // �ڵ㳤��ַ
    SUBNODE_PROPERETY Property;                             // �ڵ�����
    uint8 RxLastDataNum;                                   // ���յ�������ݵ���
    uint8 Crc8MeterData;                                    // �����ݵ�У��ֵ
    uint8 MeterData[1];                                     // ��������(������������ֽ�Ϊ���к������ź�ǿ��)
} METER_DATA_SAVE_FORMAT;

/************************************************************************************************
*                        Global Variable Declare Section
************************************************************************************************/
DATABASE_EXT CONCENTRATOR_INFO Concentrator;
DATABASE_EXT SUBNODE_INFO SubNodes[MAX_NODE_NUM];

/************************************************************************************************
*                            Function Declare Section
************************************************************************************************/
DATABASE_EXT void Data_Init(void);
DATABASE_EXT void Data_RefreshNodeStatus(void);
DATABASE_EXT void Data_RdWrConcentratorParam(DATA_FRAME_STRUCT *DataFrmPtr);
DATABASE_EXT void Data_MeterDataInit(METER_DATA_SAVE_FORMAT *MeterBufPtr, uint16 NodeId, uint8 MeterDataLen);
DATABASE_EXT void Data_SetConcentratorAddr(DATA_FRAME_STRUCT *DataFrmPtr);
DATABASE_EXT uint16 Data_FindNodeId(uint16 StartId, uint8 *BufPtr);
DATABASE_EXT bool Data_ClearMeterData(void);
DATABASE_EXT void Data_ClearDatabase(void);
DATABASE_EXT void Data_ReadNodesCount(DATA_FRAME_STRUCT *DataFrmPtr);
DATABASE_EXT void Data_ReadNodes(DATA_FRAME_STRUCT *DataFrmPtr);
DATABASE_EXT void Data_WriteNodes(DATA_FRAME_STRUCT *DataFrmPtr);
DATABASE_EXT void Data_DeleteNodes(DATA_FRAME_STRUCT *DataFrmPtr);
DATABASE_EXT void Data_ModifyNodes(DATA_FRAME_STRUCT *DataFrmPtr);
DATABASE_EXT void Data_GetTimeSlot(uint8 *NodeIdPtr, uint8 *TimeSlotPtr);
DATABASE_EXT uint8 Data_GetRoute(uint16 NodeId, uint8 *BufPtr);
DATABASE_EXT void Data_ReadCustomRoute(DATA_FRAME_STRUCT *DataBufPtr);
DATABASE_EXT void Data_WriteCustomRoute(DATA_FRAME_STRUCT *DataFrmPtr);
DATABASE_EXT void Data_BatchReadCustomRoutes(DATA_FRAME_STRUCT *DataFrmPtr);
DATABASE_EXT void Data_BatchWriteCustomRoutes(DATA_FRAME_STRUCT *DataFrmPtr);
DATABASE_EXT void Data_GprsParameter(DATA_FRAME_STRUCT *DataFramePtr);
DATABASE_EXT void Data_SwUpdate(DATA_FRAME_STRUCT *DataFrmPtr);
DATABASE_EXT void Data_EepromCheckProc(DATA_FRAME_STRUCT *DataFrmPtr);

#endif
/***************************************End of file*********************************************/


