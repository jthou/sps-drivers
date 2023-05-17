#include <stdio.h>
#include "FTD3XX.h"
#include <string>
#include <memory>
#include <iostream>

int main() {
    FT_STATUS status = FT_OK;

    // Get the number of connected devices
    DWORD dwNumDevices = 0;
    status = FT_ListDevices(&dwNumDevices, NULL, FT_LIST_NUMBER_ONLY);
    if (status != FT_OK) {
        fprintf(stderr, "FT_ListDevices failed (error code %d)\n", (int)status);
        return 1;
    }
    fprintf(stdout, "Number of devices is %d\n", (int)dwNumDevices);
    if (dwNumDevices == 0) {
        fprintf(stderr, "No devices connected\n");
        return 1;
    }

    FT_HANDLE handle = NULL;
    // Initialize a FTD3XX context
    status = FT_Create(0, FT_OPEN_BY_INDEX, &handle);
    if (status != FT_OK) {
        fprintf(stderr, "Failed to create FTD3XX context: %d\n", status);
        if (status == FT_DEVICE_NOT_FOUND) {
            fprintf(stderr, "Device not found\n");
        }
        return 1;
    }

    USHORT uwVID = 0;
    USHORT uwPID = 0;
    status = FT_GetVIDPID(handle, &uwVID, &uwPID);
    if (status != FT_OK) {
        fprintf(stderr, "Failed to get VID/PID: %d\n", status);
        return 1;
    }
    fprintf(stdout, "VID: %04X, PID: %04X\n", uwVID, uwPID);

    // Get EP
    FT_DEVICE_DESCRIPTOR deviceDescriptor;
    FT_CONFIGURATION_DESCRIPTOR configDescriptor;
    FT_INTERFACE_DESCRIPTOR interfaceDescriptor;
    FT_PIPE_INFORMATION pipeInfo;
    DWORD dwReadPipeId = 0;
    ZeroMemory(&deviceDescriptor, sizeof(FT_DEVICE_DESCRIPTOR));
    ZeroMemory(&configDescriptor, sizeof(FT_CONFIGURATION_DESCRIPTOR));
    ZeroMemory(&interfaceDescriptor, sizeof(FT_INTERFACE_DESCRIPTOR));
    ZeroMemory(&pipeInfo, sizeof(FT_PIPE_INFORMATION));

    status = FT_GetDeviceDescriptor(handle, &deviceDescriptor);
    if (status != FT_OK) {
        fprintf(stderr, "Failed to get device descriptor: %d\n", status);
        return 1;
    }

    status = FT_GetConfigurationDescriptor(handle, &configDescriptor);
    if (status != FT_OK) {
        fprintf(stderr, "Failed to get configuration descriptor: %d\n", status);
        return 1;
    }

    if (configDescriptor.bNumInterfaces == 0) {
        fprintf(stderr, "No interface found\n");
        return 1;
    } else if (configDescriptor.bNumInterfaces == 1) {
        fprintf(stdout, "Found 1 interface\n");
        status = FT_GetInterfaceDescriptor(handle, 0, &interfaceDescriptor);
    } else {
        fprintf(stdout, "Found %d interfaces\n", configDescriptor.bNumInterfaces);
        for (int i = 0; i < configDescriptor.bNumInterfaces; i++) {
            status = FT_GetInterfaceDescriptor(handle, i, &interfaceDescriptor);
            if (status != FT_OK) {
                fprintf(stderr, "Failed to get interface descriptor: %d\n", status);
                return 1;
            }
            fprintf(stdout, "Interface %d: %d endpoints\n", i, interfaceDescriptor.bNumEndpoints);
            for (int j = 0; j < interfaceDescriptor.bNumEndpoints; j++) {
                status = FT_GetPipeInformation(handle, i, j, &pipeInfo);
                if (status != FT_OK) {
                    fprintf(stderr, "Failed to get pipe information: %d\n", status);
                    return 1;
                }
                if (FT_IS_READ_PIPE(pipeInfo.PipeId)) {
                    fprintf(stdout, "Read pipe: 0x%02X\n", pipeInfo.PipeId);
                    dwReadPipeId = pipeInfo.PipeId;
                } else if (FT_IS_WRITE_PIPE(pipeInfo.PipeId)) {
                    fprintf(stdout, "Write pipe: 0x%02X\n", pipeInfo.PipeId);
                } else {
                    fprintf(stdout, "Unknown pipe: 0x%02X\n", pipeInfo.PipeId);
                }
            }
        }
    }

    if (dwReadPipeId == 0) {
        fprintf(stderr, "No read pipe found\n");
        return 1;
    }

    // Set the read timeout to 1000ms
    FT_SetPipeTimeout(handle, dwReadPipeId, 1000);

    // Allocate a buffer for reading data
    const DWORD bufferSize = 256*1024;
    std::unique_ptr<UCHAR[]> rxBuffer(new UCHAR[bufferSize]);

    const int NUM_BUFFERS = 16;
    const int BUFFER_SIZE = 256*1024;

    std::unique_ptr<unsigned char[]>acBuf(new unsigned char [NUM_BUFFERS * BUFFER_SIZE]);
    ULONG ulBytesTransferred[NUM_BUFFERS] = {0};
    OVERLAPPED vOverlapped[NUM_BUFFERS] = {0};

    for (int i=0; i<NUM_BUFFERS; i++) {
        status = FT_InitializeOverlapped(handle, &vOverlapped[i]);
    }

    status = FT_SetStreamPipe(handle, FALSE, FALSE, dwReadPipeId, BUFFER_SIZE);
    // Queue up the initial batch of requests
    for (int i=0; i<NUM_BUFFERS; i++) {
        status = FT_ReadPipeEx(handle, dwReadPipeId, &acBuf[i*BUFFER_SIZE], BUFFER_SIZE, &ulBytesTransferred[i], &vOverlapped[i]);
    }

    int i=0;
    while (1) {
        // Wait for transfer to finish
        status = FT_GetOverlappedResult(handle, &vOverlapped[i], &ulBytesTransferred[i], TRUE);
        printf("status = %d\n", status);
        if (++i == NUM_BUFFERS) {
            printf("all data has been transferred\n");
            break;
        }
    }
    for (int i=0; i<NUM_BUFFERS; i++) {
        FT_ReleaseOverlapped(handle, &vOverlapped[i]);
    }

    FT_Close(handle);
    return 0;
}