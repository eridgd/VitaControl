#include <cmath>
#include <cstring>
#include <taihen.h>
#include <psp2/motion.h>
#include <psp2/touch.h>
#include <psp2kern/bt.h>
#include <psp2kern/ctrl.h>
#include <psp2kern/kernel/modulemgr.h>
#include <psp2kern/kernel/suspend.h>
#include <psp2kern/kernel/threadmgr.h>
#include <psp2kern/kernel/debug.h>
#include <psp2kern/io/fcntl.h>
#include <psp2kern/io/stat.h>

#include "controller.h"
#include "mempool.h"
#include "vitacontrol_filelog.h"

// Logging function declaration
extern "C" {
    int ksceDebugPrintf(const char *fmt, ...);
}

// Logging macro
#define LOG(...) ksceDebugPrintf("[VitaControl] " __VA_ARGS__)

#define MAX_CONTROLLERS 4

#define TOUCHSCREEN_WIDTH  1920
#define TOUCHSCREEN_HEIGHT 1080

#define FLAG_EXIT (1 << 0)

#define AXIS_MOVED(axis) \
    (abs((int8_t)(axis - 128)) > 20)

// Redefinition for C++; requires type to specify parameters
#undef TAI_CONTINUE
#define TAI_CONTINUE(type, hook, ...)                                       \
({                                                                          \
    _tai_hook_user *cur  = (_tai_hook_user*)(hook);                         \
    _tai_hook_user *next = (_tai_hook_user*)cur->next;                      \
    next ? ((type)next->func)(__VA_ARGS__) : ((type)cur->old)(__VA_ARGS__); \
})

#define DECL_FUNC_HOOK(name, ...)          \
    static tai_hook_ref_t name##HookRef;   \
    static SceUID name##HookUid = -1;      \
    static int name##HookFunc(__VA_ARGS__)

#define BIND_FUNC_OFFSET_HOOK(name, pid, modid, segidx, offset, thumb)    \
    name##HookUid = taiHookFunctionOffsetForKernel((pid), &name##HookRef, \
        (modid), (segidx), (offset), thumb, (const void*)name##HookFunc)

#define BIND_FUNC_EXPORT_HOOK(name, pid, module, lib_nid, func_nid)       \
    name##HookUid = taiHookFunctionExportForKernel((pid), &name##HookRef, \
        (module), (lib_nid), (func_nid), (const void*)name##HookFunc)

