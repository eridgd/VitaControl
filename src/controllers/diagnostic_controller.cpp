#include <cstring>
#include <psp2kern/bt.h>

#include "diagnostic_controller.h"
#include "../vitacontrol_filelog.h"

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

DiagnosticController::DiagnosticController(uint32_t mac0, uint32_t mac1, int port): Controller(mac0, mac1, port)
{
    uint16_t id[2];
    ksceBtGetVidPid(mac0, mac1, id);

    char line[32];
    char *p = line;
    appendStr(p, "VIDPID=");
    appendHex2(p, id[0] >> 8); appendHex2(p, id[0] & 0xFF);
    *p++ = ':';
    appendHex2(p, id[1] >> 8); appendHex2(p, id[1] & 0xFF);
    *p++ = '\n';
    vitacontrolFileLogWrite(line, (size_t)(p - line));
}

void DiagnosticController::processReport(uint8_t *buffer, size_t length)
{
    size_t maxBytes = (length > sizeof(last)) ? sizeof(last) : length;
    if (maxBytes == 0)
        return;

    if (!hasLast)
    {
        memcpy(last, buffer, maxBytes);
        hasLast = true;
        return; // seed baseline only; first line comes from the next delta
    }

    bool any = false;
    for (size_t i = 0; i < maxBytes; i++)
    {
        if (last[i] != buffer[i]) { any = true; break; }
    }
    if (!any)
        return;

    // Format: id=.. ch=[idx:old>new,...] raw=.. .. ..
    char line[512];
    char *p = line;
    appendStr(p, "id=");
    appendHex2(p, buffer[0]);

    appendStr(p, " ch=[");
    bool first = true;
    for (size_t i = 0; i < maxBytes; i++)
    {
        if (last[i] == buffer[i]) continue;
        if (!first) *p++ = ',';
        if (i >= 10) *p++ = (char)('0' + (i / 10));
        *p++ = (char)('0' + (i % 10));
        *p++ = ':';
        appendHex2(p, last[i]);
        *p++ = '>';
        appendHex2(p, buffer[i]);
        first = false;
    }
    *p++ = ']';

    appendStr(p, " raw=");
    for (size_t i = 0; i < maxBytes; i++)
    {
        appendHex2(p, buffer[i]);
        if (i + 1 < maxBytes) *p++ = ' ';
    }
    *p++ = '\n';

    vitacontrolFileLogWrite(line, (size_t)(p - line));

    memcpy(last, buffer, maxBytes);
}
