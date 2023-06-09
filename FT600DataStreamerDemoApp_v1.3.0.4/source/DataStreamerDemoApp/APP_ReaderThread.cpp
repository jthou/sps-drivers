/*
 * FT600 Data Streamer Demo App
 *
 * Copyright (C) 2015 FTDI Chip
 *
 */

#include "stdafx.h"
#include "APP_ReaderThread.h"
#include "APP_TaskManager.h"
#include "APP_Utils.h"
#include "APP_Device.h"
#include <Windows.h>
#include <thread>

IMPLEMENT_DYNCREATE(CReaderThread, CWinThread)



////////////////////////////////////////////////////////////////////////////////////////////////////
//
// CReaderThread::CReaderThread
//      This contains the Writer thread class which writes data to OUT endpoints 
//      (EP02, EP03, EP04, and EP05). The data written is generated via Payload Generator class. 
//      After writing the data, it saves the written data to an output file via 
//      the Payload Recorder class. The Task Manager thread verifies the output file 
//      of the Writer thread with the output file generated by the Reader thread 
//      using the Payload Verifier class.
//
// Summary
//
// Parameters
//
// Return Value
//
// Notes
//
////////////////////////////////////////////////////////////////////////////////////////////////////

CReaderThread::CReaderThread(CTaskManager* a_pTaskManager, UCHAR a_ucEP):
    m_pTaskManager(a_pTaskManager),
    m_ucEP(a_ucEP),
    m_bCleanupDone(FALSE),
    m_hEvent(NULL),
    m_bStopTask(FALSE),
    m_bOngoingTask(FALSE)
{
    CMD_LOG(_T("INITIALIZATION: %s[%02x]"), _T(__FUNCTION__), m_ucEP);

    try
    {
        ::InitializeCriticalSection(&m_csEvent);
        ::InitializeCriticalSection(&m_csStopTask);
        ::InitializeCriticalSection(&m_csOngoingTask);
        ::InitializeCriticalSection(&m_csCleanup);
    }
    catch (int)
    {
        CMD_LOG(_T("INITIALIZATION: %s[%02x] failed!"), _T(__FUNCTION__), m_ucEP);
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////
//
// CReaderThread::~CReaderThread
//
// Summary
//
// Parameters
//
// Return Value
//
// Notes
//
////////////////////////////////////////////////////////////////////////////////////////////////////

CReaderThread::~CReaderThread()
{
    CMD_LOG(_T("TERMINATION: %s[%02x]"), _T(__FUNCTION__), m_ucEP);
}


////////////////////////////////////////////////////////////////////////////////////////////////////
//
// CReaderThread::InitInstance
//
// Summary
//
// Parameters
//
// Return Value
//
// Notes
//
////////////////////////////////////////////////////////////////////////////////////////////////////

BOOL CReaderThread::InitInstance()
{
    PostThreadMessage(WM_STARTWORK, 0, 0);
    return TRUE;
}


////////////////////////////////////////////////////////////////////////////////////////////////////
//
// CReaderThread::OnStartWork
//
// Summary
//
// Parameters
//
// Return Value
//
// Notes
//
////////////////////////////////////////////////////////////////////////////////////////////////////

VOID CReaderThread::OnStartWork(WPARAM wParam, LPARAM lParam)
{
    //_ASSERTE(_CrtCheckMemory());

    Initialize();
    ProcessTasks();
    Cleanup();
    
    //_ASSERTE(_CrtCheckMemory());

    PostQuitMessage(0);
}

BEGIN_MESSAGE_MAP(CReaderThread, CWinThread)
    ON_THREAD_MESSAGE(WM_STARTWORK, OnStartWork)
END_MESSAGE_MAP()


////////////////////////////////////////////////////////////////////////////////////////////////////
//
// CReaderThread::Initialize
//
// Summary
//      Initialize class
//
// Parameters
//      None
//
// Return Value
//      TRUE if successful, FALSE if otherwise
//
// Notes
//      None
//
////////////////////////////////////////////////////////////////////////////////////////////////////

BOOL CReaderThread::Initialize()
{
    CMD_LOG(_T("INITIALIZATION: %s[%02x]"), _T(__FUNCTION__), m_ucEP);

    m_hEvent = ::CreateEvent(NULL, FALSE, FALSE, NULL);
    if (m_hEvent == NULL)
    {
        return FALSE;
    } 

    return TRUE;
}


////////////////////////////////////////////////////////////////////////////////////////////////////
//
// CReaderThread::Cleanup
//
// Summary
//      Cleans up the class
//
// Parameters
//      None
//
// Return Value
//      None
//
// Notes
//      None
//
////////////////////////////////////////////////////////////////////////////////////////////////////

VOID CReaderThread::Cleanup()
{
    if (m_bCleanupDone)
    {
        return;
    }
    CUtils::CCriticalSectionHolder cs(&m_csCleanup);

    CMD_LOG(_T("TERMINATION: %s[%02x]"), _T(__FUNCTION__), m_ucEP);

    // Stops any ongoing write task
    StopTask(); 
    
    // Quit the main thread processing tasks
    if (m_hEvent)
    {
        ::CloseHandle(m_hEvent);
        m_hEvent = NULL;
    } 

    m_bCleanupDone = TRUE;
}


////////////////////////////////////////////////////////////////////////////////////////////////////
//
// CReaderThread::ProcessTasks
//
// Summary
//      Waits for tasks given by the task manager and processes it
//
// Parameters
//      None
//
// Return Value
//      None
//
// Notes
//      None
//
////////////////////////////////////////////////////////////////////////////////////////////////////

VOID CReaderThread::ProcessTasks()
{
    DWORD dwResult = 0;

    while (!m_bCleanupDone && m_hEvent)
    {
        dwResult = ::WaitForSingleObject(m_hEvent, 100);
        if (dwResult == WAIT_OBJECT_0)
        {
            m_CompletionParam.m_ucEP = m_ucEP;

            if (!m_hEvent) break;

#if USE_ASYNCHRONOUS
            CMD_LOG(_T("ASYNCHRONOUS: %s[%02x]"), _T(__FUNCTION__), m_ucEP);
            m_CompletionParam.m_eStatus = ProcessAStartTaskAsynch();
#else // USE_ASYNCHRONOUS
            CMD_LOG(_T("SYNCHRONOUS: %s[%02x]"), _T(__FUNCTION__), m_ucEP);
            m_CompletionParam.m_eStatus = ProcessAStartTaskSynch();
#endif // USE_ASYNCHRONOUS

            if (!m_hEvent) break;

            if (m_pTaskManager)
            {
                m_pTaskManager->CompletionRoutine(&m_CompletionParam);
            }
        }
        else if (dwResult != WAIT_TIMEOUT)
        {
            break;
        }
    }

    CMD_LOG(_T("TERMINATION: %s[%02x]"), _T(__FUNCTION__), m_ucEP);
}

#define ABORT_AND_BREAK_ON_STOP()								\
    if (IsStopTask())											\
	    {															\
        eStatus = ETASK_STATUS_STOPPED;							\
        (m_pTaskManager->GetDriver())->AbortPipe(m_ucEP);		\
        break;													\
	    }

////////////////////////////////////////////////////////////////////////////////////////////////////
//
// CReaderThread::ProcessAStartTaskSynch
//
// Summary
//      Starts processing a task synchronously
//
// Parameters
//      None
//
// Return Value
//      ETASK_STATUS            - Indicates the status of the task
//
// Notes
//      None
//
////////////////////////////////////////////////////////////////////////////////////////////////////

ETASK_STATUS CReaderThread::ProcessAStartTaskSynch()
{
    ETASK_STATUS eStatus = ETASK_STATUS_FAILED;
    BOOL bFailed = FALSE;
    FT_STATUS ftStatus;
    ULONG ulTimeMs = 0;
    ULONG i = 0;
    FT_HANDLE ftHandle = m_pTaskManager->GetDriver()->m_FTHandle;
    PUCHAR pBuffer = NULL;
    ULONG ulActualBytesTransferred = 0;
    ULONG ulActualBytesToTransfer = m_TestParam.m_ulPacketSize;
    CUtils::CCriticalSectionHolder cs(&m_csEvent);


    SetOngoingTask(TRUE);
    SetStopTask(FALSE);

	/*start the stats thread */
	std::thread t(&CReaderThread::ShowStats, m_pTaskManager->m_pReaderThreads);
	t.detach();

    pBuffer = new UCHAR[m_TestParam.m_ulPacketSize];

    ftStatus = FT_SetStreamPipe(ftHandle, FALSE, FALSE, m_ucEP, ulActualBytesToTransfer);
    if (FT_FAILED(ftStatus))
    {
        goto exit;
    }

	while (IsStopTask() == FALSE)
    {
        ulActualBytesTransferred = 0;

        ftStatus = FT_ReadPipe(ftHandle, m_ucEP, pBuffer, ulActualBytesToTransfer, &ulActualBytesTransferred, NULL);
        if (FT_FAILED(ftStatus))
        {
            CMD_LOG(_T("%s[%02x] : FT_ReadPipe failed! ftStatus=%x"), _T(__FUNCTION__), m_ucEP, ftStatus);
			(m_pTaskManager->GetDriver())->AbortPipe(m_ucEP);
			break;
        }

		m_ulTotalBytesTransferred += ulActualBytesTransferred;

        if (++i == m_TestParam.m_ulQueueSize)
        {
            i = 0;
        }
		ABORT_AND_BREAK_ON_STOP();
    }


exit:

    FT_ClearStreamPipe(ftHandle, FALSE, FALSE, m_ucEP);

    if (pBuffer)
    {
        delete[] pBuffer;
    }

    eStatus = bFailed ? ETASK_STATUS_FAILED : (IsStopTask() ? ETASK_STATUS_STOPPED : ETASK_STATUS_COMPLETED);
    SetOngoingTask(FALSE);
    return eStatus;
}

////////////////////////////////////////////////////////////////////////////////////////////////////
//
// CReaderThread::ProcessAStartTaskAsynch
//
// Summary
//      Starts processing a task
//
// Parameters
//      None
//
// Return Value
//      ETASK_STATUS            - Indicates the status of the task
//
// Notes
//      None
//
////////////////////////////////////////////////////////////////////////////////////////////////////

ETASK_STATUS CReaderThread::ProcessAStartTaskAsynch()
{
    ETASK_STATUS eStatus = ETASK_STATUS_COMPLETED;
    FT_STATUS ftStatus;
    ULONG ulTimeMs = 0;
    ULONG i = 0;
    FT_HANDLE ftHandle = m_pTaskManager->GetDriver()->m_FTHandle;
    OVERLAPPED* pOverlapped = NULL;
    PUCHAR *ppBuffers = NULL;
    ULONG ulActualBytesTransferred = 0;
    ULONG ulActualBytesToTransfer = m_TestParam.m_ulPacketSize;
    CUtils::CCriticalSectionHolder cs(&m_csEvent);

    SetOngoingTask(TRUE);
    SetStopTask(FALSE);

    ftStatus = FT_SetStreamPipe(ftHandle, FALSE, FALSE, m_ucEP, ulActualBytesToTransfer);
    if (FT_FAILED(ftStatus))
    {
        CMD_LOG(_T("TRANSFER: %s[%02x] FT_SetStreamPipe failed! status=0x%x GLE=%x"), _T(__FUNCTION__), m_ucEP, ftStatus, GetLastError());
        SetOngoingTask(FALSE);
        return ETASK_STATUS_FAILED;
    }

	/*start the stats thread */
	std::thread t(&CReaderThread::ShowStats, m_pTaskManager->m_pReaderThreads);
	t.detach();


    pOverlapped = new OVERLAPPED[m_TestParam.m_ulQueueSize];
    ppBuffers = new PUCHAR[m_TestParam.m_ulQueueSize];
    for (i=0; i<m_TestParam.m_ulQueueSize; i++)
    {
        ppBuffers[i] = new UCHAR[m_TestParam.m_ulPacketSize];
        memset(&ppBuffers[i][0], 0x55, m_TestParam.m_ulPacketSize);
        memset(&pOverlapped[i], 0, sizeof(OVERLAPPED));
        ftStatus = FT_InitializeOverlapped(ftHandle, &pOverlapped[i]);
        if (FT_FAILED(ftStatus))
        {
            CMD_LOG(_T("TRANSFER: %s[%02x] FT_InitializeOverlapped failed! status=0x%x"), _T(__FUNCTION__), m_ucEP, ftStatus);
            eStatus = ETASK_STATUS_FAILED;
            goto exit;
        }
    }

    for (i=0; i<m_TestParam.m_ulQueueSize; i++)
    {
        ftStatus = FT_ReadPipeEx(ftHandle, m_ucEP, &ppBuffers[i][0], ulActualBytesToTransfer, &ulActualBytesTransferred, &pOverlapped[i]);
        if (ftStatus != FT_IO_PENDING)
        {
            CMD_LOG(_T("TRANSFER: %s[%02x] FT_ReadPipe0 failed! status=0x%x"), _T(__FUNCTION__), m_ucEP, ftStatus);
            eStatus = ETASK_STATUS_FAILED;
			(m_pTaskManager->GetDriver())->AbortPipe(m_ucEP);
            goto exit;
        }
    }

    i = 0;
	while (1)
	{

		ftStatus = FT_GetOverlappedResult(ftHandle, &pOverlapped[i], &ulActualBytesTransferred, TRUE);
		if (ftStatus == FT_DEVICE_NOT_CONNECTED)
		{
			CMD_LOG(_T("TRANSFER: %s[%02x] FT_GetOverlappedResult failed! Device unplugged!"), _T(__FUNCTION__), m_ucEP);
			eStatus = ETASK_STATUS_UNPLUGGED;
			(m_pTaskManager->GetDriver())->AbortPipe(m_ucEP);
			goto exit;
		}
		else if (FT_FAILED(ftStatus))
		{
			CMD_LOG(_T("TRANSFER: %s[%02x] FT_GetOverlappedResult failed! status=0x%x GLE=0x%x ulActualBytesTransferred=%d"), _T(__FUNCTION__), m_ucEP, ftStatus, GetLastError(), ulActualBytesTransferred);
			eStatus = ETASK_STATUS_STOPPED;
			(m_pTaskManager->GetDriver())->AbortPipe(m_ucEP);
			goto exit;
		}

		m_ulTotalBytesTransferred += ulActualBytesTransferred;
		if (++i == m_TestParam.m_ulQueueSize)
		{
			i = 0;
		}


		if (IsStopTask() == FALSE)
		{
			ULONG j = (i == 0 ? m_TestParam.m_ulQueueSize - 1 : i - 1);
			ftStatus = FT_ReadPipeEx(ftHandle, m_ucEP, &ppBuffers[j][0], ulActualBytesToTransfer, &ulActualBytesTransferred, &pOverlapped[j]);
			if (ftStatus != FT_IO_PENDING)
			{
				CMD_LOG(_T("TRANSFER: %s[%02x] FT_ReadPipe failed! status=0x%x"), _T(__FUNCTION__), m_ucEP, ftStatus);
				eStatus = ETASK_STATUS_FAILED;
				(m_pTaskManager->GetDriver())->AbortPipe(m_ucEP);
				break;
			}
		}

        ABORT_AND_BREAK_ON_STOP();
    }


exit:

    FT_ClearStreamPipe(ftHandle, FALSE, FALSE, m_ucEP);

    if (pOverlapped)
    {
        for (i=0; i<m_TestParam.m_ulQueueSize; i++)
        {
            FT_ReleaseOverlapped(ftHandle, &pOverlapped[i]);
        }
        delete[] pOverlapped;
    }

    if (ppBuffers)
    {
        for (i=0; i<m_TestParam.m_ulQueueSize; i++)
        {
            if (ppBuffers[i])
            {
                delete[] ppBuffers[i];
            }
        }
        delete[] ppBuffers;
    }

    SetOngoingTask(FALSE);
    return eStatus;
}


////////////////////////////////////////////////////////////////////////////////////////////////////
//
// CReaderThread::ProcessAStopTask
//
// Summary
//      Stops processing a task
//
// Parameters
//      None
//
// Return Value
//      TRUE if successful, FALSE if otherwise
//
// Notes
//      None
//
////////////////////////////////////////////////////////////////////////////////////////////////////

BOOL CReaderThread::ProcessAStopTask()
{
    CMD_LOG(_T("TRANSFER: %s[%02x] STOP!"), _T(__FUNCTION__), m_ucEP);

    if (IsOngoingTask())
    {
        CUtils::CCriticalSectionHolder cs(&m_csStopTask);
        m_bStopTask = TRUE;
    }

    DWORD dwCounter = 0;
    while (IsOngoingTask())
    {
        Sleep(1);
        if (++dwCounter > 5000)
        {
            CMD_LOG(_T("%s: STOP %d"), _T(__FUNCTION__), dwCounter);
			(m_pTaskManager->GetDriver())->AbortPipe(m_ucEP);
            return FALSE;
        }
    }

    return TRUE;
}


////////////////////////////////////////////////////////////////////////////////////////////////////
//
// CReaderThread::IsStopTask
//
// Summary
//      Checks if the task is stopped
//
// Parameters
//      None
//
// Return Value
//      TRUE if stopped, FALSE if otherwise
//
// Notes
//      None
//
////////////////////////////////////////////////////////////////////////////////////////////////////

BOOL CReaderThread::IsStopTask()
{
    CUtils::CCriticalSectionHolder cs(&m_csStopTask);
    return m_bStopTask;
}


////////////////////////////////////////////////////////////////////////////////////////////////////
//
// CReaderThread::SetStopTask
//
// Summary
//      Sets the task is stopped
//
// Parameters
//      None
//
// Return Value
//      None
//
// Notes
//      None
//
////////////////////////////////////////////////////////////////////////////////////////////////////

VOID CReaderThread::SetStopTask(BOOL a_bSet)
{
    CUtils::CCriticalSectionHolder cs(&m_csStopTask);
    m_bStopTask = a_bSet;
}


////////////////////////////////////////////////////////////////////////////////////////////////////
//
// CReaderThread::IsOngoingTask
//
// Summary
//      Checks if the task is ongoing
//
// Parameters
//      None
//
// Return Value
//      TRUE if ongoing, FALSE if otherwise
//
// Notes
//      None
//
////////////////////////////////////////////////////////////////////////////////////////////////////

BOOL CReaderThread::IsOngoingTask()
{
    CUtils::CCriticalSectionHolder cs(&m_csOngoingTask);
    return m_bOngoingTask;
}


////////////////////////////////////////////////////////////////////////////////////////////////////
//
// CReaderThread::SetOngoingTask
//
// Summary
//      Sets that the task is ongoing
//
// Parameters
//      a_bSet      - Indicates if task is ongoing or not
//
// Return Value
//      None
//
// Notes
//      None
//
////////////////////////////////////////////////////////////////////////////////////////////////////

VOID CReaderThread::SetOngoingTask(BOOL a_bSet)
{
    CUtils::CCriticalSectionHolder cs(&m_csOngoingTask);
    m_bOngoingTask = a_bSet;
}


////////////////////////////////////////////////////////////////////////////////////////////////////
//
// CReaderThread::SetTaskCompletionIdentifier
//
// Summary
//      Sets the task completion identifier
//
// Parameters
//      a_pllID         - task identifier
//
// Return Value
//      TRUE if successful, FALSE if otherwise
//
// Notes
//      None
//
////////////////////////////////////////////////////////////////////////////////////////////////////

BOOL CReaderThread::SetTaskCompletionIdentifier(CONST PLARGE_INTEGER a_pllID)
{
    if (!a_pllID)
    {
        return FALSE;
    }

    m_CompletionParam.m_llTaskNumber.QuadPart = a_pllID->QuadPart;
    return TRUE;
}


////////////////////////////////////////////////////////////////////////////////////////////////////
//
// CReaderThread::SetTaskTestParam
//
// Summary
//      Sets the task test parameter
//
// Parameters
//      a_pTestParam    - Task parameter
//
// Return Value
//      TRUE if successful, FALSE if otherwise
//
// Notes
//      None
//
////////////////////////////////////////////////////////////////////////////////////////////////////

BOOL CReaderThread::SetTaskTestParam(CONST PTTASK_TEST_PARAM a_pTestParam)
{
    if (!a_pTestParam)
    {
        return FALSE;
    }

    memcpy(&m_TestParam, a_pTestParam, sizeof(*a_pTestParam));
    return TRUE;
}


////////////////////////////////////////////////////////////////////////////////////////////////////
//
// CReaderThread::StartTask
//
// Summary
//      Starts processing the given task.
//      The task should have been set before calling this function
//
// Parameters
//      None
//
// Return Value
//      TRUE if successful, FALSE if otherwise
//
// Notes
//      None
//
////////////////////////////////////////////////////////////////////////////////////////////////////

BOOL CReaderThread::StartTask()
{
    BOOL bResult = FALSE;
    
    if (m_hEvent)
    {
        bResult = ::SetEvent(m_hEvent);
    }
    return bResult;
}


////////////////////////////////////////////////////////////////////////////////////////////////////
//
// CReaderThread::StopTask
//
// Summary
//      Stops the ongoing task
//
// Parameters
//      a_bFlush    - Flush instead of stop
//
// Return Value
//      TRUE if successful, FALSE if otherwise
//
// Notes
//      None
//
////////////////////////////////////////////////////////////////////////////////////////////////////

BOOL CReaderThread::StopTask()
{
    BOOL bResult = ProcessAStopTask();
    return bResult;
}


////////////////////////////////////////////////////////////////////////////////////////////////////
//
// CReaderThread::ExitThread
//
// Summary
//      Signals and waits for the write thread to exit
//
// Parameters
//      None
//
// Return Value
//      TRUE if successful, FALSE if otherwise
//
// Notes
//      None
//
////////////////////////////////////////////////////////////////////////////////////////////////////

BOOL CReaderThread::ExitThread()
{
    CMD_LOG(_T("TERMINATION: %s[%02x]"), _T(__FUNCTION__), m_ucEP);

    StopTask();

    if (m_hEvent)
    {
        ::CloseHandle(m_hEvent);
        m_hEvent = NULL;
    }

    return TRUE;
}


////////////////////////////////////////////////////////////////////////////////////////////////////
//
// CReaderThread::ShowStatus
//
// Summary
//      Displays the transfer rate
//
// Parameters
//      None
//
// Return Value
//      None
//
// Notes
//      None
//
////////////////////////////////////////////////////////////////////////////////////////////////////
VOID CReaderThread::ShowStatus(ULONG ulTotalBytesTransferred, ULONG ulTimeMs)
{
    ULONG ulTransferRate = (ULONG)((ulTotalBytesTransferred/(double)ulTimeMs) * 0.001);
    APP_LOG(_T("Transferring %d (%d x %d) bytes took %d ms. Rate is %d MBps."), 
        ulTotalBytesTransferred, m_TestParam.m_ulPacketSize, m_TestParam.m_ulQueueSize, ulTimeMs, ulTransferRate);
    STAT_LOG(ulTransferRate, FALSE);
}

VOID CReaderThread::ShowStats()
{
	ULONG64 CurrentRxBytes = 0, PrevRxBytes = 0, TotalRxBytes = 0;
	ULONG CurrRateRxBytes = 0;
	LARGE_INTEGER Frequency = { 0 };
	LARGE_INTEGER StartingTime = { 0 };
	LARGE_INTEGER EndingTime = { 0 };
	ULONG uSeconds = 0; /* Micro seconds */
	
	QueryPerformanceFrequency(&Frequency);
	
	while (!m_bCleanupDone && !m_bStopTask)
	{

		QueryPerformanceCounter(&StartingTime);
		PrevRxBytes = m_ulTotalBytesTransferred;

		//wait a second.
		Sleep(1000);
		TotalRxBytes = m_ulTotalBytesTransferred;
		QueryPerformanceCounter(&EndingTime);

		if (TotalRxBytes >= PrevRxBytes)
		{
			CurrentRxBytes = (TotalRxBytes - PrevRxBytes);
		}
		else
		{
			/* wrap around case */
			CurrentRxBytes = TotalRxBytes + (0xFFFFFFFF - PrevRxBytes);
		}
		/* multiply by 1000000 to convert to uSeconds and to guard against loss of precision */
		uSeconds = (ULONG) (((EndingTime.QuadPart - StartingTime.QuadPart)*1000000) / (Frequency.QuadPart));
		CurrRateRxBytes = (ULONG) ((CurrentRxBytes) / uSeconds);
		
		APP_LOG(_T("Read Transfer Rate : %d MBps  "), CurrRateRxBytes);
		STAT_LOG(CurrRateRxBytes, FALSE);
	}
}

