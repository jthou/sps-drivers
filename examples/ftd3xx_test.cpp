#include <stdio.h>
#include "FTD3XX.h"
#include <string>

#define BUFFER_SIZE 1024

int main()
{
    FT_STATUS status = FT_OK;

    // Get the number of connected devices
    DWORD dwNumDevices = 0;
    status = FT_ListDevices(&dwNumDevices, NULL, FT_LIST_NUMBER_ONLY);
    if (status != FT_OK)
    {
        fprintf(stderr, "FT_ListDevices failed (error code %d)\n", (int)status);
        return 1;
    }
    fprintf(stdout, "Number of devices is %d\n", (int)dwNumDevices);
    if (dwNumDevices == 0)
    {
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

    FT_60XCONFIGURATION oConfig = { 0 };
    status = FT_GetChipConfiguration(handle, &oConfig);
    if (status != FT_OK) {
        fprintf(stderr, "Failed to get chip configuration: %d\n", status);
        return 1;
    }

    if (oConfig.FIFOMode == CONFIGURATION_FIFO_MODE_245) {
        fprintf(stdout, "FIFO mode: 245\n");

        //// configure the GPIOs to OUTPUT mode
        //status = FT_EnableGPIO(handle, (FT_GPIO_DIRECTION_OUT << FT_GPIO_0) | (FT_GPIO_DIRECTION_OUT << FT_GPIO_1),
        //    (FT_GPIO_DIRECTION_OUT << FT_GPIO_0) | (FT_GPIO_DIRECTION_OUT << FT_GPIO_1));
        //if (status != FT_OK) {
        //    fprintf(stderr, "Failed to configure GPIOs: %d\n", status);
        //    return 1;
        //}

        //// write to GPIOs: set the pins to LOW
        //status = FT_WriteGPIO(handle, (FT_GPIO_VALUE_HIGH << FT_GPIO_0) | (FT_GPIO_VALUE_HIGH << FT_GPIO_1),
        //    (FT_GPIO_VALUE_LOW << FT_GPIO_0) | (FT_GPIO_VALUE_LOW << FT_GPIO_1));
        //if (status != FT_OK) {
        //    fprintf(stderr, "Failed to write GPIOs: %d\n", status);
        //    return 1;
        //}
    }
    else if (oConfig.FIFOMode == CONFIGURATION_FIFO_MODE_600) {
        fprintf(stdout, "FIFO mode: 600\n");
    }
    else {
        fprintf(stdout, "FIFO mode: unknown\n");
    }


    // Close the FTD3XX context
    FT_Close(handle);
    return 0;
}