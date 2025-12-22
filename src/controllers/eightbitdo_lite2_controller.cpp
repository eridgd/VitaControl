#include <psp2kern/ctrl.h>
#include <psp2kern/kernel/debug.h>

#include "eightbitdo_lite2_controller.h"

// Logging function declaration
extern "C" {
    int ksceDebugPrintf(const char *fmt, ...);
}

// Logging macro
#define LOG(...) ksceDebugPrintf("[8BitDo Lite 2] " __VA_ARGS__)

EightBitDoLite2Controller::EightBitDoLite2Controller(uint32_t mac0, uint32_t mac1, int port): Controller(mac0, mac1, port)
{
    LOG("Controller initialized\n");
    // No initialization needed for now - we're in diagnostic mode
}

void EightBitDoLite2Controller::processReport(uint8_t *buffer, size_t length)
{
    // Log the report ID
    LOG("Report ID: 0x%02X, Length: %d\n", buffer[0], (int)length);
    
    // Log all bytes in the report (up to 64 bytes to avoid spam)
    int maxBytes = (length > 64) ? 64 : (int)length;
    LOG("Full report (%d bytes): ", maxBytes);
    for (int i = 0; i < maxBytes; i++)
    {
        ksceDebugPrintf("%02X ", buffer[i]);
        if ((i + 1) % 16 == 0 && i < maxBytes - 1)
            ksceDebugPrintf("\n                           ");
    }
    ksceDebugPrintf("\n");

    // For diagnostic purposes, try to interpret common patterns
    // Most controllers have buttons starting around byte 5-6 and sticks around byte 1-4
    if (length >= 8)
    {
        LOG("Potential stick data: LX=%02X LY=%02X RX=%02X RY=%02X\n", 
            buffer[1], buffer[2], buffer[3], buffer[4]);
        LOG("Potential button bytes: %02X %02X %02X\n", 
            buffer[5], buffer[6], buffer[7]);
    }

    // TODO: Once we understand the format, map buttons properly
    // For now, just clear the control data so we don't send garbage
    controlData.buttons = 0;
    controlData.leftX = 127;
    controlData.leftY = 127;
    controlData.rightX = 127;
    controlData.rightY = 127;
}

