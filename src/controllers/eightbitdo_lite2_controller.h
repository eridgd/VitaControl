#ifndef EIGHTBITDO_LITE2_CONTROLLER_H
#define EIGHTBITDO_LITE2_CONTROLLER_H

#include "../controller.h"

class EightBitDoLite2Controller: public Controller
{
    public:
        EightBitDoLite2Controller(uint32_t mac0, uint32_t mac1, int port);

        void processReport(uint8_t *buffer, size_t length);

    private:
        bool hasLast = false;
        size_t lastLen = 0;
        uint8_t last[64] = {};
};

#endif // EIGHTBITDO_LITE2_CONTROLLER_H

