#ifndef DIAGNOSTIC_CONTROLLER_H
#define DIAGNOSTIC_CONTROLLER_H

#include "../controller.h"

// Fallback for any VID/PID that doesn't match a known controller. Logs the device's VID/PID
// plus raw HID report deltas to file so an unrecognized controller can be mapped without a
// debugger. Never populates buttons/sticks, so it has no effect on actual Vita input.
class DiagnosticController: public Controller
{
    public:
        DiagnosticController(uint32_t mac0, uint32_t mac1, int port);

        void processReport(uint8_t *buffer, size_t length);

    private:
        bool hasLast = false;
        uint8_t last[32] = {};
};

#endif // DIAGNOSTIC_CONTROLLER_H
