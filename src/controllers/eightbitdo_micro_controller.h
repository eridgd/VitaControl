#ifndef EIGHTBITDO_MICRO_CONTROLLER_H
#define EIGHTBITDO_MICRO_CONTROLLER_H

#include "../controller.h"

class EightBitDoMicroController: public Controller
{
    public:
        EightBitDoMicroController(uint32_t mac0, uint32_t mac1, int port);

        void processReport(uint8_t *buffer, size_t length);
};

#endif // EIGHTBITDO_MICRO_CONTROLLER_H
