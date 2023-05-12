#include "spikelinkinput.h"
#include <cassert>
#include <iostream>

#include "DRV_DriverInterface.h"

const int8_t SPS10_002_PKT_HEADER[5] = {0X00, 0X63, 0X60, 0X63 };

SpikeLinkUSB3* SpikeLinkUSB3::CreateInstance() {
    SpikeLinkUSB3* const instance = new SpikeLinkUSB3;
    return (instance);
}

SpikeLinkUSB3::SpikeLinkUSB3(): devInfo_(nullptr), params_(nullptr), dataPool_(nullptr),
    readThrd_(nullptr), readEP_(0x82), nWriteEP_(0), nReadEP_(0) {
}

SpikeLinkUSB3::~SpikeLinkUSB3() {
    Fini();
}

int32_t SpikeLinkUSB3::Init(SpikeLinkInitParams *initParams, ISpikeLinkInputObserver *obsver) {
    assert(initParams != nullptr);
    assert(obsver != nullptr);

    if (initParams == nullptr || initParams->opaque == nullptr || obsver == nullptr) {
        return (-1);
    }

    if (obsver_ != nullptr || dataPool_ != nullptr) {
        return (1);
    }

    {
        unique_lock<mutex> lock(mtx_);
        initParams->picture.ppsSize = SPS10_002_PKT_HEADER_SIZE;
        if (SpikeLinkBaseInput::Init(initParams, obsver) != 0) {
            return (-1);
        }

        if (params_ == nullptr) {
            params_.reset(new SpikeLinkUSB3InitParams());
        }

        memcpy(params_.get(), initParams->opaque, sizeof(SpikeLinkUSB3InitParams));

        dataPool_.reset(new SpikeFramePool);

        if (dataPool_->Init(params_->packetSize, params_->queueSize * 30) != 0) {
            return (-1);
        }

    }
    return (0);
}

int32_t SpikeLinkUSB3::Init(SpikeLinkInitParams *initParams) {
    assert(initParams != nullptr);
    if (initParams == nullptr) {
        return (-1);
    }
    if (dataPool_ != nullptr) {
        return (1);
    }

    {
        unique_lock<mutex> lock(mtx_);
        initParams->picture.ppsSize = SPS10_002_PKT_HEADER_SIZE;
        if (SpikeLinkBaseInput::Init(initParams) != 0) {
            return (-1);
        }

        if (params_ == nullptr) {
            params_.reset(new SpikeLinkUSB3InitParams());
        }

        memcpy(params_.get(), initParams->opaque, sizeof(SpikeLinkUSB3InitParams));

        dataPool_.reset(new SpikeFramePool);
        if (dataPool_->Init(params_->packetSize, params_->queueSize * 30) != 0) {
            return (-1);
        }
       
    }

    return (0);
}

void SpikeLinkUSB3::Fini() {
    Stop();
    {
//        unique_lock<mutex> lock(mtx_);
        SpikeLinkBaseInput::Fini();
    }
}

int32_t SpikeLinkUSB3::Open() {
    if (readThrd_ != NULL || IsOpen()) {
        return (1);
    }

    int retv = false;
    {
        unique_lock<mutex> lock(mtx_);
        devInfo_.reset(new CDriverInterface());
        int retv = devInfo_->Initialize((EOPEN_BY)params_->type, (PVOID)params_->openByParam);
        if (retv == 0) {
            goto ErrExit;
        }
        retv = devInfo_->GetEP(&nReadEP_, &nWriteEP_, readEPs_, writeEPs_);
        if (retv == 0) {
            goto ErrExit;
        }

        state_ = OPENED;
    }

    return (0);

 ErrExit:           
    Close();
    return (retv);
}

int32_t SpikeLinkUSB3::Close() {
    int32_t retv = -1;
    {
        unique_lock<mutex> lock(mtx_);

        if (!IsOpen()) {
            return (retv);
        }

        if (state_ == _SpikeLinkInputState::STARTED) {
            retv = Stop();
        }

        state_ = _SpikeLinkInputState::CLOSED;

        if (readThrd_ != nullptr) {
            readThrd_->join();
        }

        if (decodeThrd_ != nullptr) {
            decodeThrd_->join();
        }

        devInfo_->Cleanup();
        devInfo_.reset(nullptr);
        params_.reset(nullptr);
        dataPool_.reset(nullptr);
        readThrd_.reset(nullptr);
        decodeThrd_.reset(nullptr);
    }
    return (retv);
}

