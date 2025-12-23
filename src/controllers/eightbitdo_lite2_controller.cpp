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
    // Expected input report id for this controller mode.
    if (length < 8 || buffer[0] != 0x01)
        return;

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

        // Note: file-based raw logging is now handled centrally in bluetoothCallback so it
        // works for any controller. We keep kernel printf diagnostics here only.
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

    // --- Actual mapping (from your lite2_mapper_results.txt) ---
    //
    // report[1] (b1): face + shoulders + home
    //   0x01=A, 0x02=B, 0x08=X, 0x10=Y, 0x40=L1, 0x80=R1, 0x04=HOME
    // report[2] (b2): triggers + start/select
    //   0x01=L2, 0x02=R2, 0x04=SELECT, 0x08=START
    // report[3] (b3): dpad/hat: neutral=0x80; U=0x00, R=0x20, D=0x40, L=0x60 (diagonals appear to be +0x10 steps)
    // report[4..7]: analog-ish bytes (best-effort): LY=b4, LX=b5, RX=b6, RY=b7
    const uint8_t b1 = buffer[1];
    const uint8_t b2 = buffer[2];
    const uint8_t hat = buffer[3];

    controlData.buttons = 0;

    // Face buttons
    // Empirically on this controller/mode, A/B and X/Y labels are swapped relative to Vita's
    // expected layout, so we map accordingly.
    if (b1 & 0x01) controlData.buttons |= SCE_CTRL_CIRCLE;   // FACE_A
    if (b1 & 0x02) controlData.buttons |= SCE_CTRL_CROSS;    // FACE_B
    if (b1 & 0x08) controlData.buttons |= SCE_CTRL_TRIANGLE; // FACE_X
    if (b1 & 0x10) controlData.buttons |= SCE_CTRL_SQUARE;   // FACE_Y

    // Shoulders / triggers
    //
    // Vita's primary physical shoulders are exposed as LTRIGGER/RTRIGGER.
    // Since the Lite 2's *big* shoulders are L2/R2, map those to Vita's primary shoulders.
    // Keep Lite 2 L1/R1 as secondary shoulders (Vita L1/R1).
    if (b1 & 0x40) controlData.buttons |= SCE_CTRL_L1;       // Lite2 L1 (small)
    if (b1 & 0x80) controlData.buttons |= SCE_CTRL_R1;       // Lite2 R1 (small)
    if (b2 & 0x01) controlData.buttons |= SCE_CTRL_LTRIGGER; // Lite2 L2 (big)
    if (b2 & 0x02) controlData.buttons |= SCE_CTRL_RTRIGGER; // Lite2 R2 (big)

    // Start / Select
    if (b2 & 0x08) controlData.buttons |= SCE_CTRL_START;
    if (b2 & 0x04) controlData.buttons |= SCE_CTRL_SELECT;

    // Home -> PS button
    if (b1 & 0x04) controlData.buttons |= SCE_CTRL_PSBUTTON;

    // D-pad / hat mapping (including diagonals using 0x10 steps)
    switch (hat)
    {
        case 0x00: controlData.buttons |= SCE_CTRL_UP; break;
        case 0x10: controlData.buttons |= (SCE_CTRL_UP | SCE_CTRL_RIGHT); break;
        case 0x20: controlData.buttons |= SCE_CTRL_RIGHT; break;
        case 0x30: controlData.buttons |= (SCE_CTRL_RIGHT | SCE_CTRL_DOWN); break;
        case 0x40: controlData.buttons |= SCE_CTRL_DOWN; break;
        case 0x50: controlData.buttons |= (SCE_CTRL_DOWN | SCE_CTRL_LEFT); break;
        case 0x60: controlData.buttons |= SCE_CTRL_LEFT; break;
        case 0x70: controlData.buttons |= (SCE_CTRL_LEFT | SCE_CTRL_UP); break;
        default: break; // 0x80 (neutral) or unknown
    }

    // Analog mapping
    //
    // Empirically, the left stick axes bytes are b4 (X) and b5 (Y). Our previous
    // implementation swapped them, resulting in a 90° rotation (Up->Right, etc).
    // Map directly; if any axis is still inverted on-device, we can invert just that axis.
    controlData.leftX  = buffer[4];
    controlData.leftY  = buffer[5];
    controlData.rightX = buffer[6];
    controlData.rightY = buffer[7];
}

