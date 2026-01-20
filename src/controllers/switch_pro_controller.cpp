#include <psp2kern/ctrl.h>

#include "switch_pro_controller.h"

// Inline deadzone helper for Pro 3 analog sticks
static inline uint8_t applyDeadzone(uint8_t v, uint8_t center, uint8_t dz)
{
    int d = (int)v - (int)center;
    if (d < 0) d = -d;
    return (d <= dz) ? center : v;
}

SwitchProController::SwitchProController(uint32_t mac0, uint32_t mac1, int port): Controller(mac0, mac1, port)
{
    // Don't send any mode-switching writes here.
    // Some Switch-compatible controllers (e.g. 8BitDo Pro 3) can disconnect if we send the
    // "standard mode 0x30" command immediately on connect. We defer until we see input.
}

void SwitchProController::processReport(uint8_t *buffer, size_t length)
{
    if (length < 12)
        return;

    // If the device is not already sending standard reports, request standard mode once.
    // IMPORTANT: do NOT do this for 0x3F (8BitDo Pro 3 mapping) since it already works there.
    if (!requestedStandardMode && buffer[0] != 0x30 && buffer[0] != 0x3F)
    {
        static uint8_t report[12] = {};
        report[0]  = 0x01;
        report[1]  = 0x01;
        report[10] = 0x03;
        report[11] = 0x30;
        requestReport(HID_REQUEST_WRITE, report, sizeof(report));
        requestedStandardMode = true;
        // Continue; this report likely isn't useful anyway.
        return;
    }

    // 8BitDo Pro 3 (in Switch-compatible mode) can present with the same VID/PID as Switch Pro
    // but uses a different input report (0x3F). Handle it here.
    if (buffer[0] == 0x3F)
    {
        // Byte layout derived from vitacontrol_mapper_results.txt:
        //  b1=buf[1] face+shoulders+triggers (bitfield)
        //  b2=buf[2] select/start/home (bitfield)
        //  hat=buf[3] neutral=0x08; U=0x00 R=0x02 D=0x04 L=0x06 (diagonals likely 0x01/0x03/0x05/0x07)
        //  left stick appears to affect buf[4]/buf[5], right stick buf[8]/buf[9]
        const uint8_t b1  = buffer[1];
        const uint8_t b2  = buffer[2];
        const uint8_t hat = buffer[3];

        controlData.buttons = 0;

        // Face buttons (Nintendo layout -> Vita mapping, consistent with existing Switch Pro logic)
        // From capture: B=0x01, A=0x02, Y=0x04, X=0x08
        if (b1 & 0x01) controlData.buttons |= SCE_CTRL_CROSS;    // B
        if (b1 & 0x02) controlData.buttons |= SCE_CTRL_CIRCLE;   // A
        if (b1 & 0x08) controlData.buttons |= SCE_CTRL_TRIANGLE; // X
        if (b1 & 0x04) controlData.buttons |= SCE_CTRL_SQUARE;   // Y

        // D-pad / hat
        switch (hat)
        {
            case 0x00: controlData.buttons |= SCE_CTRL_UP; break;
            case 0x01: controlData.buttons |= (SCE_CTRL_UP | SCE_CTRL_RIGHT); break;
            case 0x02: controlData.buttons |= SCE_CTRL_RIGHT; break;
            case 0x03: controlData.buttons |= (SCE_CTRL_RIGHT | SCE_CTRL_DOWN); break;
            case 0x04: controlData.buttons |= SCE_CTRL_DOWN; break;
            case 0x05: controlData.buttons |= (SCE_CTRL_DOWN | SCE_CTRL_LEFT); break;
            case 0x06: controlData.buttons |= SCE_CTRL_LEFT; break;
            case 0x07: controlData.buttons |= (SCE_CTRL_LEFT | SCE_CTRL_UP); break;
            default: break; // 0x08 neutral
        }

        // Shoulders / triggers from capture:
        // L1=0x10 R1=0x20 L2=0x40 R2=0x80
        if (b1 & 0x10) controlData.buttons |= SCE_CTRL_L1;
        if (b1 & 0x20) controlData.buttons |= SCE_CTRL_R1;
        if (b1 & 0x40) controlData.buttons |= SCE_CTRL_LTRIGGER;
        if (b1 & 0x80) controlData.buttons |= SCE_CTRL_RTRIGGER;

        // Start/Select/Home from capture: select=0x01 start=0x02 home=0x10
        if (b2 & 0x02) controlData.buttons |= SCE_CTRL_START;
        if (b2 & 0x01) controlData.buttons |= SCE_CTRL_SELECT;
        if (b2 & 0x10) controlData.buttons |= SCE_CTRL_PSBUTTON;

        // Stick mapping (0x3F):
        //
        // The stable interpretation for Pro 3 is 16-bit little-endian axes:
        //   LX = b4 | (b5 << 8)
        //   LY = b6 | (b7 << 8)
        //   RX = b8 | (b9 << 8)
        //   RY = b10 | (b11 << 8)
        //
        // At rest these MSBs sit around 0x80 (center), matching Vita expectations.
        // We use only the MSB (high byte) as it's stable; the LSB has jitter.
        // Direct MSB reads avoid unnecessary 16-bit construction and shifting.
        const uint8_t lx = buffer[5];   // MSB of LX
        const uint8_t ly = buffer[7];   // MSB of LY
        const uint8_t rx = buffer[9];   // MSB of RX
        const uint8_t ry = buffer[11];  // MSB of RY

        const uint8_t dz = 3;
        controlData.leftX  = applyDeadzone(lx, 0x80, dz);
        controlData.leftY  = applyDeadzone(ly, 0x80, dz);
        controlData.rightX = applyDeadzone(rx, 0x80, dz);
        controlData.rightY = applyDeadzone(ry, 0x80, dz);

        // NOTE: L3/R3 click bits weren't cleanly isolated in the captures (axis bytes changed too),
        // so we don't map stick clicks yet to avoid false positives. We can add them after one more
        // mapper run that presses L3/R3 without touching the stick.

        return;
    }

    // Only process the standard Switch Pro report if it's of the right type
    if (buffer[0] != 0x30)
    {
        // Unknown report type for this VID/PID; ignore.
        return;
    }

    // Interpret the data as an input report
    SwitchProReport0x30 *report = (SwitchProReport0x30*)buffer;

    // Clear the old control data
    controlData.buttons = 0;

    // Map the face buttons
    if (report->b) controlData.buttons |= SCE_CTRL_CROSS;
    if (report->a) controlData.buttons |= SCE_CTRL_CIRCLE;
    if (report->x) controlData.buttons |= SCE_CTRL_TRIANGLE;
    if (report->y) controlData.buttons |= SCE_CTRL_SQUARE;

    // Map the D-pad
    if (report->up)    controlData.buttons |= SCE_CTRL_UP;
    if (report->right) controlData.buttons |= SCE_CTRL_RIGHT;
    if (report->down)  controlData.buttons |= SCE_CTRL_DOWN;
    if (report->left)  controlData.buttons |= SCE_CTRL_LEFT;

    // Map the triggers
    if (report->l)      controlData.buttons |= SCE_CTRL_L1;
    if (report->r)      controlData.buttons |= SCE_CTRL_R1;
    if (report->zl)     controlData.buttons |= SCE_CTRL_LTRIGGER;
    if (report->zr)     controlData.buttons |= SCE_CTRL_RTRIGGER;
    if (report->stickL) controlData.buttons |= SCE_CTRL_L3;
    if (report->stickR) controlData.buttons |= SCE_CTRL_R3;

    // Map the menu buttons
    if (report->plus)  controlData.buttons |= SCE_CTRL_START;
    if (report->minus) controlData.buttons |= SCE_CTRL_SELECT;
    if (report->home)  controlData.buttons |= SCE_CTRL_PSBUTTON;

    // Map the extra buttons
    if (report->capture) controlData.buttons |= SCE_CTRL_EXT1;

    // Map the sticks
    controlData.leftX  = report->leftX  >> 4;
    controlData.leftY  = report->leftY  >> 4;
    controlData.rightX = report->rightX >> 4;
    controlData.rightY = report->rightY >> 4;

    // Reverse up and down
    controlData.leftY  = 255 - controlData.leftY;
    controlData.rightY = 255 - controlData.rightY;

    // Map the motion controls
    motionState.accelerX  = report->accelerX;
    motionState.accelerY  = report->accelerY;
    motionState.accelerZ  = report->accelerZ;
    motionState.velocityX = report->velocityX;
    motionState.velocityY = report->velocityY;
    motionState.velocityZ = report->velocityZ;

    // TODO: implement battery level
}
