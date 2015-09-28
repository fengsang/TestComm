// SerialPortEx.cpp: implementation of the CSerialPortEx class.
//
//////////////////////////////////////////////////////////////////////
//
//Author:Xujl
//

//#include "stdafx.h"
#include "SerialPortEx.h"

#ifdef _DEBUG
#undef THIS_FILE
static char THIS_FILE[]=__FILE__;
#define new DEBUG_NEW
#endif

//////////////////////////////////////////////////////////////////////
// Construction/Destruction
//////////////////////////////////////////////////////////////////////
UINT WINAPI CommProcThread(LPVOID pParam)
{
	OVERLAPPED os;
	DWORD dwMask, dwTrans;
	COMSTAT ComStat;
	DWORD dwErrorFlags;
	
	CSerialPortEx *pSerialPort = (CSerialPortEx*)pParam;
	memset(&os, 0, sizeof(OVERLAPPED));
	os.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
	
	if(os.hEvent == NULL)
	{
		return (UINT)-1;
	}
	
	while(pSerialPort->m_bOpened)
	{
		//清除串口状态，获取当前串口状态
		ClearCommError(pSerialPort->m_hCom, &dwErrorFlags, &ComStat);
		//串口输入缓存中有数据
		if(ComStat.cbInQue)
		{
			//等待操作事件有效，防止线程反复的去操作RecvChar函数。
			WaitForSingleObject(pSerialPort->m_hCanReadEvent, INFINITE);
			ResetEvent(pSerialPort->m_hCanReadEvent);
	
			pSerialPort->RecvChar();

			continue;
		}
		
		dwMask = 0;
		
		//等待串口事件
		if(!WaitCommEvent(pSerialPort->m_hCom, &dwMask, &os)) // 重叠操作
		{
			//串口一直在等待
			if(GetLastError() == ERROR_IO_PENDING)
			{
				//获取当前等待事件的结果，TRUE一直等待
				GetOverlappedResult(pSerialPort->m_hCom, &os, &dwTrans, TRUE);
			}
			else
			{
				CloseHandle(os.hEvent);

				return (UINT)-1;
			}
		}
	}
	
	CloseHandle(os.hEvent);
	
	return 0;
}

CSerialPortEx::CSerialPortEx()
{
	m_hCom = NULL;
	m_bInitComOk = FALSE;
	m_bOpened = FALSE;
	m_hThread = NULL;
	m_nBaudRate = 9600;
	m_nByteSize = 8;
	m_nFlowCtrl = 0;
	m_nParity = 0;
	m_szPort = "COM1";
	m_nStopBits = 0;

	InitializeCriticalSection(&m_csComSync);

	if((m_hCanReadEvent = CreateEvent(NULL, TRUE, TRUE, NULL)) == NULL)
	{
		return;
	}
	
	memset(&m_osRead, 0, sizeof(OVERLAPPED));
	memset(&m_osWrite, 0, sizeof(OVERLAPPED));
	
	if((m_osRead.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL)) == NULL)
	{
		return;
	}
	
	if((m_osWrite.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL)) == NULL)
	{
		return;
	}
}

CSerialPortEx::~CSerialPortEx()
{
	CloseCom();
	
	// 删除事件句柄
	if(NULL != m_hCanReadEvent)
	{
		CloseHandle(m_hCanReadEvent);
	}
	
	if(NULL != m_osRead.hEvent)
	{
		CloseHandle(m_osRead.hEvent);
	}
	
	if(NULL != m_osWrite.hEvent)
	{
		CloseHandle(m_osWrite.hEvent);
	}

	m_CharList.RemoveAll();

	DeleteCriticalSection(&m_csComSync);
}

