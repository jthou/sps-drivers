/*
 * FT600 Data Loopback Demo App
 *
 * Copyright (C) 2015 FTDI Chip
 *
 */

#include "stdafx.h"
#include "APP_WriterThread.h"
#include "APP_TaskManager.h"
#include "APP_PayloadGeneration.h"
#include "APP_PayloadRecording.h"
#include "APP_Utils.h"
#include "APP_Device.h"
#include <Windows.h>



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

CWriterThread::CWriterThread(CTaskManager* a_pTaskManager, UCHAR a_ucEP, CPayloadRecorder *a_pPayloadRecorder):
    m_pTaskManager(a_pTaskManager),
    m_ucEP(a_ucEP),
    m_pPayloadRecorder(a_pPayloadRecorder),
    m_bCleanupDone(FALSE),
    m_hEvent(NULL),
    m_bStopTask(FALSE),
    m_bOngoingTask(FALSE)
{
    CMD_LOG(_T("INITIALIZATION: %s[%02x]"), _T(__FUNCTION__), m_ucEP);

    ::InitializeCriticalSection(&m_csEvent);
    ::InitializeCriticalSection(&m_csStopTask);
    ::InitializeCriticalSection(&m_csOngoingTask);
    ::InitializeCriticalSection(&m_csCleanup);

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
    //CMD_LOG(_T("TERMINATION: %s[%02x]"), _T(__FUNCTION__), m_ucEP);

    while (!m_bCleanupDone)
    {
        Sleep(500);
    }

    //::DeleteCriticalSection(&m_csEvent);
    //::DeleteCriticalSection(&m_csStopTask);
    //::DeleteCriticalSection(&m_csOngoingTask);
    //::DeleteCriticalSection(&m_csCleanup);
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
    StopTask(FALSE); 
    
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
        // Wait for signal that task is available
        dwResult = ::WaitForSingleObject(m_hEvent, 100);
        if (dwResult == WAIT_OBJECT_0)
        {
            //CMD_LOG(_T("[0x%02x] %s Task available!\n"), m_ucEP, _T(__FUNCTION__));

            m_CompletionParam.m_llPayloadTransferred.QuadPart = 0;
            m_CompletionParam.m_dwTimeMs = 0;

            if (!m_hEvent)
            {
                break;
            }

            // Process the task
            m_CompletionParam.m_eStatus = ProcessAStartTask(&m_CompletionParam.m_llPayloadTransferred, 
                                                            &m_CompletionParam.m_dwTimeMs);

            if (!m_hEvent)
            {
                break;
            }

            // Inform task manager of task completion status
            if (m_pTaskManager)
            {
                m_pTaskManager->CompletionRoutine(&m_CompletionParam);
            }

            continue;
        }
        else if (dwResult == WAIT_TIMEOUT)
        {
            continue;
        }

        break;
    }

    CMD_LOG(_T("TERMINATION: %s[%02x]"), _T(__FUNCTION__), m_ucEP);
}


////////////////////////////////////////////////////////////////////////////////////////////////////
//
// CWriterThread::ProcessAStartTask
//
// Summary
//      Starts processing a task
//
// Parameters
//      a_pllBytesTransferred   - Pointer to contain the number of bytes transferred
//      a_pdwTimeMs             - Pointer to contain the length of time the data was transferred
//
// Return Value
//      ETASK_STATUS            - Indicates the status of the task
//
// Notes
//      None
//
////////////////////////////////////////////////////////////////////////////////////////////////////