int32_t SpikeLinkUSB3::Start() {
    int retv = -1;

    if (state_ == _SpikeLinkInputState::STARTED) {
        return (1);
    }

    {
        unique_lock<mutex> lock(mtx_);

        if (readThrd_ != nullptr || decodeThrd_ != nullptr) {
            return (retv);
        }

        state_ = STARTED;
        readThrd_.reset(new thread(&SpikeLinkUSB3::ReadSpikeThrd, this));
        decodeThrd_.reset(new thread(&SpikeLinkUSB3::DecodeThrd, this));
    }

    return (0);
}

int32_t SpikeLinkUSB3::Stop() {
    if (state_ != _SpikeLinkInputState::STARTED) {
        return (1);
    }

    state_ = OPENED;
    cond_.notify_all();
    {
        if (readThrd_ != nullptr) {
            readThrd_->join();
            readThrd_.reset(nullptr);
        }

        if (decodeThrd_ != nullptr) {
            decodeThrd_->join();
            decodeThrd_.reset(nullptr);
        }     
    }

    return (0);
}

void SpikeLinkUSB3::ReadSpikeThrd() {
    FT_STATUS ftStatus;
    FT_HANDLE ftHandle = devInfo_->m_FTHandle;
    
    ULONG nActualBytesTransferred = 0;
    ULONG nActualBytesToTransfer = params_->packetSize;
    SpikeLinkVideoFrame* frame = dataPool_->PopFrame(true);

    ftStatus = FT_SetStreamPipe(ftHandle, FALSE, FALSE, readEP_, nActualBytesToTransfer);
    if (FT_FAILED(ftStatus)) {
        state_ = OPENED;
        return;
    }

    uint8_t **ppBuffers = NULL;
    OVERLAPPED *pOverlapped = new OVERLAPPED[params_->queueSize];
    ppBuffers = new PUCHAR[params_->queueSize];

    for(uint32_t i = 0; i < params_->queueSize; i++) {
        ppBuffers[i] = new uint8_t[params_->packetSize];
        memset(&ppBuffers[i][0], 0x55, params_->packetSize);
        memset(&pOverlapped[i], 0, sizeof(OVERLAPPED));
        ftStatus = FT_InitializeOverlapped(ftHandle, &pOverlapped[i]);
        if (FT_FAILED(ftStatus)) {
            goto exit;
        }
    }

    for(uint32_t i = 0; i < params_->queueSize; i++) {
         ftStatus = FT_ReadPipeEx(ftHandle, readEP_, &ppBuffers[i][0], nActualBytesToTransfer, &nActualBytesTransferred, &pOverlapped[i]);
        if (ftStatus != FT_IO_PENDING) {
            goto exit;
        }
    }

    uint32_t i = 0;
    while (state_ == _SpikeLinkInputState::STARTED) {
        ftStatus = FT_GetOverlappedResult(ftHandle, &pOverlapped[i], &nActualBytesTransferred, TRUE);
        if (ftStatus == FT_DEVICE_NOT_CONNECTED) {
            goto exit;            
        } else if (FT_FAILED(ftStatus)) {           
            goto exit;  
        } 

        if (++i == params_->queueSize) {
			i = 0;
		}

        if (state_ == _SpikeLinkInputState::STARTED) {
            uint32_t j = (i == 0 ? params_->queueSize - 1 : i - 1);
            ftStatus = FT_ReadPipeEx(ftHandle, readEP_, &ppBuffers[j][0], nActualBytesToTransfer, &nActualBytesTransferred, &pOverlapped[j]);
            if (ftStatus != FT_IO_PENDING)
            {
                goto exit; 
            }
           // fwrite(ppBuffers[j], 1, ulActualBytesTransferred, fout);
            //fclose(fout);

            if (frame != nullptr) {
                memcpy(frame->data[0], ppBuffers[j], nActualBytesTransferred);
                frame->size = nActualBytesTransferred;
                dataPool_->PushFrame(frame, false);
                cond_.notify_one();
                frame = NULL;
            } else {
                printf("Drop frame\n");
            }

            frame = dataPool_->PopFrame(true);
        }
    }

exit:
    devInfo_->AbortPipe(readEP_);
    if (frame != nullptr) {
        dataPool_->PushFrame(frame, true);
    }

    cond_.notify_all();

    FT_ClearStreamPipe(ftHandle, FALSE, FALSE, readEP_);

    if (pOverlapped)
    {
        for (i = 0; i < params_->queueSize; i++)
        {
            FT_ReleaseOverlapped(ftHandle, &pOverlapped[i]);
        }
        delete[] pOverlapped;
    }

    if (ppBuffers)
    {
        for (i = 0; i < params_->queueSize; i++)
        {
            if (ppBuffers[i])
            {
                delete[] ppBuffers[i];
            }
        }
        delete[] ppBuffers;
    }
    // state_ = OPENED;

    cout << "exit ReadSpikeThrd" << endl;
}