#define UNBIND_FUNC_HOOK(name)                                 \
({                                                             \
    if (name##HookUid > 0)                                     \
        taiHookReleaseForKernel(name##HookUid, name##HookRef); \
})

SceUID Mempool::uid = -1;
static SceUID eventFlagUid = -1;
static SceUID threadUid = -1;

static Controller *controllers[MAX_CONTROLLERS] = {};

static int g_logFd = -1;

struct RawLogState
{
    bool hasLast = false;
    uint8_t last[64] = {};
};

static RawLogState g_rawLogStates[MAX_CONTROLLERS] = {};
static uint8_t g_lastReportId[MAX_CONTROLLERS] = {};

extern "C" void vitacontrolFileLogWrite(const char *buf, size_t len)
{
    if (g_logFd < 0 || !buf || len == 0)
        return;
    ksceIoWrite(g_logFd, buf, len);
}

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

static void rawLogDeltaForSlot(int slot, const uint8_t *buf, size_t len)
{
    if (g_logFd < 0 || slot < 0 || slot >= MAX_CONTROLLERS || !buf || len < 1)
        return;

    size_t maxBytes = (len > 64) ? 64 : len;
    RawLogState &st = g_rawLogStates[slot];

    if (!st.hasLast)
    {
        for (size_t i = 0; i < maxBytes; i++)
            st.last[i] = buf[i];
        st.hasLast = true;
        return; // don't write a line until we have a delta (keeps file clean for mapper)
    }

    bool any = false;
    for (size_t i = 0; i < maxBytes; i++)
    {
        if (st.last[i] != buf[i]) { any = true; break; }
    }
    if (!any) return;

    // Format (parseable by mapper): id=.. b1=.. b2=.. b3=.. b4=.. b5=.. b6=.. b7=.. ch=[idx:old>new,...]\n
    char line[256];
    char *p = line;
    appendStr(p, "id=");
    appendHex2(p, buf[0]);
    if (maxBytes >= 8)
    {
        appendStr(p, " b1="); appendHex2(p, buf[1]);
        appendStr(p, " b2="); appendHex2(p, buf[2]);
        appendStr(p, " b3="); appendHex2(p, buf[3]);
        appendStr(p, " b4="); appendHex2(p, buf[4]);
        appendStr(p, " b5="); appendHex2(p, buf[5]);
        appendStr(p, " b6="); appendHex2(p, buf[6]);
        appendStr(p, " b7="); appendHex2(p, buf[7]);
    }
    appendStr(p, " ch=[");
    bool first = true;
    for (size_t i = 0; i < maxBytes; i++)
    {
        if (st.last[i] == buf[i]) continue;
        if (!first) *p++ = ',';
        if (i >= 10) *p++ = (char)('0' + (i / 10));
        *p++ = (char)('0' + (i % 10));
        *p++ = ':';
        appendHex2(p, st.last[i]);
        *p++ = '>';
        appendHex2(p, buf[i]);
        first = false;
    }
    *p++ = ']';
    *p++ = '\n';
    vitacontrolFileLogWrite(line, (size_t)(p - line));

    // update baseline
    for (size_t i = 0; i < maxBytes; i++)
        st.last[i] = buf[i];
}

static inline int clamp(int value, int min, int max)
{
    if (value <= min) return min;
    if (value >= max) return max;
    return value;
}

static inline int scaleTouchCoord(int coord, int size, int dead, int vitaSize)
{
    return clamp(((coord - dead) * vitaSize) / (size - dead * 2), 0, vitaSize - 1);
}

DECL_FUNC_HOOK(sceBt0x22999C8, void *ptr0, void *ptr1)
{
    uint32_t flags = *(uint32_t*)((uint32_t)ptr1 + 4);

    if (ptr0 && !(flags & 0x2))
    {
        // Set some bits to allow pairing unsupported devices
        uint32_t *data = (uint32_t*)(*(uint32_t*)ptr0 + 8);
        *data |= 0x11000;
    }

    return TAI_CONTINUE(int(*)(void*, void*), sceBt0x22999C8HookRef, ptr0, ptr1);
}

DECL_FUNC_HOOK(ksceCtrlGetControllerPortInfo, SceCtrlPortInfo *info)
{
    int ret = TAI_CONTINUE(int(*)(SceCtrlPortInfo*), ksceCtrlGetControllerPortInfoHookRef, info);

    if (ret >= 0)
    {
        // Spoof connected controllers to be DualShock 4 controllers
        for (int i = 0; i < MAX_CONTROLLERS; i++)
        {
            if (controllers[i])
                info->port[i + 1] = SCE_CTRL_TYPE_DS4;
        }
    }

    return ret;
}

DECL_FUNC_HOOK(sceCtrlGetBatteryInfo, int port, uint8_t *batt)
{
    if (port > 0 && controllers[port - 1])
    {
        // Override the battery level for connected controllers
        uint8_t data;
        ksceKernelMemcpyUserToKernel(&data, (void*)batt, sizeof(uint8_t));
        data = controllers[port - 1]->getBatteryLevel();
        ksceKernelMemcpyKernelToUser((void*)batt, &data, sizeof(uint8_t));
        return 0;
    }

    return TAI_CONTINUE(int(*)(int, uint8_t*), sceCtrlGetBatteryInfoHookRef, port, batt);
}

static void patchControlData(int port, SceCtrlData *data, int count, bool negative)
{
    // Use controller 1 data for port 0, or controllers 1-4 for ports 1-4
    int cont = (port > 0) ? (port - 1) : 0;
    if (!controllers[cont]) return;
    const ControlData *controlData = controllers[cont]->getControlData();

    // Forward PS button presses to the kernel so the system menu receives them
    if (controlData->buttons & SCE_CTRL_PSBUTTON)
        ksceCtrlSetButtonEmulation(port, 0, 0, SCE_CTRL_PSBUTTON, 16);

    for (int i = 0; i < count; i++)
    {
        // Reset initial values for controller ports (port 0 is additive)
        if (port > 0)
        {
            data[i].buttons = (negative ? 0xFFFFFFFF : 0x00000000);
            data[i].lx = data[i].ly = data[i].rx = data[i].ry = 127;
        }

        // Set the button data from the controller, with optional negative logic
        // TODO: properly handle extended/analog triggers
        if (negative)
            data[i].buttons &= ~controlData->buttons;
        else
            data[i].buttons |= controlData->buttons;

        // Set the stick data from the controller
        data[i].lx = clamp(data[i].lx + controlData->leftX  - 127, 0, 255);
        data[i].ly = clamp(data[i].ly + controlData->leftY  - 127, 0, 255);
        data[i].rx = clamp(data[i].rx + controlData->rightX - 127, 0, 255);
        data[i].ry = clamp(data[i].ry + controlData->rightY - 127, 0, 255);
    }
}

#define DECL_FUNC_HOOK_CTRL(name, negative)                                                       \
    DECL_FUNC_HOOK(name, int port, SceCtrlData *data, int count)                                  \
    {                                                                                             \
        int ret = TAI_CONTINUE(int(*)(int, SceCtrlData*, int), name##HookRef, port, data, count); \
        if (ret >= 0)                                                                             \
            patchControlData(port, data, count, (negative));                                      \
        return ret;                                                                               \
    }

DECL_FUNC_HOOK_CTRL(ksceCtrlPeekBufferPositive,     false)
DECL_FUNC_HOOK_CTRL(ksceCtrlReadBufferPositive,     false)
DECL_FUNC_HOOK_CTRL(ksceCtrlPeekBufferNegative,     true)
DECL_FUNC_HOOK_CTRL(ksceCtrlReadBufferNegative,     true)
DECL_FUNC_HOOK_CTRL(ksceCtrlPeekBufferPositiveExt,  false)
DECL_FUNC_HOOK_CTRL(ksceCtrlReadBufferPositiveExt,  false)

DECL_FUNC_HOOK_CTRL(ksceCtrlPeekBufferPositive2,    false)
DECL_FUNC_HOOK_CTRL(ksceCtrlReadBufferPositive2,    false)
DECL_FUNC_HOOK_CTRL(ksceCtrlPeekBufferNegative2,    true)
DECL_FUNC_HOOK_CTRL(ksceCtrlReadBufferNegative2,    true)
DECL_FUNC_HOOK_CTRL(ksceCtrlPeekBufferPositiveExt2, false)
DECL_FUNC_HOOK_CTRL(ksceCtrlReadBufferPositiveExt2, false)

static void patchTouchData(int port, SceTouchData *data, int count)
{
    // Use controller 1 data for the front touch port
    if (port != SCE_TOUCH_PORT_FRONT || !controllers[0]) return;
    const TouchData *touchData = controllers[0]->getTouchData();

    for (int i = 0; i < count; i++)
    {
        int reportNum = 0;

        // Add touches from the controller if present
        for (int j = 0; j < 2; j++)
        {
            if (!touchData->touchActive[j]) continue;
            data[i].report[reportNum].id = touchData->touchId[j];
            data[i].report[reportNum].x = scaleTouchCoord(touchData->touchX[j], touchData->touchWidth,  touchData->touchDeadX, TOUCHSCREEN_WIDTH);
            data[i].report[reportNum].y = scaleTouchCoord(touchData->touchY[j], touchData->touchHeight, touchData->touchDeadY, TOUCHSCREEN_HEIGHT);
            reportNum++;
        }

        // If the controller provided touches, update the report number (system touches are overwritten and ignored)
        // TODO: touches could be additive, if we make sure not to overflow... do we want that?
        if (reportNum > 0)
            data[i].reportNum = reportNum;
    }
}

#define DECL_FUNC_HOOK_TOUCH(name)                                                                              \
    DECL_FUNC_HOOK(name, int port, SceTouchData *data, int count, int region)                                   \
    {                                                                                                           \
        int ret = TAI_CONTINUE(int(*)(int, SceTouchData*, int, int), name##HookRef, port, data, count, region); \
        if (ret >= 0)                                                                                           \
            patchTouchData(port, data, count);                                                                  \
        return ret;                                                                                             \
    }

DECL_FUNC_HOOK_TOUCH(ksceTouchPeek)
DECL_FUNC_HOOK_TOUCH(ksceTouchPeekRegion)
DECL_FUNC_HOOK_TOUCH(ksceTouchRead)
DECL_FUNC_HOOK_TOUCH(ksceTouchReadRegion)

DECL_FUNC_HOOK(sceMotionGetState, SceMotionState *state)
{
    int ret = TAI_CONTINUE(int(*)(SceMotionState*), sceMotionGetStateHookRef, state);

    if (ret >= 0 && controllers[0])
    {
        // Use controller 1 data for the motion state
        const MotionState *motionState = controllers[0]->getMotionState();

        // Set the acceleration and velocity from the controller
        SceMotionState data;
        ksceKernelMemcpyUserToKernel(&data, (void*)state, sizeof(SceMotionState));
        data.acceleration.x    = motionState->accelerX;
        data.acceleration.y    = motionState->accelerY;
        data.acceleration.z    = motionState->accelerZ;
        data.angularVelocity.x = motionState->velocityX;
        data.angularVelocity.y = motionState->velocityY;
        data.angularVelocity.z = motionState->velocityZ;
        ksceKernelMemcpyKernelToUser((void*)state, &data, sizeof(SceMotionState));
    }

    return ret;
}

static int bluetoothCallback(int notifyId, int notifyCount, int notifyArg, void *common)
{
    static uint8_t buffer[0x100];

    SceBtEvent event;

    // Read a bluetooth event, or multiple to take care of overflow
    int ret;
    while ((ret = ksceBtReadEvent(&event, 1)) == SCE_BT_ERROR_CB_OVERFLOW);
    if (ret <= 0) return 0;

    int cont = -1;

    // Search connected controllers for the device that triggered the event
    for (int i = 0; i < MAX_CONTROLLERS; i++)
    {
        if (controllers[i] && controllers[i]->getMac0() == event.mac0 && controllers[i]->getMac1() == event.mac1)
        {
            cont = i;
            break;
        }
    }

    // If the device isn't a connected controller, find a free controller slot
    if (cont == -1)
    {
        for (int i = 0; i < MAX_CONTROLLERS; i++)
        {
            if (!controllers[i])
            {
                cont = i;
                break;
            }
        }

        if (cont == -1)
        {
            LOG("  No free controller slots!\n");
            return 0;
        }
    }

    // Handle the bluetooth event
    switch (event.id)
    {
        case 0x05: // Connection accepted
            LOG("  Connection accepted (slot %d)\n", cont);
            // Try to create a controller instance for the device
            if (!controllers[cont])
            {
                controllers[cont] = Controller::makeController(event.mac0, event.mac1, cont);
                if (controllers[cont])
                    LOG("  Controller created successfully\n");
                else
                    LOG("  Failed to create controller (unknown VID/PID?)\n");
            }
            // Kick off input polling immediately. Some controllers never send any init write/feature
            // replies, so without an initial read request we will never receive 0x0A events.
            if (controllers[cont])
            {
                LOG("  Starting initial HID read...\n");
                controllers[cont]->requestReport(HID_REQUEST_READ, buffer, sizeof(buffer));
            }
            break;

        case 0x06: // Connection terminated
            LOG("  Connection terminated (slot %d)\n", cont);
            // Remove the controller instance for the device
            if (controllers[cont])
            {
                Mempool::free(controllers[cont]);
                controllers[cont] = nullptr;
            }
            break;

        case 0x0A: // Reply to read request
            if (controllers[cont])
            {
                // Minimal diagnostics: log report ID changes per slot so we can identify the active report type
                if (buffer[0] != g_lastReportId[cont])
                {
                    g_lastReportId[cont] = buffer[0];
                    LOG("  Slot %d reportId=0x%02X\n", cont, buffer[0]);
                }

                // Always emit a raw delta line for the mapper app (works for any controller type)
                rawLogDeltaForSlot(cont, buffer, sizeof(buffer));

                // Process the received input report and request another
                controllers[cont]->processReport(buffer, sizeof(buffer));
                controllers[cont]->requestReport(HID_REQUEST_READ, buffer, sizeof(buffer));

                // Keep the screen awake when inputs are pressed
                const ControlData *c = controllers[cont]->getControlData();
                const TouchData *t = controllers[cont]->getTouchData();
                if (c->buttons || t->touchActive[0] || t->touchActive[1] || AXIS_MOVED(c->leftX) ||
                    AXIS_MOVED(c->leftY) || AXIS_MOVED(c->rightX) || AXIS_MOVED(c->rightY))
                    ksceKernelPowerTick(SCE_KERNEL_POWER_TICK_DEFAULT);
            }
            else
            {
                // Log raw data even when no controller object exists
                LOG("  Read report [slot %d, NO CONTROLLER]: %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X\n",
                    cont, buffer[0], buffer[1], buffer[2], buffer[3], buffer[4], buffer[5], buffer[6], buffer[7],
                    buffer[8], buffer[9], buffer[10], buffer[11], buffer[12], buffer[13], buffer[14], buffer[15]);
            }
            break;

        case 0x0B: // Reply to write request
            LOG("  Write request reply (slot %d)\n", cont);
            // Request an initial input report (write/feature requests are typically part of controller init)
            if (controllers[cont])
                controllers[cont]->requestReport(HID_REQUEST_READ, buffer, sizeof(buffer));
            break;

        case 0x0C: // Reply to feature request
            LOG("  Feature request reply (slot %d)\n", cont);
            if (controllers[cont])
                controllers[cont]->requestReport(HID_REQUEST_READ, buffer, sizeof(buffer));
            break;
    }

    return 0;
}

static int callbackThread(SceSize args, void *argp)
{
    // Set up a callback to handle bluetooth events
    SceUID callbackUid = ksceKernelCreateCallback("vitacontrol_callback", 0, bluetoothCallback, nullptr);
    ksceBtRegisterCallback(callbackUid, 0, 0xFFFFFFFF, 0xFFFFFFFF);

    while (true)
    {
        // Idle and handle callbacks until the exit flag is set
        uint32_t outBits;
        int ret = ksceKernelWaitEventFlagCB(eventFlagUid, FLAG_EXIT, SCE_EVENT_WAITOR | SCE_EVENT_WAITCLEAR_PAT, &outBits, nullptr);
        if (ret >= 0 && (outBits & FLAG_EXIT))
            break;
    }

    // Clean up the callback
    ksceBtUnregisterCallback(callbackUid);
    ksceKernelDeleteCallback(callbackUid);

    return 0;
}

extern "C"
{

int moduleStart(SceSize args, void *argp)
{
    LOG("=== VitaControl starting ===\n");

    // File logging disabled for performance - eliminates buffer comparison and I/O overhead on every input.
    // Uncomment below to re-enable for diagnostics / mapping sessions.
    /*
    g_logFd = ksceIoOpen("ux0:data/vitacontrol_mapper_raw.txt",
        SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0666);
    if (g_logFd < 0)
    {
        LOG("Failed to open ux0:data/vitacontrol_mapper_raw.txt (%d)\n", g_logFd);
        g_logFd = ksceIoOpen("ur0:data/vitacontrol_mapper_raw.txt",
            SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0666);
        if (g_logFd >= 0)
            LOG("Logging to ur0:data/vitacontrol_mapper_raw.txt\n");
    }
    else
    {
        LOG("Logging to ux0:data/vitacontrol_mapper_raw.txt\n");
    }
    */

    tai_module_info_t modInfo;
    modInfo.size = sizeof(tai_module_info_t);

    if (taiGetModuleInfoForKernel(KERNEL_PID, "SceBt", &modInfo) < 0)
    {
        LOG("Failed to get SceBt module info\n");
        return SCE_KERNEL_START_FAILED;
    }

    // Hook bluetooth functions
    BIND_FUNC_OFFSET_HOOK(sceBt0x22999C8, KERNEL_PID, modInfo.modid, 0, 0x22999C8 - 0x2280000, 1);

    // Hook controller info functions
    BIND_FUNC_EXPORT_HOOK(ksceCtrlGetControllerPortInfo, KERNEL_PID, "SceCtrl", TAI_ANY_LIBRARY, 0xF11D0D30);
    BIND_FUNC_EXPORT_HOOK(sceCtrlGetBatteryInfo,         KERNEL_PID, "SceCtrl", TAI_ANY_LIBRARY, 0x8F9B1CE5);

    if (taiGetModuleInfoForKernel(KERNEL_PID, "SceCtrl", &modInfo) < 0)
    {
        LOG("Failed to get SceCtrl module info\n");
        return SCE_KERNEL_START_FAILED;
    }

    // Hook control data functions
    BIND_FUNC_EXPORT_HOOK(ksceCtrlPeekBufferPositive, KERNEL_PID, "SceCtrl", TAI_ANY_LIBRARY, 0xEA1D3A34);
    BIND_FUNC_EXPORT_HOOK(ksceCtrlReadBufferPositive, KERNEL_PID, "SceCtrl", TAI_ANY_LIBRARY, 0x9B96A1AA);
    BIND_FUNC_EXPORT_HOOK(ksceCtrlPeekBufferNegative, KERNEL_PID, "SceCtrl", TAI_ANY_LIBRARY, 0x19895843);
    BIND_FUNC_EXPORT_HOOK(ksceCtrlReadBufferNegative, KERNEL_PID, "SceCtrl", TAI_ANY_LIBRARY, 0x8D4E0DD1);
    BIND_FUNC_OFFSET_HOOK(ksceCtrlPeekBufferPositiveExt,  KERNEL_PID, modInfo.modid, 0, 0x3928 | 1, 1);
    BIND_FUNC_OFFSET_HOOK(ksceCtrlReadBufferPositiveExt,  KERNEL_PID, modInfo.modid, 0, 0x3BCC | 1, 1);

    // Hook extended control data functions
    BIND_FUNC_OFFSET_HOOK(ksceCtrlPeekBufferPositive2,    KERNEL_PID, modInfo.modid, 0, 0x3EF8 | 1, 1);
    BIND_FUNC_OFFSET_HOOK(ksceCtrlReadBufferPositive2,    KERNEL_PID, modInfo.modid, 0, 0x449C | 1, 1);
    BIND_FUNC_OFFSET_HOOK(ksceCtrlPeekBufferNegative2,    KERNEL_PID, modInfo.modid, 0, 0x41C8 | 1, 1);
    BIND_FUNC_OFFSET_HOOK(ksceCtrlReadBufferNegative2,    KERNEL_PID, modInfo.modid, 0, 0x47F0 | 1, 1);
    BIND_FUNC_OFFSET_HOOK(ksceCtrlPeekBufferPositiveExt2, KERNEL_PID, modInfo.modid, 0, 0x4B48 | 1, 1);
    BIND_FUNC_OFFSET_HOOK(ksceCtrlReadBufferPositiveExt2, KERNEL_PID, modInfo.modid, 0, 0x4E14 | 1, 1);

    // Hook touch data functions
    BIND_FUNC_EXPORT_HOOK(ksceTouchPeek,       KERNEL_PID, "SceTouch", TAI_ANY_LIBRARY, 0xBAD1960B);
    BIND_FUNC_EXPORT_HOOK(ksceTouchPeekRegion, KERNEL_PID, "SceTouch", TAI_ANY_LIBRARY, 0x9B3F7207);
    BIND_FUNC_EXPORT_HOOK(ksceTouchRead,       KERNEL_PID, "SceTouch", TAI_ANY_LIBRARY, 0x70C8AACE);
    BIND_FUNC_EXPORT_HOOK(ksceTouchReadRegion, KERNEL_PID, "SceTouch", TAI_ANY_LIBRARY, 0x9A91F624);

    // Hook motion state functions
    BIND_FUNC_EXPORT_HOOK(sceMotionGetState, KERNEL_PID, "SceMotion", TAI_ANY_LIBRARY, 0xBDB32767);

    Mempool::init();

    // Prepare the event flag and callback thread
    eventFlagUid = ksceKernelCreateEventFlag("vitacontrol_eventflag", 0, 0, nullptr);
    threadUid = ksceKernelCreateThread("vitacontrol_thread", callbackThread, 0x3C, 0x1000, 0, 0x10000, 0);
    ksceKernelStartThread(threadUid, 0, nullptr);

    LOG("=== VitaControl started successfully ===\n");
    return SCE_KERNEL_START_SUCCESS;
}

int moduleStop(SceSize args, void *argp)
{
    LOG("=== VitaControl stopping ===\n");

    if (g_logFd >= 0)
    {
        ksceIoClose(g_logFd);
        g_logFd = -1;
    }

    // Set the exit flag to stop the callback thread
    if (eventFlagUid > 0)
        ksceKernelSetEventFlag(eventFlagUid, FLAG_EXIT);

    // Wait for the callback thread to stop and clean it up
    if (threadUid > 0)
    {
        ksceKernelWaitThreadEnd(threadUid, nullptr, nullptr);
        ksceKernelDeleteThread(threadUid);
        threadUid = -1;
    }

    // Clean up the event flag
    if (eventFlagUid > 0)
    {
        ksceKernelDeleteEventFlag(eventFlagUid);
        eventFlagUid = -1;
    }

    // Disconnect and clean up controllers
    for (int i = 0; i < MAX_CONTROLLERS; i++)
    {
        if (controllers[i])
        {
            ksceBtStartDisconnect(controllers[i]->getMac0(), controllers[i]->getMac1());
            Mempool::free(controllers[i]);
            controllers[i] = nullptr;
        }
    }

    Mempool::deinit();

    // Unhook bluetooth functions
    UNBIND_FUNC_HOOK(sceBt0x22999C8);

    // Unhook controller info functions
    UNBIND_FUNC_HOOK(ksceCtrlGetControllerPortInfo);
    UNBIND_FUNC_HOOK(sceCtrlGetBatteryInfo);

    // Unhook control data functions
    UNBIND_FUNC_HOOK(ksceCtrlReadBufferNegative);
    UNBIND_FUNC_HOOK(ksceCtrlPeekBufferPositive);
    UNBIND_FUNC_HOOK(ksceCtrlReadBufferPositive);
    UNBIND_FUNC_HOOK(ksceCtrlPeekBufferNegative);
    UNBIND_FUNC_HOOK(ksceCtrlPeekBufferPositiveExt);
    UNBIND_FUNC_HOOK(ksceCtrlReadBufferPositiveExt);

    // Unhook extended control data functions
    UNBIND_FUNC_HOOK(ksceCtrlPeekBufferPositive2);
    UNBIND_FUNC_HOOK(ksceCtrlReadBufferPositive2);
    UNBIND_FUNC_HOOK(ksceCtrlPeekBufferNegative2);
    UNBIND_FUNC_HOOK(ksceCtrlReadBufferNegative2);
    UNBIND_FUNC_HOOK(ksceCtrlPeekBufferPositiveExt2);
    UNBIND_FUNC_HOOK(ksceCtrlReadBufferPositiveExt2);

    // Unhook touch data functions
    UNBIND_FUNC_HOOK(ksceTouchPeek);
    UNBIND_FUNC_HOOK(ksceTouchPeekRegion);
    UNBIND_FUNC_HOOK(ksceTouchRead);
    UNBIND_FUNC_HOOK(ksceTouchReadRegion);

    // Unhook motion state functions
    UNBIND_FUNC_HOOK(sceMotionGetState);

    return SCE_KERNEL_STOP_SUCCESS;
}

void _start()
{
    moduleStart(0, nullptr);
}

}