BOOL CSerialPortEx::InitComPort(const int nComPort,
								const int nBaudRate,
								const int nByteSize,
								const int nStopBits,
								const int nParity
							 )
{
	COMMTIMEOUTS TimeOuts;

	m_nBaudRate = nBaudRate;
	m_nByteSize = nByteSize;
	m_nStopBits = nStopBits;
	m_nParity = nParity;
	if(nComPort <= 9)
	{
		m_szPort.Format(_T("COM%d"), nComPort);
	}
	else
	{
		m_szPort.Format(_T("\\\\.\\COM%d"), nComPort);
	}

	CloseCom();

	m_bInitComOk = FALSE;
	m_hCom = CreateFile(m_szPort, GENERIC_READ | GENERIC_WRITE, 0, NULL,
		OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED, NULL); // 重叠方式

	if(m_hCom == INVALID_HANDLE_VALUE)
	{
		return FALSE;
	}

	SetupComm(m_hCom, MAXBLOCK, MAXBLOCK);
	//设置串口标志
	SetCommMask(m_hCom, EV_RXCHAR);

	//将ReadIntervalTimeout设置为MAXDWORD，
	//并将ReadTotalTimeoutMultiplier 和ReadTotalTimeoutConstant设置为0
	//表示读取操作将立即返回存放在输入缓冲区的字符。 
	TimeOuts.ReadIntervalTimeout = MAXDWORD; //两个字符间隔最大时间
	TimeOuts.ReadTotalTimeoutMultiplier = 0; //读取每个字符间的超时
	TimeOuts.ReadTotalTimeoutConstant = 0;	 //一次读取串口数据的固定超时

	TimeOuts.WriteTotalTimeoutMultiplier = 50; //写入每字符间的超时
	TimeOuts.WriteTotalTimeoutConstant = 2000;//一次写入串口数据的固定超时
	SetCommTimeouts(m_hCom, &TimeOuts);
	
	if(!ConfigCom())
	{
		CloseHandle(m_hCom);
		
		return FALSE;
	}
	
	m_bInitComOk = TRUE;

	return TRUE;
}

BOOL CSerialPortEx::InitComPortEx(const int nComPort,
								const int nBaudRate,
								const int nByteSize,
								const int nStopBits,
								const int nParity,
								BOOL bConfig /*FALSE*/)
{
	COMMTIMEOUTS TimeOuts;
	
	m_nBaudRate = nBaudRate;
	m_nByteSize = nByteSize;
	m_nStopBits = nStopBits;
	m_nParity = nParity;
	if(nComPort <= 9)
	{
		m_szPort.Format(_T("COM%d"), nComPort);
	}
	else
	{
		m_szPort.Format(_T("\\\\.\\COM%d"), nComPort);
	}
	
	CloseCom();
	
	m_bInitComOk = FALSE;
	m_hCom = CreateFile(m_szPort, GENERIC_READ | GENERIC_WRITE, 0, NULL,
		OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED, NULL); // 重叠方式
	
	if(m_hCom == INVALID_HANDLE_VALUE)
	{
		return FALSE;
	}
	
	SetupComm(m_hCom, MAXBLOCK, MAXBLOCK);
	SetCommMask(m_hCom, EV_RXCHAR);
	
	TimeOuts.ReadIntervalTimeout = MAXDWORD; 
	TimeOuts.ReadTotalTimeoutMultiplier = 0; 
	TimeOuts.ReadTotalTimeoutConstant = 0; 
	
	TimeOuts.WriteTotalTimeoutMultiplier = 50; 
	TimeOuts.WriteTotalTimeoutConstant = 2000;
	SetCommTimeouts(m_hCom, &TimeOuts);
	
	if(bConfig)
	{
		if(!ConfigComEx())
		{
			CloseHandle(m_hCom);
			
			return FALSE;
		}
	}
	else
	{
		if(!ConfigCom())
		{
			CloseHandle(m_hCom);
			
			return FALSE;
		}
	}
	
	m_bInitComOk = TRUE;
	
	return TRUE;
}


BOOL CSerialPortEx::ReadCom(char &ch, int &nListLen)
{
	if(!m_bInitComOk)
	{
		return FALSE;
	}
	
	nListLen = m_CharList.GetCount();
	if(nListLen < 1)
	{
		return FALSE;
	}

	EnterCriticalSection(&m_csComSync);
	ch = m_CharList.GetHead();
	m_CharList.RemoveHead();
	LeaveCriticalSection(&m_csComSync);

	return TRUE;
}

