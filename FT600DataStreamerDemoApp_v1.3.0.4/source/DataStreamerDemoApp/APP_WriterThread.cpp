/*
 * FT600 Data Streamer Demo App
 *
 * Copyright (C) 2015 FTDI Chip
 *
 */

#include "stdafx.h"
#include "APP_WriterThread.h"
#include "APP_TaskManager.h"
#include "APP_PayloadGeneration.h"
#include "APP_Utils.h"
#include "APP_Device.h"
#include <Windows.h>
#include <thread>


IMPLEMENT_DYNCREATE(CWriterThread, CWinThread)



////////////////////////////////////////////////////////////////////////////////////////////////////
//
// CWriterThread::CWriterThread
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

CWriterThread::CWriterThread(CTaskManager* a_pTaskManager, UCHAR a_ucEP):
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
	m_pPayload = new CPayloadGenerator;

}


////////////////////////////////////////////////////////////////////////////////////////////////////
//
// CWriterThread::~CWriterThread
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

CWriterThread::~CWriterThread()
{
    CMD_LOG(_T("TERMINATION: %s[%02x]"), _T(__FUNCTION__), m_ucEP);
}


////////////////////////////////////////////////////////////////////////////////////////////////////
//
// CWriterThread::InitInstance
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

BOOL CWriterThread::InitInstance()
{
    PostThreadMessage(WM_STARTWORK, 0, 0);
    return TRUE;
}


////////////////////////////////////////////////////////////////////////////////////////////////////
//
// CWriterThread::OnStartWork
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

VOID CWriterThread::OnStartWork(WPARAM wParam, LPARAM lParam)
{
    //_ASSERTE(_CrtCheckMemory());

    Initialize();
    ProcessTasks();
    Cleanup();
    
    //_ASSERTE(_CrtCheckMemory());

    PostQuitMessage(0);
}

BEGIN_MESSAGE_MAP(CWriterThread, CWinThread)
    ON_THREAD_MESSAGE(WM_STARTWORK, OnStartWork)
END_MESSAGE_MAP()


////////////////////////////////////////////////////////////////////////////////////////////////////
//
// CWriterThread::Initialize
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

BOOL CWriterThread::Initialize()
{
    CMD_LOG(_T("INITIALIZATION: %s[%02x]"), _T(__FUNCTION__), m_ucEP);

    m_hEvent = ::CreateEvent(NULL, FALSE, FALSE, NULL);
    if (m_hEvent == NULL)
    {
        return FALSE;
    } 
	(m_pTaskManager->GetDriver())->AbortPipe(m_ucEP);

    return TRUE;
}


////////////////////////////////////////////////////////////////////////////////////////////////////
//
// CWriterThread::Cleanup
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

VOID CWriterThread::Cleanup()
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
	if (m_pPayload)
	{
		m_pPayload->Cleanup();
		delete m_pPayload;
		m_pPayload = NULL;
	}

	m_bCleanupDone = TRUE;
}


////////////////////////////////////////////////////////////////////////////////////////////////////
//
// CWriterThread::ProcessTasks
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

VOID CWriterThread::ProcessTasks()
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
        (m_pTaskManager->GetDriver())->AbortPipe(m_ucEP);		\
        break;													\
	    }

////////////////////////////////////////////////////////////////////////////////////////////////////
//
// CWriterThread::ProcessAStartTaskSynch
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

