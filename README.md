# **Overview**

This is a community branch of the Smoothieware firmware for the Makera Carvera CNC Machine. 
The community has a trello tracker here: https://trello.com/b/qKxPlEbk/carvera-controller-community-feature-requests

Checkout the Releases Page for downloads.

In the [releases](https://github.com/faecorrigan/CarveraCommunityFirmware/releases) page there several post processors with updated features

Smoothie is a free, opensource, high performance G-code interpreter and CNC controller written in Object-Oriented C++ for the LPC17xx micro-controller ( ARM Cortex M3 architecture ). It will run on a mBed, a LPCXpresso, a SmoothieBoard, R2C2 or any other LPC17xx-based board. The motion control part is a port of the awesome grbl.

#  **Documentation**:
[makera website](https://wiki.makera.com/en/home)

[Supported G codes](https://wiki.makera.com/en/supported-codes)

[Smoothieware documentation](https://smoothieware.github.io/Webif-pack/documentation/web/html/index.html)

**NOTE** it is not necessary to build Smoothie yourself unless you want to. prebuilt binaries are available [here](https://github.com/faecorrigan/CarveraCommunityFirmware/releases)

# **Building Firmware Quick Start**
Note: the current firmware build process has a major bug with the ATC where it will not actuate the stepper motor. As a result, it is not yet useful for running jobs, only testing new features

These are the quick steps to get Smoothie dependencies installed on your computer:

Pull down a clone of the this github project to your local machine.

Download the [launchpad prerequisites](https://launchpad.net/gcc-arm-embedded/4.8/4.8-2014-q1-update/+download/gcc-arm-none-eabi-4_8-2014q1-20140314-win32.zip) and place it in the root folder of this directory.

In the root subdirectory of the cloned project, there are install scripts for the supported platforms. Run the install script appropriate for your platform:
Windows: win_install.cmd
OS X: mac_install - I have not tested this yet, but you will probably have to edit the script to remove the command that downlads the gcc-arm-none file that is linked above
Linux: linux_install - I have not tested this yet, but you will probably have to edit the script to remove the command that downlads the gcc-arm-none file that is linked above
You can then run the BuildShell script which will be created during the install to properly configure the PATH environment variable to point to the required version of GCC for ARM which was just installed on your machine. You may want to edit this script to further customize your development environment.

note: in this fork, I modified the win_install.cmd by removing lines 44-47 and manually downloading the .zip

### **Building The Firmware**
Open the BuildShell.cmd and run:

make clean

make all AXIS=5 PAXIS=3 CNC=1

this will create a file in LPC1768/main.bin

rename it to firmware.bin or another name with firmware in it and either upload it using the carvera controller or copy the file to the sdcard and reset your machine.

Filing issues (for bugs ONLY)
Please follow this guide https://github.com/Smoothieware/Smoothieware/blob/edge/ISSUE_TEMPLATE.md

for more information on compiling smoothieware: Follow this guide... https://smoothieware.github.io/Webif-pack/documentation/web/html/compiling-smoothie.html

# **Contributing**
Please take a look at :

http://smoothieware.org/coding-standards
http://smoothieware.org/developers-guide
http://smoothieware.org/contribution-guidlines
Contributions very welcome !

### **Donate**
This particular branch of the carvera firmware is maintained by [Fae Corrigan](https://www.patreon.com/propsmonster)
For smoothieware as a whole: the Smoothie firmware is free software developed by volunteers. If you find this software useful, want to say thanks and encourage development, please consider a [Donation](https://paypal.me/smoothieware)

### **License**
Smoothieware is released under the GNU GPL v3, which you can find at http://www.gnu.org/licenses/gpl-3.0.en.html


### **Other community resources: **

Open source controllers: 
https://cc.grid.space/ 
https://github.com/GridSpace/carve-control


https://cnc.js.org/ 
https://github.com/cncjs/cncjs-pendant-boilerplate

community feeds, speeds and accessories: https://docs.google.com/spreadsheets/d/1i9jD0Tg6wzTpGYVqhLZMyLFN7pMlSfdfgpQIxDKbojc/edit

carvera website: https://www.makera.com/pages/community https://wiki.makera.com/en/home

work in progress wireless 3 axis touch probe: will be released open source and open hardware along with a purchasable version https://github.com/faecorrigan/Open-Source-3-axis-CNC-Touch-Probe
