/*
 * FT600 Data Loopback Demo App
 *
 * Copyright (C) 2015 FTDI Chip
 *
 */

#include "stdafx.h"
#include "APP_ReaderThread.h"
#include "APP_TaskManager.h"
#include "APP_PayloadRecording.h"
#include "APP_Utils.h"
#include "APP_Device.h"



IMPLEMENT_DYNCREATE(CReaderThread, CWinThread)



////////////////////////////////////////////////////////////////////////////////////////////////////
//
// CReaderThread::CReaderThread
//      This contains the Reader thread class which reads data from IN endpoints 
//      (EP82, EP83, EP84, and EP85). After reading the data, it saves the read data 
//      to an output file via the Payload Recorder class. The Task Manager thread verifies 
//      the output file of the Writer thread with the output file generated by the Reader thread 
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

CReaderThread::CReaderThread(CTaskManager* a_pTaskManager, UCHAR a_ucEP, CPayloadRecorder *a_pPayloadRecorder, USHORT a_uwMPS):
    m_pTaskManager(a_pTaskManager),
    m_ucEP(a_ucEP),
    m_pPayloadRecorder(a_pPayloadRecorder),
	m_uwMPS(a_uwMPS),
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
    StopTask(FALSE); 
    
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
        // Wait for signal that task is available
        dwResult = ::WaitForSingleObject(m_hEvent, 100);
        if (dwResult == WAIT_OBJECT_0)
        {
            //CMD_LOG(_T("[0x%02x] %s Task available!\n"), m_ucEP, _T(__FUNCTION__));

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
// CReaderThread::ProcessAStartTask
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

ETASK_STATUS CReaderThread::ProcessAStartTask(PLARGE_INTEGER a_pllBytesTransferred, DWORD *a_pdwTimeMs)
{
    ETASK_STATUS eStatus;
    BOOL bFailed = FALSE;
    BOOL bResult = FALSE;
    FT_STATUS ftStatus = FT_OK;
    OVERLAPPED oOverlapped = {0};
    CDriverInterface* pDriver = m_pTaskManager->GetDriver();
    BOOL bErrorMessageShown = FALSE;
    CUtils::CCriticalSectionHolder cs(&m_csEvent);


    SetOngoingTask(TRUE);
    SetStopTask(FALSE);

    a_pllBytesTransferred->QuadPart = 0;
    ULONG ulBytesToRead = m_TestParam.m_llSessionLength.LowPart;
	PUCHAR m_pucBuffer = new UCHAR[ulBytesToRead];
	if (!m_pucBuffer)
	{
		bFailed = TRUE;
		goto cleanup;
	}

    pDriver->InitializeOverlapped(&oOverlapped);

    //
    // Read data from device asynchronously
    //
    PUCHAR pData = m_pucBuffer;
    while (!IsStopTask())
    {
        ULONG ulActualBytesRead = 0;
        ftStatus = pDriver->ReadPipe(m_ucEP, pData, ulBytesToRead, &ulActualBytesRead, &oOverlapped);
        if (ftStatus == FT_IO_PENDING)
        {
            while (!IsStopTask())
            {
                switch (WaitForSingleObject(oOverlapped.hEvent, 3000))
                {
                    case WAIT_OBJECT_0:
                    {
                        CMD_LOG(_T("[0x%02x] WaitForSingleObject WAIT_OBJECT_0!"), m_ucEP);
                        ftStatus = pDriver->GetOverlappedResult(&oOverlapped, &ulActualBytesRead, TRUE);
                        break;
                    }
                    case WAIT_TIMEOUT:
                    {
                        CMD_LOG(_T("[0x%02x] WaitForSingleObject WAIT_TIMEOUT!"), m_ucEP);
                        if (m_pTaskManager->GetLoopbackMonitor(m_ucEP)->HasPendingData())
                        {
                            if (m_TestParam.m_llSessionLength.QuadPart <= FIFO_SIZE)
                            {
                                if (!bErrorMessageShown)
                                {
                                    bErrorMessageShown = TRUE;
                                    ShowErrorMessage();
                                }
                            }
                        }

                        ftStatus = pDriver->GetOverlappedResult(&oOverlapped, &ulActualBytesRead, FALSE);
                        CMD_LOG(_T("[0x%02x] GetOverlappedResult WAIT_TIMEOUT! ftStatus=0x%x"), m_ucEP, ftStatus);
                        continue;
                    }
                    default:
                    {
                        CMD_LOG(_T("[0x%02x] WaitForSingleObject failed!\n"), m_ucEP);
                        bFailed = TRUE;
                        break;
                    }
                }

                break;
            }

            if (IsStopTask())
            {
                break;
            }
        }

        if (bFailed)
        {
            break;
        }

        a_pllBytesTransferred->QuadPart += ulActualBytesRead;
        if (ulActualBytesRead)
        {
            ULONG ulTemp = 0;
            m_pPayloadRecorder->Write(m_pucBuffer, ulActualBytesRead, &ulTemp);
        }

        if (FT_FAILED(ftStatus))
        {
            CMD_LOG(_T("[0x%02x] ReadPipe failed!\n"), m_ucEP);
			pDriver->AbortPipe(m_ucEP);
            bFailed = TRUE;
            break;
        }

        if (a_pllBytesTransferred->QuadPart >= m_TestParam.m_llSessionLength.QuadPart)
        {
            break;
        }

        ulBytesToRead = m_TestParam.m_llSessionLength.LowPart - a_pllBytesTransferred->LowPart;
        if (ulActualBytesRead == 0)
        {
            break;
        }
        else if (ulActualBytesRead % m_uwMPS != 0) // 512 is for USB 2, 1024 if USB 3. Can use 512 instead of 1024
        {
            break;
        }
    }

    CMD_LOG(_T("[0x%02x] %I64d XXX\n"), m_ucEP, a_pllBytesTransferred->QuadPart);

    pDriver->ReleaseOverlapped(&oOverlapped);
	delete[] m_pucBuffer;


cleanup:

    eStatus = bFailed ? ETASK_STATUS_FAILED : (IsStopTask() ? ETASK_STATUS_STOPPED : ETASK_STATUS_COMPLETED);
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
//      a_bFlush                - Indicates if flush should be done instead stop
//
// Return Value
//      TRUE if successful, FALSE if otherwise
//
// Notes
//      None
//
////////////////////////////////////////////////////////////////////////////////////////////////////

BOOL CReaderThread::ProcessAStopTask(BOOL a_bFlush)
{
    if (a_bFlush)
    {
        if (!m_pTaskManager->GetDriver()->FlushPipe(m_ucEP))
        {
            CMD_LOG(_T("[0x%02x] %s FT_FlushPipe failed! GLE=%d\n"), m_ucEP, _T(__FUNCTION__), GetLastError());
            return FALSE;
        }
    }
    else
    {
        if (IsOngoingTask())
        {
            CUtils::CCriticalSectionHolder cs(&m_csStopTask);
            m_bStopTask = TRUE;

            if (!m_pTaskManager->GetDriver()->AbortPipe(m_ucEP))
            {
                CMD_LOG(_T("[0x%02x] %s FT_AbortPipe failed! GLE=%d\n"), m_ucEP, _T(__FUNCTION__), GetLastError());
                return FALSE;
            }
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
// CReaderThread::SetTaskPayloadType
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

BOOL CReaderThread::SetTaskPayloadType(CONST PTTASK_PAYLOAD_PARAM a_pPayload)
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

    m_TestParam.m_llSessionLength.QuadPart = a_pTestParam->m_llSessionLength.QuadPart;
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

BOOL CReaderThread::StopTask(BOOL a_bFlush)
{
    BOOL bResult = ProcessAStopTask(a_bFlush);
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


////////////////////////////////////////////////////////////////////////////////////////////////////
//
// CReaderThread::ShowErrorMessage
//
// Summary
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

VOID CReaderThread::ShowErrorMessage()
{
    APP_LOG(_T("\nWARNING!!!"));
    APP_LOG(_T("Could not read anything from FIFO master. Please check if FPGA image is correct!"));
    APP_LOG(_T("Troubleshooting Guide:"));
    APP_LOG(_T("\t1) Unplug device."));
    APP_LOG(_T("\t2) Is FPGA image for 245 mode or 600 mode?\n\tMake sure chip is also configured using the same mode."));
    APP_LOG(_T("\t3) Is FPGA image for FT601 (32-bit) or FT600 (16-bit)?\n\tMake sure PCB board has the matching architecture."));
    APP_LOG(_T("\t4) Are the jumpers and switches correct?\n\tMake sure jumpers and switches are set correctly on the FPGA board and PCB board."));
}