ETASK_STATUS CWriterThread::ProcessAStartTaskSynch()
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
	LARGE_INTEGER llTotalPayload;
    CUtils::CCriticalSectionHolder cs(&m_csEvent);


    SetOngoingTask(TRUE);
    SetStopTask(FALSE);

	/* start the stats thread */
	std::thread t(&CWriterThread::ShowStats, m_pTaskManager->m_pWriterThreads);
	t.detach();

    pBuffer = new UCHAR[m_TestParam.m_ulPacketSize];
	
	llTotalPayload.QuadPart = m_TestParam.m_ulPacketSize;

	m_pPayload->Initialize(&m_Payload, &llTotalPayload, m_TestParam.m_ulPacketSize);
	m_pPayload->ResetCounter();
    ftStatus = FT_SetStreamPipe(ftHandle, FALSE, FALSE, m_ucEP, ulActualBytesToTransfer);
    if (FT_FAILED(ftStatus))
    {
        goto exit;
    }

	

	if (m_Payload.m_eType != ETASK_PAYLOAD_TYPE_INCREMENTAL)
	{
		bFailed = !m_pPayload->GetData(pBuffer, m_TestParam.m_ulPacketSize, &ulActualBytesToTransfer);
		if (bFailed)
		{
			CMD_LOG(_T("[0x%02x] %s GetData failed!\n"), m_ucEP, _T(__FUNCTION__));
			goto exit;
		}
	}

	while (1)
    {
        ulActualBytesTransferred = 0;
		if (m_Payload.m_eType == ETASK_PAYLOAD_TYPE_INCREMENTAL)
		{
			bFailed = !m_pPayload->GetData(pBuffer, m_TestParam.m_ulPacketSize, &ulActualBytesToTransfer);
			if (bFailed)
			{
				CMD_LOG(_T("[0x%02x] %s GetData failed!\n"), m_ucEP, _T(__FUNCTION__));
				goto exit;
			}
		}

        ftStatus = FT_WritePipe(ftHandle, m_ucEP, pBuffer, ulActualBytesToTransfer, &ulActualBytesTransferred, NULL);
        if (FT_FAILED(ftStatus))
        {
            CMD_LOG(_T("%s[%02x] : FT_WritePipe failed! ftStatus=%x"), _T(__FUNCTION__), m_ucEP, ftStatus);
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
// CWriterThread::ProcessAStartTaskAsynch
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

ETASK_STATUS CWriterThread::ProcessAStartTaskAsynch()
{
    ETASK_STATUS eStatus = ETASK_STATUS_FAILED;
    BOOL bFailed = FALSE;
    FT_STATUS ftStatus;
    ULONG ulTimeMs = 0;
    ULONG i = 0;
    FT_HANDLE ftHandle = m_pTaskManager->GetDriver()->m_FTHandle;
    OVERLAPPED* pOverlapped = NULL;
    PUCHAR *ppBuffers = NULL;
    ULONG ulActualBytesTransferred = 0;
    ULONG ulActualBytesToTransfer = m_TestParam.m_ulPacketSize;
    ULONG ulTotalBytesTransferred = 0;
	LARGE_INTEGER llTotalPayload;

    CUtils::CCriticalSectionHolder cs(&m_csEvent);


    SetOngoingTask(TRUE);
    SetStopTask(FALSE);

	llTotalPayload.QuadPart = m_TestParam.m_ulPacketSize;

	m_pPayload->Initialize(&m_Payload, &llTotalPayload, m_TestParam.m_ulPacketSize);
	m_pPayload->ResetCounter();

    ftStatus = FT_SetStreamPipe(ftHandle, FALSE, FALSE, m_ucEP, ulActualBytesToTransfer);
    if (FT_FAILED(ftStatus))
    {
        CMD_LOG(_T("TRANSFER: %s[%02x] FT_SetStreamPipe failed! status=0x%x GLE=%x"), _T(__FUNCTION__), m_ucEP, ftStatus, GetLastError());
        SetOngoingTask(FALSE);
        return ETASK_STATUS_FAILED;
    }



	/* start the stats thread */
	std::thread t(&CWriterThread::ShowStats, m_pTaskManager->m_pWriterThreads);
	t.detach();


    pOverlapped = new OVERLAPPED[m_TestParam.m_ulQueueSize];
    ppBuffers = new PUCHAR[m_TestParam.m_ulQueueSize];
    for (i=0; i<m_TestParam.m_ulQueueSize; i++)
    {
        ppBuffers[i] = new UCHAR[m_TestParam.m_ulPacketSize];
        //memset(&ppBuffers[i][0], 0x55, m_TestParam.m_ulPacketSize);
		bFailed = !m_pPayload->GetData(&ppBuffers[i][0], m_TestParam.m_ulPacketSize, &ulActualBytesToTransfer);
		if (bFailed)
		{
			CMD_LOG(_T("[0x%02x] %s GetData failed!\n"), m_ucEP, _T(__FUNCTION__));
			goto exit;
		}
        memset(&pOverlapped[i], 0, sizeof(OVERLAPPED));
        ftStatus = FT_InitializeOverlapped(ftHandle, &pOverlapped[i]);
        if (FT_FAILED(ftStatus))
        {
            CMD_LOG(_T("TRANSFER: %s[%02x] FT_InitializeOverlapped failed! status=0x%x"), _T(__FUNCTION__), m_ucEP, ftStatus);
            goto exit;
        }
    }

    for (i=0; i<m_TestParam.m_ulQueueSize; i++)
    {
        ftStatus = FT_WritePipeEx(ftHandle, m_ucEP, &ppBuffers[i][0], ulActualBytesToTransfer, &ulActualBytesTransferred, &pOverlapped[i]);
        if (ftStatus != FT_IO_PENDING)
        {
            CMD_LOG(_T("TRANSFER: %s[%02x] FT_WritePipe0 failed! status=0x%x"), _T(__FUNCTION__), m_ucEP, ftStatus);
			(m_pTaskManager->GetDriver())->AbortPipe(m_ucEP);
            goto exit;
        }
    }

    i = 0;
    while (1)
    {
        //ulActualBytesTransferred = 0;

		ftStatus = FT_GetOverlappedResult(ftHandle, &pOverlapped[i], &ulActualBytesTransferred, TRUE);
		if (FT_FAILED(ftStatus))
		{
			CMD_LOG(_T("TRANSFER: %s[%02x] FT_GetOverlappedResult failed! status=0x%x GLE=0x%x ulActualBytesTransferred=%d"), _T(__FUNCTION__), m_ucEP, ftStatus, GetLastError(), ulActualBytesTransferred);
			(m_pTaskManager->GetDriver())->AbortPipe(m_ucEP);
			goto exit;
		}

        m_ulTotalBytesTransferred += ulActualBytesTransferred;

      
		/*if (m_Payload.m_eType == ETASK_PAYLOAD_TYPE_INCREMENTAL)
		{
			bFailed = !m_pPayload->GetData(&ppBuffers[j][0], m_TestParam.m_ulPacketSize, &ulActualBytesToTransfer);
			if (bFailed)
			{
				CMD_LOG(_T("[0x%02x] %s GetData failed!\n"), m_ucEP, _T(__FUNCTION__));
				break;
			}
		}*/
        ftStatus = FT_WritePipeEx(ftHandle, m_ucEP, &ppBuffers[i][0], ulActualBytesToTransfer, &ulActualBytesTransferred, &pOverlapped[i]);
        if (ftStatus != FT_IO_PENDING)
        {
            CMD_LOG(_T("TRANSFER: %s[%02x] FT_WritePipe failed! status=0x%x"), _T(__FUNCTION__), m_ucEP, ftStatus);
			(m_pTaskManager->GetDriver())->AbortPipe(m_ucEP);
            break;
        }
		i++;
		i = (i == m_TestParam.m_ulQueueSize) ? 0 : i;
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

    eStatus = bFailed ? ETASK_STATUS_FAILED : (IsStopTask() ? ETASK_STATUS_STOPPED : ETASK_STATUS_COMPLETED);
    SetOngoingTask(FALSE);
    return eStatus;
}


////////////////////////////////////////////////////////////////////////////////////////////////////
//
// CWriterThread::ProcessAStopTask
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

BOOL CWriterThread::ProcessAStopTask()
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
// CWriterThread::IsStopTask
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

BOOL CWriterThread::IsStopTask()
{
    CUtils::CCriticalSectionHolder cs(&m_csStopTask);
    return m_bStopTask;
}


////////////////////////////////////////////////////////////////////////////////////////////////////
//
// CWriterThread::SetStopTask
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

VOID CWriterThread::SetStopTask(BOOL a_bSet)
{
    CUtils::CCriticalSectionHolder cs(&m_csStopTask);
    m_bStopTask = a_bSet;
}


////////////////////////////////////////////////////////////////////////////////////////////////////
//
// CWriterThread::IsOngoingTask
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

BOOL CWriterThread::IsOngoingTask()
{
    CUtils::CCriticalSectionHolder cs(&m_csOngoingTask);
    return m_bOngoingTask;
}


////////////////////////////////////////////////////////////////////////////////////////////////////
//
// CWriterThread::SetOngoingTask
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

VOID CWriterThread::SetOngoingTask(BOOL a_bSet)
{
    CUtils::CCriticalSectionHolder cs(&m_csOngoingTask);
    m_bOngoingTask = a_bSet;
}

BOOL CWriterThread::SetTaskPayloadType(CONST PTTASK_PAYLOAD_PARAM a_pPayload)
{
	if (!a_pPayload)
	{
		return FALSE;
	}

	m_Payload.m_eType = a_pPayload->m_eType;
	m_Payload.m_ulFixedValue = a_pPayload->m_ulFixedValue;
	//m_Payload.m_ulFixedValue2 = a_pPayload->m_ulFixedValue2;
	m_Payload.m_bIntfType = a_pPayload->m_bIntfType;
	return TRUE;
}

////////////////////////////////////////////////////////////////////////////////////////////////////
//
// CWriterThread::SetTaskCompletionIdentifier
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

BOOL CWriterThread::SetTaskCompletionIdentifier(CONST PLARGE_INTEGER a_pllID)
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
// CWriterThread::SetTaskTestParam
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

BOOL CWriterThread::SetTaskTestParam(CONST PTTASK_TEST_PARAM a_pTestParam)
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
// CWriterThread::StartTask
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

BOOL CWriterThread::StartTask()
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
// CWriterThread::StopTask
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

BOOL CWriterThread::StopTask()
{
    BOOL bResult = ProcessAStopTask();
    return bResult;
}


////////////////////////////////////////////////////////////////////////////////////////////////////
//
// CWriterThread::ExitThread
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

BOOL CWriterThread::ExitThread()
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
// CWriterThread::ShowStatus
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
VOID CWriterThread::ShowStatus(ULONG ulTotalBytesTransferred, ULONG ulTimeMs)
{
    ULONG ulTransferRate = (ULONG)((ulTotalBytesTransferred/(double)ulTimeMs) * 0.001);
    APP_LOG(_T("Transferring %d (%d x %d) bytes took %d ms. Rate is %d MBps."), 
        ulTotalBytesTransferred, m_TestParam.m_ulPacketSize, m_TestParam.m_ulQueueSize, ulTimeMs, ulTransferRate);
    STAT_LOG(ulTransferRate, TRUE);
}

VOID CWriterThread::ShowStats()
{
	ULONG64 CurrentTxBytes = 0, PrevTxBytes = 0, TotalTxBytes = 0;
	ULONG CurrRateTxBytes = 0;
	LARGE_INTEGER Frequency = { 0 };
	LARGE_INTEGER StartingTime = { 0 };
	LARGE_INTEGER EndingTime = { 0 };
	ULONG uSeconds = 0; /* Micro seconds */

	QueryPerformanceFrequency(&Frequency);
	
	while (!m_bCleanupDone && !m_bStopTask)
	{

		QueryPerformanceCounter(&StartingTime);
		PrevTxBytes = m_ulTotalBytesTransferred;

		//wait a second.
		Sleep(1000);
		QueryPerformanceCounter(&EndingTime);
		TotalTxBytes = m_ulTotalBytesTransferred;

		if (TotalTxBytes > PrevTxBytes)
		{
			CurrentTxBytes = (TotalTxBytes - PrevTxBytes);
		}
		else
		{
			/* wrap around case */
			CurrentTxBytes = TotalTxBytes + (0xFFFFFFFF - PrevTxBytes);
		}
		/* multiply by 1000000 to convert to uSeconds and to guard against loss of precision */
		uSeconds = (ULONG) (((EndingTime.QuadPart - StartingTime.QuadPart) * 1000000) / (Frequency.QuadPart));
		CurrRateTxBytes = (ULONG) ((CurrentTxBytes ) / uSeconds);

		APP_LOG(_T("Write Transfer Rate : %d MBps  "), CurrRateTxBytes);
		STAT_LOG(CurrRateTxBytes, TRUE);
	}
}