DWORD CSerialPortEx::ReadCom(char *szBuff, DWORD dwLength)
{
	if(!m_bInitComOk)
	{
		return 0;
	}

	DWORD dwLen = 0;
	COMSTAT ComStat;
	DWORD dwErrorFlags;
	
	ClearCommError(m_hCom, &dwErrorFlags, &ComStat);
	dwLen = min(dwLength, ComStat.cbInQue);
	ReadFile(m_hCom, szBuff, dwLen, &dwLen, &m_osRead);

	return dwLen;
}

BOOL CSerialPortEx::WriteCom(char ch)
{
	if(!m_bInitComOk)
	{
		return FALSE;
	}

	BOOL bState;
	DWORD dwLen = 1;
	COMSTAT ComStat;
	DWORD dwErrorFlags;
	
	ClearCommError(m_hCom, &dwErrorFlags, &ComStat);
	PurgeComm(m_hCom, PURGE_TXCLEAR);
	bState = WriteFile(m_hCom, &ch, dwLen, &dwLen, &m_osWrite);
	
	if(!bState)
	{
		if(GetLastError() == ERROR_IO_PENDING)
		{
			GetOverlappedResult(m_hCom, &m_osWrite, &dwLen, TRUE);// 等待
		}
		else
		{
			dwLen = 0;
		}
	}
	
	if(dwLen == 1)
	{
		return TRUE;
	}

	return FALSE;
}

DWORD CSerialPortEx::WriteCom(char *szBuff, DWORD dwLength)
{
	if(!m_bInitComOk)
	{
		return 0;
	}

	BOOL bState;
	DWORD dwLen = dwLength;
	COMSTAT ComStat;
	DWORD dwErrorFlags;
	
	ClearCommError(m_hCom, &dwErrorFlags, &ComStat);
	PurgeComm(m_hCom, PURGE_TXCLEAR);
	bState = WriteFile(m_hCom, szBuff, dwLen, &dwLen, &m_osWrite);
	
	if(!bState)
	{
		if(GetLastError() == ERROR_IO_PENDING)
		{
			GetOverlappedResult(m_hCom, &m_osWrite, &dwLen, TRUE);// 等待
		}
		else
		{
			dwLen = 0;
		}
	}
	
	return dwLen;
}

void CSerialPortEx::CloseCom()
{
	CloseMonitor();

	if(m_hCom != NULL)
	{
		PurgeComm(m_hCom, PURGE_TXCLEAR);
		PurgeComm(m_hCom, PURGE_RXCLEAR);
		CloseHandle(m_hCom);
		m_hCom = NULL;

		printf("close com!\n");
	}

	ClearBuff();
}

BOOL CSerialPortEx::ConfigComEx()
{
	DCB dcb;
	BOOL bRet=FALSE;
	
	if(!GetCommState(m_hCom, &dcb))
	{
		return FALSE;
	}
	
	dcb.fBinary = TRUE;
	dcb.BaudRate = m_nBaudRate; // 波特率
	dcb.ByteSize = m_nByteSize; // 每字节位数
	dcb.fParity = FALSE;
	
	switch(m_nParity)
	{//校验设置
	case 0:
		dcb.Parity = NOPARITY;
		
		break;
	case 1:
		dcb.Parity = ODDPARITY;
		
		break;
	case 2:
		dcb.Parity = EVENPARITY;
		
		break;
	case 3:
		dcb.Parity = MARKPARITY;
		
		break;
	case 4:
		dcb.Parity = SPACEPARITY;
		
		break;
	default:
		break;
	}
	
	switch(m_nStopBits)
	{//停止位
	case 1: 
		dcb.StopBits = ONESTOPBIT;
		
		break;
	case 15: 
		dcb.StopBits = ONE5STOPBITS;
		
		break;
	case 2: 
		dcb.StopBits = TWOSTOPBITS;
		
		break;
	default:
		break;
	}
	
	dcb.fDtrControl = DTR_CONTROL_ENABLE;
	dcb.fDsrSensitivity = FALSE;
	dcb.fRtsControl = RTS_CONTROL_DISABLE;
	dcb.fOutxCtsFlow = FALSE;
	dcb.fOutxDsrFlow = FALSE;
	
	return SetCommState(m_hCom, &dcb);
}

