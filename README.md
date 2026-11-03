# CH32X035 DFU Bootloader

A custom USB DFU bootloader implementation for CH32X035, a drop-in replacement for the factory bootloader.  
[zh_CN(简体中文)](./README_zh.md)

> ⚠️ **Warning**.
> 
> **Most of code is AI-generated.**
> Flash and option areas have been tested and read/write correctly, and RDP (read protection) works as expected. No complete security or boundary testing has been performed.
> Flashing the BOOT area carries some risk; using it means **you accept all risk and responsibility**.
> Be sure to back up the factory bootloader first.

## Features

- Standard DFU 1.1 implementation, compatible with `dfu-util`
- Tiny: binary ~3200 bytes, fits inside the BOOT area
- Read and write user flash and option bytes

## Boot sequence

After reset, the destination is chosen in this order:

1. `BOOT_MODE` (`STATR` bit14) set → clear it and enter DFU
2. PC3 high → enter DFU
3. First word of user flash is `0xFF`/`0x00` (no app) → enter DFU
4. Otherwise → reset the system and jump to the application in user flash

## Flashing

The latest firmware is built with MRS 3 and WCH GCC15, can be downloaded from Releases.  

Since the BOOT area cannot be written through the flash area, a **WCH-Link/E** must be used for downloading: on Windows you can use the MRS built-in downloader or WCH-LinkUtility, setting the start address to 0x1FFF0000.

**Note: MRS downloader and WCH-LinkUtility do not provide a way to read the BOOT area, it is strongly recommended to back up the factory BOOT with minichlink or a similar tool first!**

On *Unix, [minichlink](https://github.com/cnlohr/ch32fun/tree/master/minichlink) is recommended.

Back up the factory bootloader with `minichlink` first:

```sh
minichlink -r stock_boot.bin bootloader 3328
```

Write the firmware:

```sh
minichlink -w dfu_bl.bin bootloader
```

Application firmware is then written to user flash over USB DFU, for example:

```sh
dfu-util -d 1a86:df11 -a 0 -D app.bin
```

Restore the factory one:

```sh
minichlink -w stock_boot.bin bootloader
```

## Building

Open the project (`<project>.wvproj`) in MounRiver Studio and build — the `.bin`/`.hex` artifacts are placed under `obj/`.

## Notes

CH32X035 series chips execute from the **BOOT area** by default on **first power-on**; if code running in the BOOT area performs an NVIC reset, or the RST pin is pulled low without a power cycle, the chip boots from the **Flash area** instead.

## Test
*Machine：macOS 27.0.1，dfu-util 0.11*
```bash
$ dfu-util -l
dfu-util 0.11

Copyright 2005-2009 Weston Schmidt, Harald Welte and OpenMoko Inc.
Copyright 2010-2021 Tormod Volden and Stefan Schmidt
This program is Free Software and has ABSOLUTELY NO WARRANTY
Please report bugs to http://sourceforge.net/projects/dfu-util/tickets/

Found DFU: [1a86:df11] ver=0100, devnum=5, cfg=1, intf=0, path="0-1.3", alt=1, name="@Option", serial="*****"
Found DFU: [1a86:df11] ver=0100, devnum=5, cfg=1, intf=0, path="0-1.3", alt=0, name="@Flash", serial="*****"

$ dfu-util -a 1 -U ob.bin; hexdump ob.bin # read the option bytes
dfu-util 0.11

Copyright 2005-2009 Weston Schmidt, Harald Welte and OpenMoko Inc.
Copyright 2010-2021 Tormod Volden and Stefan Schmidt
This program is Free Software and has ABSOLUTELY NO WARRANTY
Please report bugs to http://sourceforge.net/projects/dfu-util/tickets/

Opening DFU capable USB device...
Device ID 1a86:df11
Device DFU version 0110
Claiming USB DFU Interface...
Setting Alternate Interface #1 ...
Determining device status...
DFU state(2) = dfuIDLE, status(0) = No error condition is present
DFU mode device DFU version 0110
Device returned transfer size 2048
Copying data from DFU device to PC
Upload	[=========================] 100%           16 bytes
Upload done.
Received a total of 16 bytes
0000000 5aa5 18e7 ff00 ff00 00ff 00ff 00ff 00ff
0000010

$ dfu-util -a 0 -D blink.bin # flash the blink test program
dfu-util 0.11

Copyright 2005-2009 Weston Schmidt, Harald Welte and OpenMoko Inc.
Copyright 2010-2021 Tormod Volden and Stefan Schmidt
This program is Free Software and has ABSOLUTELY NO WARRANTY
Please report bugs to http://sourceforge.net/projects/dfu-util/tickets/

dfu-util: Warning: Invalid DFU suffix signature
dfu-util: A valid DFU suffix will be required in a future dfu-util release
Opening DFU capable USB device...
Device ID 1a86:df11
Device DFU version 0110
Claiming USB DFU Interface...
Setting Alternate Interface #0 ...
Determining device status...
DFU state(2) = dfuIDLE, status(0) = No error condition is present
DFU mode device DFU version 0110
Device returned transfer size 2048
Copying data from PC to DFU device
Download        [=========================] 100%         7080 bytes
Download done.
DFU state(8) = dfuMANIFEST-WAIT-RESET, status(0) = No error condition is present
Resetting USB to switch back to runtime mode
Done!
```
