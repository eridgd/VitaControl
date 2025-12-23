#ifndef VITACONTROL_FILELOG_H
#define VITACONTROL_FILELOG_H

#include <stddef.h>

// Kernel-side log file writer used by diagnostic controllers.
// Writes raw-ish, parseable lines to ux0:data so a userland app can collect them.
#ifdef __cplusplus
extern "C" {
#endif

void vitacontrolFileLogWrite(const char *buf, size_t len);

#ifdef __cplusplus
}
#endif

#endif // VITACONTROL_FILELOG_H


