# 8BitDo Lite 2 Controller Debugging Guide

## What We've Done

We've added comprehensive logging throughout the VitaControl plugin to help you reverse-engineer the 8BitDo Lite 2 controller protocol. Here's what's been implemented:

### 1. Core Logging Infrastructure
- Added `ksceDebugPrintf` logging to kernel module
- Logs are written to the Vita's kernel log (accessible via USB/psvDebugScreenPrintf or serial)
- All logs are prefixed with `[VitaControl]` or `[8BitDo Lite 2]` for easy filtering

### 2. Bluetooth Event Logging
The plugin now logs:
- **Connection Events**: When controllers connect/disconnect
- **MAC Addresses**: The Bluetooth MAC address of each device
- **VID/PID Information**: Vendor ID and Product ID of connecting devices
- **Event Types**: All Bluetooth event IDs (0x05=connect, 0x06=disconnect, 0x0A=data, etc.)

### 3. Device Detection
- Logs the VID:PID of every device that tries to connect
- Shows when a controller is recognized vs. unrecognized
- For the 8BitDo Lite 2, this will help us identify its VID:PID

### 4. HID Report Logging
For ANY unrecognized controller (including your 8BitDo Lite 2):
- **Full Report Dump**: First 64 bytes of every HID report in hex format
- **Report ID**: The type of report being sent
- **Potential Button Data**: Best-guess interpretation of button/stick bytes
- **Continuous Logging**: Every input is logged, so you can press buttons and see what changes

### 5. Diagnostic Controller
Created `EightBitDoLite2Controller` that acts as a catch-all for unknown devices:
- Accepts ANY controller that isn't explicitly recognized
- Logs all incoming data in detail
- Provides diagnostic output to help reverse-engineer the protocol

## How to Use This

### Step 1: Deploy the Updated Plugin
1. Copy `build/vitacontrol.skprx` to your Vita at `ur0:tai/vitacontrol.skprx`
2. Reboot your Vita

### Step 2: Enable Kernel Logging
You'll need to capture kernel logs. There are several methods:

**Method A: Using udcd_uvc (Recommended)**
1. Install udcd_uvc and PrincessLog
2. Connect Vita to PC via USB
3. Logs will appear in the console

**Method B: Using TegraRcmGUI (if available)**
- Connect via USB and view kernel logs

**Method C: Using psp2shell**
1. Install psp2shell
2. Connect to Vita and view kernel logs

### Step 3: Pair Your Controller
1. Open Settings → Devices → Bluetooth Devices
2. Put your 8BitDo Lite 2 in pairing mode (D input mode)
3. Pair it with the Vita

### Step 4: Observe the Logs
When you pair, you should see output like:

```
[VitaControl] === VitaControl started successfully ===
[VitaControl] BT Event: ID=0x05 MAC=XXXXXXXX:XXXXXXXX
[VitaControl]   Connection accepted (slot 0)
[VitaControl]   Device VID:PID = 0xXXXX:0xXXXX
[VitaControl]   No exact match found - using diagnostic controller for VID:PID 0xXXXX:0xXXXX
[8BitDo Lite 2] Controller initialized
```

**The VID:PID shown is critical** - record it!

### Step 5: Press Buttons and Observe
Press buttons on your controller one at a time:
- Press and hold one button
- Look at the hex dump in the logs
- Note which bytes change

You'll see output like:
```
[VitaControl]   Read report [slot 0]: 01 7F 80 7F 80 00 00 00 00 00 00 00 00 00 00 00
[8BitDo Lite 2] Report ID: 0x01, Length: 64
[8BitDo Lite 2] Full report (64 bytes): 01 7F 80 7F 80 00 00 00 ...
[8BitDo Lite 2] Potential stick data: LX=7F LY=80 RX=7F RY=80
[8BitDo Lite 2] Potential button bytes: 00 00 00
```

When you press a button, one of those `00` bytes will change to something else!

## Step 6: Document Your Findings

Create a mapping table. For example:

| Button | Byte Changed | Bit/Value |
|--------|--------------|-----------|
| A      | Byte 5       | 0x01      |
| B      | Byte 5       | 0x02      |
| X      | Byte 5       | 0x08      |
| Y      | Byte 5       | 0x10      |
| D-pad  | Byte 5       | 0-7 (like DualShock) |
| L      | Byte 6       | 0x01      |
| R      | Byte 6       | 0x02      |
| etc... | ...          | ...       |

Also test the analog sticks - move them and note which bytes change (likely bytes 1-4).

## Step 7: Implement the Actual Controller

Once you understand the protocol:

1. **Update the VID:PID mapping** in `src/controller.cpp`:
   ```cpp
   DECL_CONTROLLER(0xXXXX, 0xXXXX, EightBitDoLite2Controller);
   ```

2. **Define the report structure** in `src/controllers/eightbitdo_lite2_controller.h`:
   ```cpp
   struct EightBitDoLite2Report
   {
       uint8_t reportId;
       uint8_t leftX;
       uint8_t leftY;
       uint8_t rightX;
       uint8_t rightY;
       uint8_t buttons1;
       uint8_t buttons2;
       // ... etc
   } __attribute__((packed));
   ```

3. **Implement button mapping** in `src/controllers/eightbitdo_lite2_controller.cpp`:
   ```cpp
   void EightBitDoLite2Controller::processReport(uint8_t *buffer, size_t length)
   {
       if (buffer[0] != 0x01) return; // Expected report ID
       
       EightBitDoLite2Report *report = (EightBitDoLite2Report*)buffer;
       
       controlData.buttons = 0;
       
       // Map buttons based on your findings
       if (report->buttons1 & 0x01) controlData.buttons |= SCE_CTRL_CROSS;
       if (report->buttons1 & 0x02) controlData.buttons |= SCE_CTRL_CIRCLE;
       // ... etc
       
       // Map sticks
       controlData.leftX = report->leftX;
       controlData.leftY = report->leftY;
       controlData.rightX = report->rightX;
       controlData.rightY = report->rightY;
   }
   ```

4. **Rebuild and test**:
   ```bash
   cd build
   make -j4
   ```

## Tips for Reverse Engineering

1. **Test one button at a time** - this makes it obvious which byte/bit corresponds to which button
2. **Test analog sticks separately** - move each stick axis individually
3. **Compare to similar controllers** - Check SDL or MissionControl sources for similar 8BitDo controllers
4. **Look for patterns**:
   - D-pad often uses values 0-7 for 8 directions (N, NE, E, SE, S, SW, W, NW)
   - Face buttons are usually bits in a byte
   - Analog sticks are usually 8-bit values (0-255, centered at 127-128)

## Troubleshooting

**No logs appearing?**
- Make sure you have a kernel log viewer installed
- Check that the plugin loaded: look for "VitaControl started successfully"
- Try rebooting after installing

**Controller pairs but no data?**
- This is expected! The logging shows you the raw data so you can map it
- Follow Step 5-7 above to implement the mapping

**Controller doesn't pair at all?**
- Make sure it's in "D" input mode (check the 8BitDo manual)
- Try forgetting and re-pairing
- Check if other Bluetooth devices work

## Next Steps

1. Get the kernel logs working
2. Pair your controller and record the VID:PID
3. Press each button and record which bytes change
4. Share your findings (or implement the mapping yourself!)
5. Test and iterate

Good luck! The hardest part is getting the logs - once you have those, mapping the buttons is straightforward.