static bool findFrameStartCode(uint8_t* pInData, int size, int height, uint8_t** pStart) {
    bool bRet = false;
    uint8_t* p = pInData;
    uint8_t* q = pInData + size;
    do {
        if(memcmp(p, SPS10_002_PKT_HEADER, 4) == 0) {
            bRet = true;
            *pStart = p;
            break;
        }
        p += 4;
    } while ( p < (q - SPS10_002_PKT_HEADER_SIZE));

    return (bRet);
}

FILE* fout = fopen("f:/usb3_16_out.dat", "wb");

void SpikeLinkUSB3::DecodeThrd() {   
    SpikeLinkVideoFrame* packet = nullptr;
    SpikeLinkVideoFrame* frame = nullptr;
    bool bFoundFreamHead = false;
    int32_t heigth = initParams_->picture.height;
    int32_t frameSize = SpikeFramePool::GetFrameSize2(initParams_->picture.format, initParams_->picture.width, initParams_->picture.height, SPS10_002_PKT_HEADER_SIZE);
    int32_t leftSize = 0;
    int64_t pts = 0;
    uint32_t prevId = 0, currId;
    int32_t pos = 0;
    frame = framePool_->PopFrame(true);
    while (state_ == STARTED) {
        {
            unique_lock<mutex> lock(mtx_);

            while (dataPool_->Size() <= 0 && state_ == STARTED) {
                cond_.wait(lock);
            }
            // if(dataPool_->Size() <= 0 && state_ == STARTED) {
            //     Sleep(1);
            //     continue;
            // }
        }
        if (state_ != STARTED) {
            break;
        }
#if 0
        // packet = dataPool_->PopFrame(false);
        // fwrite(packet->data[0], 1, packet->size, fout);
        packet = dataPool_->PopFrame(false);
        uint8_t* p = packet->data[0];
        uint8_t* q = p + packet->size;

        if (!bFoundFreamHead) {
            cout << packet->size << endl;
            if (!(bFoundFreamHead = findFrameStartCode(p, packet->size, heigth, &p))) {
                dataPool_->PushFrame(packet, true);
                packet = nullptr;
                cond_.notify_one();
                continue;
            }

            bFoundFreamHead = true;
        }

        do {
            leftSize = 0;

            if (frame->size + (q - p) <= frameSize) {
                memcpy(frame->data[0] + frame->size, p, (q - p));
                frame->size += (int32_t)(q - p);
            } else {
                memcpy(frame->data[0] + frame->size, p, frameSize - frame->size);
                p += (frameSize - frame->size);
                frame->size = frameSize;
                leftSize = (int32_t)(q - p);
            }

            if (frame->size < frameSize) {
                break;
            }

            currId = frame->data[0][12] | frame->data[0][13] << 8 | frame->data[0][14] << 16 | frame->data[0][15] << 24;
            if(memcmp(frame->data[0], SPS10_002_PKT_HEADER, 4) != 0) {
                bFoundFreamHead = false;
                frame->size = 0;
                cout << "err" << endl;
                break;
            }

            if(prevId == 0) {
                prevId = currId;
                frame->size = 0;
            } else if(prevId != currId) {
                frame->width = initParams_->picture.width;
                frame->height = initParams_->picture.height;
                frame->pts = frame->dts = pts++;
                // framePool_->PushFrame(frame, false);
                // frame = nullptr;
                // cond_.notify_one();

                prevId = currId;
                frame->size = 0;

                // frame = framePool_->PopFrame(true);
                // if(frame != nullptr) {
                //     frame->size = 0;
                // } else {
                //     bFoundFreamHead = false;
                //     prevId = 0;
                //     cout << "frame buff overflow" << endl;
                //     break;
                // }
                // cout << "one frame : " << pts << endl;
            } else {
                frame->size = 0;
            }
        } while (leftSize > 0);

        dataPool_->PushFrame(packet, true);
        cond_.notify_one();
        packet = nullptr;
#endif
#if 0        
        packet = dataPool_->PopFrame(false);
        uint8_t* p = packet->data[0];
        uint8_t* q = p + packet->size;
        uint8_t* r;
        while(findFrameStartCode(p, packet->size, heigth, &r)) {
            //cout << (r - p) + pos << endl;
            p = r + 4;
            pos = 0;
        }
        pos = q - p;
        cout << dataPool_->Size() << endl;
        dataPool_->PushFrame(packet, true);
        cond_.notify_one();
        packet = nullptr;
#endif 
#if 1
        packet = dataPool_->PopFrame(false);
        uint8_t* p = packet->data[0];
        uint8_t* q = p + packet->size;

        if (!bFoundFreamHead) {
            cout << packet->size << endl;
            if (!(bFoundFreamHead = findFrameStartCode(p, packet->size, heigth, &p))) {
                dataPool_->PushFrame(packet, true);
                packet = nullptr;
                cond_.notify_one();
                continue;
            }

            bFoundFreamHead = true;
        }
    
        if (frame == nullptr) {
            frame = framePool_->PopFrame(true);
            if(frame == nullptr) {
                dataPool_->PushFrame(packet, true);
                packet = nullptr;
                cond_.notify_one();
                bFoundFreamHead = false;
                prevId = 0;
                cout << "data buff overflow" << endl;
                continue;
            }
            frame->size = 0;
        }

        do {
            leftSize = 0;

            if (frame->size + (q - p) <= frameSize) {
                memcpy(frame->data[0] + frame->size, p, (q - p));
                frame->size += (int32_t)(q - p);
            } else {
                memcpy(frame->data[0] + frame->size, p, frameSize - frame->size);
                p += (frameSize - frame->size);
                frame->size = frameSize;
                leftSize = (int32_t)(q - p);
            }

            if (frame->size < frameSize) {
                break;
            }

            if(memcmp(frame->data[0], SPS10_002_PKT_HEADER, 4) != 0) {
                bFoundFreamHead = false;
                frame->size = 0;
                cout << "err" << endl;
                break;
            }

            currId = frame->data[0][12] | frame->data[0][13] << 8 | frame->data[0][14] << 16 | frame->data[0][15] << 24;
            if(prevId == 0) {
                prevId = currId;
                frame->size = 0;
            } else if(prevId != currId) {
                frame->width = initParams_->picture.width;
                frame->height = initParams_->picture.height;
                frame->pts = frame->dts = pts++;
                framePool_->PushFrame(frame, false);
                frame = nullptr;
                cond_.notify_one();

                prevId = currId;

                frame = framePool_->PopFrame(true);
                if(frame != nullptr) {
                    frame->size = 0;
                } else {
                    bFoundFreamHead = false;
                    prevId = 0;
                    cout << "frame buff overflow" << endl;
                    break;
                }
            } else {
                frame->size = 0;
            }
        } while (leftSize > 0);

        dataPool_->PushFrame(packet, true);
        cond_.notify_one();
        packet = nullptr;
#endif
    }

    if (packet != nullptr) {
        dataPool_->PushFrame(frame, true);
    }

    if (frame != nullptr) {
        framePool_->PushFrame(frame, true);
    }  
    cout << "exit DecodeThrd" << endl;
}

int32_t SpikeLinkUSB3::EnumerateDevice(int8_t *deviceName[], int32_t iDeviceNumMax, int32_t *pDeviceNum) {
    return CDriverInterface::EnumerateDevice(deviceName, iDeviceNumMax, pDeviceNum);
}
