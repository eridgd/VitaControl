#include <psp2/ctrl.h>
#include <psp2/io/fcntl.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>

#include "debugScreen.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define RAW_LOG_PATH  "ux0:data/vitacontrol_8bitdo_raw.txt"
#define OUT_LOG_PATH  "ux0:data/lite2_mapper_results.txt"

typedef struct Step {
  const char *name;
  const char *prompt;
} Step;

static const Step STEPS[] = {
  // Physical button labels on the 8BitDo Lite 2 (D-input), with expected Vita mapping
  {"A",           "Press A (expected Vita: CIRCLE)"},
  {"B",           "Press B (expected Vita: CROSS)"},
  {"X",           "Press X (expected Vita: TRIANGLE)"},
  {"Y",           "Press Y (expected Vita: SQUARE)"},

  {"DPAD_UP",     "Press D-PAD UP"},
  {"DPAD_RIGHT",  "Press D-PAD RIGHT"},
  {"DPAD_DOWN",   "Press D-PAD DOWN"},
  {"DPAD_LEFT",   "Press D-PAD LEFT"},

  {"L1",          "Press L1 (small bumper; expected Vita: L1 / secondary)"},
  {"R1",          "Press R1 (small bumper; expected Vita: R1 / secondary)"},
  {"L2",          "Press L2 (big shoulder; expected Vita: LTRIGGER / Left shoulder)"},
  {"R2",          "Press R2 (big shoulder; expected Vita: RTRIGGER / Right shoulder)"},

  {"L3",          "Press L3 (left stick click)"},
  {"R3",          "Press R3 (right stick click)"},

  {"START",       "Press START / PLUS"},
  {"SELECT",      "Press SELECT / MINUS"},

  {"HOME",        "Press HOME (expected Vita: PS button) - optional"},

  {"STICK_L",     "Move LEFT stick a bit (any direction)"},
  {"STICK_R",     "Move RIGHT stick a bit (any direction)"},
};

static void clear_screen(void) {
  psvDebugScreenClear(0x000000);
}

static int open_out_log(void) {
  int fd = sceIoOpen(OUT_LOG_PATH, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0666);
  if (fd >= 0) {
    const char *hdr =
      "Lite2 Mapper Results\n"
      "Source raw log: " RAW_LOG_PATH "\n"
      "Format: STEP_NAME\tRAW_LINE\n\n";
    sceIoWrite(fd, hdr, (unsigned)strlen(hdr));
  }
  return fd;
}

static bool read_next_line(int fd, char *out, int out_cap) {
  // Read until newline. The kernel module writes newline-terminated records.
  int n = 0;
  while (n < out_cap - 1) {
    char ch;
    int r = sceIoRead(fd, &ch, 1);
    if (r <= 0) {
      return false;
    }
    out[n++] = ch;
    if (ch == '\n') break;
  }
  out[n] = 0;
  return true;
}

static void wait_for_new_raw_line(int raw_fd, char *line, int line_cap) {
  // The raw log is append-only during a session.
  // We poll until we can read a full newline-terminated line.
  while (true) {
    if (read_next_line(raw_fd, line, line_cap)) {
      return;
    }
    // No new data yet — wait a bit and retry.
    sceKernelDelayThread(50 * 1000);
  }
}

static void write_step_result(int out_fd, const char *step_name, const char *raw_line) {
  if (out_fd < 0) return;
  sceIoWrite(out_fd, step_name, (unsigned)strlen(step_name));
  sceIoWrite(out_fd, "\t", 1);
  sceIoWrite(out_fd, raw_line, (unsigned)strlen(raw_line));
}