BOOL CSerialPortEx::ConfigCom()
{
	DCB dcb;
	BOOL bRet=FALSE;
	
	if(!GetCommState(m_hCom, &dcb))
	{
		return FALSE;
	}
	
	dcb.fBinary = TRUE;
	dcb.BaudRate = m_nBaudRate; // 波特率
	dcb.ByteSize = m_nByteSize; // 每字节位数
	dcb.fParity = TRUE;
	
	switch(m_nParity)
	{//校验设置
	case 0:
		dcb.Parity = NOPARITY;

		break;
	case 1:
		dcb.Parity = ODDPARITY;

		break;
	case 2:
		dcb.Parity = EVENPARITY;

		break;
	case 3:
		dcb.Parity = MARKPARITY;

		break;
	case 4:
		dcb.Parity = SPACEPARITY;

		break;
	default:
		break;
	}
	
	switch(m_nStopBits)
	{//停止位
	case 1: 
		dcb.StopBits = ONESTOPBIT;

		break;
	case 15: 
		dcb.StopBits = ONE5STOPBITS;

		break;
	case 2: 
		dcb.StopBits = TWOSTOPBITS;

		break;
	default:
		break;
	}

	bRet = SetCommState(m_hCom, &dcb);

	DWORD dLastErro = GetLastError();
	return bRet;
}

void CSerialPortEx::ClearBuff()
{
	EnterCriticalSection(&m_csComSync);
	m_CharList.RemoveAll();
	LeaveCriticalSection(&m_csComSync);
}

BOOL CSerialPortEx::RecvChar()
{
	if(!m_bInitComOk)
	{
		return FALSE;
	}

	char cCh;
	int nLength;
	
	if(!m_bOpened)
	{
		SetEvent(m_hCanReadEvent); 

		return FALSE;
	}
	
	nLength = ReadCom(&cCh, 1);

	EnterCriticalSection(&m_csComSync);
	m_CharList.AddTail(cCh);
 	if(m_CharList.GetCount() > 2048)
 	{
		m_CharList.RemoveHead();
	}

	LeaveCriticalSection(&m_csComSync);

	SetEvent(m_hCanReadEvent);
	
	return TRUE;
}

BOOL CSerialPortEx::IsRecvBuffEmpty()
{
	int nCount=0;

	EnterCriticalSection(&m_csComSync);
	nCount = m_CharList.GetCount();
	LeaveCriticalSection(&m_csComSync);

	if(nCount > 0)
	{
		return FALSE;
	}
	else
	{
		return TRUE;
	}
}

BOOL CSerialPortEx::StartMonitor()
{
	if(!m_bInitComOk || m_bOpened)
	{
		return FALSE;
	}

	m_bOpened = FALSE;

	//创建并挂起线程
	m_hThread  = (HANDLE)_beginthreadex(NULL, 0, CommProcThread, this, 0, NULL);
	if(m_hThread == NULL)
	{
		CloseHandle(m_hCom);
		
		return FALSE;
	}
	else
	{
		PurgeComm(m_hCom, PURGE_TXCLEAR);
		PurgeComm(m_hCom, PURGE_RXCLEAR);
		ClearBuff();

		m_bOpened = TRUE;
		::ResumeThread(m_hThread);
	}

	return TRUE;
}

BOOL CSerialPortEx::CloseMonitor()
{
	if(!m_bInitComOk)
	{
		return FALSE;
	}

	if(m_bOpened)
	{
		m_bOpened = FALSE;
		
		SetEvent(m_hCanReadEvent); 
		SetCommMask(m_hCom, 0); 

		WaitForSingleObject(m_hThread, INFINITE);
		m_hThread = NULL;
	}

	return TRUE;
}

BOOL CSerialPortEx::IsInitComOk()
{
	return m_bInitComOk;
}

BOOL CSerialPortEx::IsInMonitor()
{
	return m_bOpened;
}
