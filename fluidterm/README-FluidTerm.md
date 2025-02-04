# fluidterm a Serial Terminal for FluidNC

### Overview

This is a clone of the python [miniterm](https://github.com/pyserial/pyserial/blob/master/serial/tools/miniterm.py) with changes made for [FluidNC](https://github.com/bdring/FluidNC). FluidNC is a next generation CNC controller that runs on ESP32 hardware.

In most cases no parameters need to be supplied, but all miniterm parameters and hotkeys are still supported. 

Changes

- The default serial port parameters match FluidNC

  - **Baudrate**: 115200
  - **EOL**: CRLF
  - **Echo**: On

- If there is one serial port, it will attempt to use it. If there is more than one, you will be presented a list to choose from.

- There is a new Transformation to colorize FluidNC responses. It uses that by default.

- You can run it as a python script or as a [Windows exe](https://github.com/bdring/fluidterm/tree/main/dist).

### Usage

- With Python: **python fluidterm.py**
- Windows: double click on **fluidterm.exe** or open a Powershell window in the folder and send **./fluidterm.exe**

### Restarting the ESP32

You can restart the ESP32 to see the boot messages with the FluidNC **$bye** command or you can toggle the DTR function to restart most ESP32 modules by typing Ctrl-R. 

<img src="https://github.com/bdring/fluidterm/blob/main/images/screenshot_01.png" width="800" >

### Source Code

The source code for fluidterm.py (the Python version) is in this directory.  The source code for
fluidterm.exe (the Windows native version) is at https://github.com/MitchBradley/FluidTerm2 .