int main(void) {
  sceCtrlSetSamplingMode(SCE_CTRL_MODE_ANALOG);
  psvDebugScreenInit();

  clear_screen();
  // Bigger text: 2x font scaling (more readable, fewer chars per line)
  psvDebugScreenSetFont(psvDebugScreenScaleFont2x(psvDebugScreenGetFont()));

  psvDebugScreenPrintf("Lite2 Mapper\n\n");
  psvDebugScreenPrintf("This app watches: %s\n", RAW_LOG_PATH);
  psvDebugScreenPrintf("and writes:       %s\n\n", OUT_LOG_PATH);
  psvDebugScreenPrintf("Make sure VitaControl is installed and the controller is connected.\n");
  psvDebugScreenPrintf("Press X on the Vita to begin.\n");

  // Wait for Vita X to start (this is the built-in Vita buttons, not the external controller).
  SceCtrlData pad;
  uint32_t prev = 0;
  while (true) {
    sceCtrlPeekBufferPositive(0, &pad, 1);
    uint32_t pressed = pad.buttons & ~prev;
    prev = pad.buttons;
    if (pressed & SCE_CTRL_CROSS) break;
    sceKernelDelayThread(16 * 1000);
  }

  int out_fd = open_out_log();

  // Open raw log and seek to end so each step captures the *next* delta line.
  int raw_fd = sceIoOpen(RAW_LOG_PATH, SCE_O_RDONLY, 0);
  if (raw_fd < 0) {
    clear_screen();
    psvDebugScreenPrintf("ERROR: couldn't open %s\n", RAW_LOG_PATH);
    psvDebugScreenPrintf("Is VitaControl updated and loaded?\n");
    psvDebugScreenPrintf("\nPress CIRCLE to exit.\n");
    while (true) {
      sceCtrlPeekBufferPositive(0, &pad, 1);
      if (pad.buttons & SCE_CTRL_CIRCLE) break;
      sceKernelDelayThread(16 * 1000);
    }
    sceKernelExitProcess(0);
    return 0;
  }

  sceIoLseek(raw_fd, 0, SCE_SEEK_END);

  const int steps_total = (int)(sizeof(STEPS) / sizeof(STEPS[0]));
  char line[256];

  for (int i = 0; i < steps_total; i++) {
    clear_screen();
    psvDebugScreenPrintf("Step %d / %d\n\n", i + 1, steps_total);
    psvDebugScreenPrintf("%s\n\n", STEPS[i].prompt);
    psvDebugScreenPrintf("Now press the button / do the action on the Lite 2.\n");
    psvDebugScreenPrintf("Waiting for a new raw log line...\n");

    // Flush any pending raw lines (e.g. button release from previous step)
    // so this step only captures deltas that happen AFTER the prompt.
    sceIoLseek(raw_fd, 0, SCE_SEEK_END);

    // Wait for the kernel module to append the next delta line.
    wait_for_new_raw_line(raw_fd, line, (int)sizeof(line));

    // Show what we captured and write it to the output file.
    psvDebugScreenPrintf("\nCaptured:\n%s\n", line);
    write_step_result(out_fd, STEPS[i].name, line);

    // Auto-advance after a short pause so the user can see what was captured.
    // Allow abort with Vita CIRCLE during the pause.
    psvDebugScreenPrintf("\nNext step in 1s... (press CIRCLE on Vita to abort)\n");
    for (int t = 0; t < 1000; t += 16) {
      sceCtrlPeekBufferPositive(0, &pad, 1);
      if (pad.buttons & SCE_CTRL_CIRCLE) {
        psvDebugScreenPrintf("\nAborted.\n");
        if (raw_fd >= 0) sceIoClose(raw_fd);
        if (out_fd >= 0) sceIoClose(out_fd);
        sceKernelExitProcess(0);
        return 0;
      }
      sceKernelDelayThread(16 * 1000);
    }
  }

  clear_screen();
  psvDebugScreenPrintf("Done!\n\n");
  psvDebugScreenPrintf("Results written to:\n%s\n\n", OUT_LOG_PATH);
  psvDebugScreenPrintf("Press CIRCLE (Vita) to exit.\n");

  while (true) {
    sceCtrlPeekBufferPositive(0, &pad, 1);
    if (pad.buttons & SCE_CTRL_CIRCLE) break;
    sceKernelDelayThread(16 * 1000);
  }

  if (raw_fd >= 0) sceIoClose(raw_fd);
  if (out_fd >= 0) sceIoClose(out_fd);
  sceKernelExitProcess(0);
  return 0;
}


