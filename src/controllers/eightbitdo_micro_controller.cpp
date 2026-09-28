#include <psp2kern/ctrl.h>

#include "eightbitdo_micro_controller.h"

EightBitDoMicroController::EightBitDoMicroController(uint32_t mac0, uint32_t mac1, int port): Controller(mac0, mac1, port)
{
    // This controller has no analog sticks. Stick offsets are additive to the Vita's own
    // physical sticks (see main.cpp), so leave them at neutral (127) rather than the
    // ControlData default of 0, which would otherwise drag the real sticks to one extreme.
    controlData.leftX  = 127;
    controlData.leftY  = 127;
    controlData.rightX = 127;
    controlData.rightY = 127;
}

void EightBitDoMicroController::processReport(uint8_t *buffer, size_t length)
{
    // Expected input report id for this controller (D-input mode).
    if (length < 10 || buffer[0] != 0x03)
        return;

    // report[8]: face buttons + shoulders (bitfield)
    //   0x01=A, 0x02=B, 0x08=X, 0x10=Y, 0x40=L, 0x80=R
    // report[9]: menu buttons (bitfield)
    //   0x04=SELECT, 0x08=START, 0x10=HOME
    // report[1]: dpad/hat: neutral=0x08; U=0x00 R=0x02 D=0x04 L=0x06 (diagonals untested)
    const uint8_t b8  = buffer[8];
    const uint8_t b9  = buffer[9];
    const uint8_t hat = buffer[1];

    controlData.buttons = 0;

    // Face buttons (Nintendo layout -> Vita mapping, consistent with the Lite2/Switch Pro)
    if (b8 & 0x01) controlData.buttons |= SCE_CTRL_CIRCLE;   // A
    if (b8 & 0x02) controlData.buttons |= SCE_CTRL_CROSS;    // B
    if (b8 & 0x08) controlData.buttons |= SCE_CTRL_TRIANGLE; // X
    if (b8 & 0x10) controlData.buttons |= SCE_CTRL_SQUARE;   // Y

    // Shoulders: this controller only has one L/R pair, and the base Vita's physical
    // shoulders are SCE_CTRL_LTRIGGER/RTRIGGER, not L1/R1 (see Lite2's big-shoulder mapping).
    if (b8 & 0x40) controlData.buttons |= SCE_CTRL_LTRIGGER;
    if (b8 & 0x80) controlData.buttons |= SCE_CTRL_RTRIGGER;

    // Start / Select
    if (b9 & 0x08) controlData.buttons |= SCE_CTRL_START;
    if (b9 & 0x04) controlData.buttons |= SCE_CTRL_SELECT;

    // Home -> PS button
    if (b9 & 0x10) controlData.buttons |= SCE_CTRL_PSBUTTON;

    // D-pad / hat mapping
    switch (hat)
    {
        case 0x00: controlData.buttons |= SCE_CTRL_UP;    break;
        case 0x02: controlData.buttons |= SCE_CTRL_RIGHT; break;
        case 0x04: controlData.buttons |= SCE_CTRL_DOWN;  break;
        case 0x06: controlData.buttons |= SCE_CTRL_LEFT;  break;
        default: break; // 0x08 neutral (diagonals untested)
    }
}
