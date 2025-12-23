#include <psp2kern/ctrl.h>
#include <psp2kern/kernel/debug.h>

#include "eightbitdo_lite2_controller.h"
#include "../vitacontrol_filelog.h"

// Logging function declaration
extern "C" {
    int ksceDebugPrintf(const char *fmt, ...);
}

// Logging macro
#define LOG(...) ksceDebugPrintf("[8BitDo Lite 2] " __VA_ARGS__)

static inline void appendHex2(char *&p, unsigned v)
{
    static const char *hex = "0123456789ABCDEF";
    *p++ = hex[(v >> 4) & 0xF];
    *p++ = hex[(v >> 0) & 0xF];
}

static inline void appendStr(char *&p, const char *s)
{
    while (*s) *p++ = *s++;
}

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
    bool didLog = false;
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
        didLog = true;
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
        didLog = true;

        // Extra: if bytes 5-7 change, show bit-level deltas (common for button fields)
        if (maxBytes >= 8)
        {
            for (size_t i = 5; i <= 7; i++)
            {
                if (last[i] == buffer[i]) continue;
                uint8_t oldv = last[i];
                uint8_t newv = buffer[i];
                uint8_t pressed = (uint8_t)(~oldv & newv);   // bits that went 0->1
                uint8_t released = (uint8_t)(oldv & ~newv);  // bits that went 1->0
                LOG("Bits[%u]: old=%02X new=%02X pressed=%02X released=%02X\n",
                    (unsigned)i, oldv, newv, pressed, released);
            }
        }

        // Write a compact, parseable line for the mapping app.
        // Format:
        // id=01 b1=.. b2=.. b3=.. b4=.. b5=.. b6=.. b7=.. ch=[idx:old>new,...]\n
        char line[256];
        char *p = line;
        appendStr(p, "id=");
        appendHex2(p, buffer[0]);
        if (maxBytes >= 8)
        {
            appendStr(p, " b1="); appendHex2(p, buffer[1]);
            appendStr(p, " b2="); appendHex2(p, buffer[2]);
            appendStr(p, " b3="); appendHex2(p, buffer[3]);
            appendStr(p, " b4="); appendHex2(p, buffer[4]);
            appendStr(p, " b5="); appendHex2(p, buffer[5]);
            appendStr(p, " b6="); appendHex2(p, buffer[6]);
            appendStr(p, " b7="); appendHex2(p, buffer[7]);
        }
        appendStr(p, " ch=[");
        bool firstCh = true;
        for (size_t i = 0; i < maxBytes; i++)
        {
            if (last[i] == buffer[i]) continue;
            if (!firstCh) *p++ = ',';
            // idx (decimal, 0-63)
            if (i >= 10) *p++ = (char)('0' + (i / 10));
            *p++ = (char)('0' + (i % 10));
            *p++ = ':';
            appendHex2(p, last[i]);
            *p++ = '>';
            appendHex2(p, buffer[i]);
            firstCh = false;
        }
        *p++ = ']';
        *p++ = '\n';
        vitacontrolFileLogWrite(line, (size_t)(p - line));
    }

    // Only print the quick interpretation when something changed / snapshot was taken.
    // (Avoid spamming identical lines when no deltas occur.)
    if (didLog && maxBytes >= 8)
    {
        LOG("Fields: b1=%02X b2=%02X b3=%02X b4=%02X b5=%02X b6=%02X b7=%02X\n",
            buffer[1], buffer[2], buffer[3], buffer[4], buffer[5], buffer[6], buffer[7]);
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

