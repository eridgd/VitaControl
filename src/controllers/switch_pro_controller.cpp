#include <psp2kern/ctrl.h>

#include "switch_pro_controller.h"

SwitchProController::SwitchProController(uint32_t mac0, uint32_t mac1, int port): Controller(mac0, mac1, port)
{
    // Prepare a write request to switch to standard mode
    static uint8_t report[12] = {};
    report[0]  = 0x01;
    report[1]  = 0x01;
    report[10] = 0x03;
    report[11] = 0x30;

    // Send the write request
    requestReport(HID_REQUEST_WRITE, report, sizeof(report));
}

void SwitchProController::processReport(uint8_t *buffer, size_t length)
{
    if (length < 12)
        return;

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

        // Stick mapping (initial best-effort).
        // (We'll refine once basic controls are confirmed stable on-device.)
        const uint8_t leftX  = (uint8_t)(buffer[4] + 0x80); // signed -> unsigned-ish
        const uint8_t leftY  = buffer[5];                   // 0x80-centered
        const uint8_t rightX = (uint8_t)(buffer[8] + 0x80); // signed -> unsigned-ish
        const uint8_t rightY = buffer[9];                   // 0x80-centered

        controlData.leftX  = leftX;
        controlData.leftY  = (uint8_t)(0xFF - leftY);
        controlData.rightX = rightX;
        controlData.rightY = (uint8_t)(0xFF - rightY);

        // R3 appears to set bit 0x10 in byte 10 in your capture
        if (buffer[10] & 0x10) controlData.buttons |= SCE_CTRL_R3;

        return;
    }

    // Only process the standard Switch Pro report if it's of the right type
    if (buffer[0] != 0x30)
        return;

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
