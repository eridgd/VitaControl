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
    // Cap to avoid log spam / overly long reports
    size_t maxBytes = (length > 64) ? 64 : length;

    // Diff against last report so we can see which bytes change per button press
    bool anyChanges = false;
    if (hasLast && lastLen == maxBytes)
    {
        for (size_t i = 0; i < maxBytes; i++)
        {
            if (last[i] != buffer[i])
            {
                anyChanges = true;
                break;
            }
        }
    }

    // If we have no previous report, or the length changed, log a full snapshot once
    if (!hasLast || lastLen != maxBytes)
    {
        LOG("Report snapshot: id=0x%02X len=%u\n", buffer[0], (unsigned)maxBytes);
        LOG("Data: ");
        for (size_t i = 0; i < maxBytes; i++)
        {
            ksceDebugPrintf("%02X ", buffer[i]);
            if ((i + 1) % 16 == 0 && i < maxBytes - 1)
                ksceDebugPrintf("\n                 ");
        }
        ksceDebugPrintf("\n");
    }
    else if (anyChanges)
    {
        LOG("Delta: id=0x%02X\n", buffer[0]);
        // Print changed bytes as: [idx]=old->new
        LOG("Changed: ");
        bool first = true;
        for (size_t i = 0; i < maxBytes; i++)
        {
            if (last[i] == buffer[i]) continue;
            if (!first) ksceDebugPrintf(", ");
            ksceDebugPrintf("[%u]=%02X->%02X", (unsigned)i, last[i], buffer[i]);
            first = false;
        }
        ksceDebugPrintf("\n");
    }

    // For diagnostic purposes, try to interpret common patterns
    // Most controllers have buttons starting around byte 5-6 and sticks around byte 1-4
    if (maxBytes >= 8)
    {
        LOG("Axes? LX=%02X LY=%02X RX=%02X RY=%02X\n",
            buffer[1], buffer[2], buffer[3], buffer[4]);
        LOG("Buttons? b5=%02X b6=%02X b7=%02X\n",
            buffer[5], buffer[6], buffer[7]);
    }

    // Save current report as baseline for next diff
    for (size_t i = 0; i < maxBytes; i++)
        last[i] = buffer[i];
    lastLen = maxBytes;
    hasLast = true;

    // TODO: Once we understand the format, map buttons properly
    // For now, just clear the control data so we don't send garbage
    controlData.buttons = 0;
    controlData.leftX = 127;
    controlData.leftY = 127;
    controlData.rightX = 127;
    controlData.rightY = 127;
}

