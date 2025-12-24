# VitaControl
Use bluetooth controllers on your Vita!

### Overview
VitaControl is a plugin that aims to support a wide variety of bluetooth controllers on the PlayStation Vita. It uses an
object-oriented design, which makes it easy to add support for new controllers. VitaControl is based on
[ds34vita](https://github.com/MERLev/ds34vita) by MERLev, which is based on [ds3vita](https://github.com/xerpi/ds3vita)
and [ds4vita](https://github.com/xerpi/ds4vita) by xerpi.

### Downloads
The latest build of VitaControl is automatically provided via GitHub Actions, and can be downloaded from the
[releases page](https://github.com/Hydr8gon/VitaControl/releases).

### VitaControl Mapper
This branch includes **VitaControl Mapper**, a companion VPK application for controller calibration and mapping.
The mapper helps identify button mappings for unsupported controllers by capturing raw HID reports while you press
buttons. Build the mapper with the main project - the VPK will be generated in `vitacontrol-mapper/build/`.

**Features:**
* Interactive button mapping wizard
* Real-time HID report capture
* File-based logging for analysis
* Touchscreen-based navigation (avoids controller conflicts)

See `DEBUGGING_GUIDE.md` and `CHANGES_SUMMARY.md` for more details on using the mapper and adding controller support.

### Usage
Place `vitacontrol.skprx` in the `ur0:tai/` directory on your Vita. Open `config.txt` in the same folder and add
`ur0:tai/vitacontrol.skprx` under the `*KERNEL` header. Reboot the Vita and pair your controllers through the Settings
app! If you have issues with a controller when it's first paired, it might work after another reboot.

### Supported Controllers
* Sony DualShock 3 Controller
* Sony DualShock 4 Controller
* Sony DualSense Controller
* Sony DualSense Edge Controller
* Microsoft Xbox One Controller
* Nintendo Switch Pro Controller
* 8BitDo Lite 2 Controller (D-input mode)
* 8BitDo Pro 3 Controller (Switch-compatible mode)

### Contributing
Pull requests are welcome for this project. I can add support for controllers that I have access to and am interested
in, but anything else is up to the community. If you know your way around code and have a controller that you'd like to
support, it shouldn't be too hard to add. You can check if projects such as
[MissionControl](https://github.com/ndeadly/MissionControl) or [SDL](https://github.com/libsdl-org/SDL) have drivers for
the controller that you can use as a reference.

### Building
To build VitaControl, you need to install [Vita SDK](https://vitasdk.org). With that set up, run
`mkdir -p build && cd build && cmake .. && make -j$(nproc)` in the project root directory to start building.

The build will generate:
* `build/vitacontrol.skprx` - The main plugin
* `vitacontrol-mapper/build/vitacontrol_mapper.vpk` - The companion mapper application

### Enhanced Features (This Branch)
This branch includes several enhancements over the base VitaControl:

* **8BitDo Controller Support**: Full support for 8BitDo Lite 2 and Pro 3 controllers
* **VitaControl Mapper**: Interactive tool for mapping unsupported controllers
* **Enhanced Logging**: File-based logging to `ux0:data/vitacontrol_mapper_raw.txt` for diagnostics
* **Improved Switch Pro Controller**: Better handling of Switch-compatible controllers including 8BitDo Pro 3

### Other Links
* [Hydra's Lair](https://hydr8gon.github.io) - Blog where I may or may not write about things
* [Discord Server](https://discord.gg/JbNz7y4) - A place to chat about my projects and stuff