ETASK_STATUS CWriterThread::ProcessAStartTask(PLARGE_INTEGER a_pllBytesTransferred, DWORD *a_pdwTimeMs)
{
    ETASK_STATUS eStatus;
    BOOL bFailed = FALSE;
    BOOL bResult = FALSE;
    FT_STATUS ftStatus = FT_OK;
    CDriverInterface* pDriver = m_pTaskManager->GetDriver();
    CUtils::CCriticalSectionHolder cs(&m_csEvent);


    SetOngoingTask(TRUE);
    SetStopTask(FALSE);

    //
    // Initialize payload creator
    //
	PUCHAR m_pucBuffer = new UCHAR[(ULONG)m_TestParam.m_llSessionLength.QuadPart];
	if (!m_pucBuffer)
	{
		bFailed = TRUE;
		goto cleanup;
	}
	bResult = m_pPayload->Initialize(&m_Payload, &m_TestParam.m_llSessionLength, (ULONG)m_TestParam.m_llSessionLength.QuadPart);
    if (!bResult)
    {
        bFailed = TRUE;
        goto cleanup;
    }
    a_pllBytesTransferred->QuadPart = 0;

    //
    // Read data from payload class
    //
    ULONG ulActualBytesToWrite = 0;
    {
		bResult = m_pPayload->GetData(m_pucBuffer, (ULONG)m_TestParam.m_llSessionLength.QuadPart, &ulActualBytesToWrite);
        if (!bResult)
        {
            CMD_LOG(_T("[0x%02x] %s GetData failed!\n"), m_ucEP, _T(__FUNCTION__));
            bFailed = TRUE;
            goto cleanup;
        }
        if (ulActualBytesToWrite == 0)
        {
            goto cleanup;
        }
    }

    //
    // Write data to device synchronously
    //
    PUCHAR pData = m_pucBuffer;
    while (!IsStopTask())
    {
        ULONG ulActualBytesWritten = 0;
        ftStatus = pDriver->WritePipe(m_ucEP, pData, ulActualBytesToWrite, &ulActualBytesWritten, NULL);

        if (ulActualBytesWritten)
        {
            a_pllBytesTransferred->QuadPart += ulActualBytesWritten;
            ULONG ulTemp = 0;
            m_pPayloadRecorder->Write(pData, ulActualBytesWritten, &ulTemp);
        }

        if (FT_FAILED(ftStatus))
        {
            CMD_LOG(_T("[0x%02x] WritePipe failed!\n"), m_ucEP);
			pDriver->AbortPipe(m_ucEP);
            bFailed = TRUE;
            break;
        }

        if (a_pllBytesTransferred->QuadPart >= m_TestParam.m_llSessionLength.QuadPart)
        {
            break;
        }

        pData += ulActualBytesWritten;
        ulActualBytesToWrite -= ulActualBytesWritten;
    }

    CMD_LOG(_T("[0x%02x] %I64d XXX\n"), m_ucEP, a_pllBytesTransferred->QuadPart);


cleanup:
	if (m_pucBuffer)
	{
		delete[] m_pucBuffer;
		m_pucBuffer = NULL;
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
//      a_bFlush                - Indicates if flush should be done instead stop
//
// Return Value
//      TRUE if successful, FALSE if otherwise
//
// Notes
//      None
//
////////////////////////////////////////////////////////////////////////////////////////////////////

BOOL CWriterThread::ProcessAStopTask(BOOL a_bFlush)
{
    if (IsOngoingTask())
    {
        CUtils::CCriticalSectionHolder cs(&m_csStopTask);
        m_bStopTask = TRUE;

        if (!a_bFlush)
        {
            if (!m_pTaskManager->GetDriver()->AbortPipe(m_ucEP))
            {
                CMD_LOG(_T("[0x%02x] %s FT_AbortPipe failed! GLE=%d\n"), m_ucEP, _T(__FUNCTION__), GetLastError());
                return FALSE;
            }
        }
    }

    if (a_bFlush)
    {
        if (!m_pTaskManager->GetDriver()->FlushPipe(m_ucEP))
        {
            CMD_LOG(_T("[0x%02x] %s FT_FlushPipe failed! GLE=%d\n"), m_ucEP, _T(__FUNCTION__), GetLastError());
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
// CWriterThread::SetTaskPayloadType
//
// Summary
//      Sets the task payload parameter
//
// Parameters
//      a_pPayload      - Payload parameter
//
// Return Value
//      TRUE if successful, FALSE if otherwise
//
// Notes
//      None
//
////////////////////////////////////////////////////////////////////////////////////////////////////

BOOL CWriterThread::SetTaskPayloadType(CONST PTTASK_PAYLOAD_PARAM a_pPayload)
{
    if (!a_pPayload)
    {
        return FALSE;
    }

    m_Payload.m_eType = a_pPayload->m_eType;
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

    m_TestParam.m_llSessionLength.QuadPart = a_pTestParam->m_llSessionLength.QuadPart;
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

BOOL CWriterThread::StopTask(BOOL a_bFlush)
{
    BOOL bResult = ProcessAStopTask(a_bFlush);
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
    //CMD_LOG(_T("TERMINATION: %s[%02x]"), _T(__FUNCTION__), m_ucEP);

    if (m_hEvent)
    {
        ::CloseHandle(m_hEvent);
        m_hEvent = NULL;
    }

    // Wait for main thread to exit loop and to perform its cleanup
    //while (!m_bCleanupDone)
    //{
    //    Sleep(1);
    //}

    return TRUE;
}


