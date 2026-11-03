# CH32X035 DFU Bootloader

自定义的CH32X035 USB DFU Bootloader实现，可替换原厂 bootloader

> ⚠️ **风险提示**
>
> 本项目**大部分代码由 AI 生成**。  
> 实测 Flash/Option 区域均可正常读写，RDP （读保护）正常生效。未进行完整的安全性与边界测试。  
> 烧写 BOOT 区存在一定风险，使用即表示**你需自行承担全部风险与后果**。  
> 动手前务必备份原厂 bootloader。

## 功能特性

- 标准 DFU 1.1 实现，兼容 `dfu-util`
- 超小体积：构建后约3200字节，可放入BOOT区域内
- 支持读写用户 flash 与选项字节

## 跳转方式

复位后按以下优先级决定去向：

1. `BOOT_MODE`（`STATR` bit14）置位 → 清位并进入 DFU
2. PC3 为高电平 → 进入 DFU
3. 用户区首字为 `0xFF`/`0x00`（无 app）→ 进入 DFU
4. 其余情况 → 复位系统后跳转至用户 app

## 烧录
最新固件使用 MRS 3 与 WCH GCC15 进行构建, 可在 Release 中下载。  

由于BOOT区无法被flash区域烧写，必须使用 **WCH-Link/E** 进行下载：Windows平台可使用 MRS 自带下载器或 WCH-LinkUtility，下载时选择起始地址为0x1FFF0000即可。  

**由于MRS下载器和WCH-LinkUtility未提供读取BOOT区方法，强烈建议先使用minichlink或类似工具备份原厂 BOOT！**

*Unix系统则建议使用[minichlink](https://github.com/cnlohr/ch32fun/tree/master/minichlink)。  

先使用`minichlink`对原厂 bootloader 进行备份：

```sh
minichlink -r stock_boot.bin bootloader 3328
```

写入固件：

```sh
minichlink -w dfu_bl.bin bootloader
```

之后 app 固件通过 USB DFU 写入用户 flash，例如：

```sh
dfu-util -d 1a86:df11 -a 0 -D app.bin
```

恢复原厂Bootloader：

```sh
minichlink -w stock_boot.bin bootloader
```

## 构建

用 MounRiver Studio 打开工程（`<工程名>.wvproj`），直接 Build 即可，产物在 `obj/` 下的 `.bin`/`.hex`。

## 提示
CH32X035系列芯片在**第一次上电**时会默认从 BOOT 区域开始执行；在 BOOT 区代码中执行NVIC复位/未断电情况下将RST引脚拉低，则会从**Flash**区域启动。  

## 实测
*测试环境：macOS 27.0.1，dfu-util 0.11*
```bash
$ dfu-util -l
dfu-util 0.11

Copyright 2005-2009 Weston Schmidt, Harald Welte and OpenMoko Inc.
Copyright 2010-2021 Tormod Volden and Stefan Schmidt
This program is Free Software and has ABSOLUTELY NO WARRANTY
Please report bugs to http://sourceforge.net/p/dfu-util/tickets/

Found DFU: [1a86:df11] ver=0100, devnum=5, cfg=1, intf=0, path="0-1.3", alt=1, name="@Option", serial="*****"
Found DFU: [1a86:df11] ver=0100, devnum=5, cfg=1, intf=0, path="0-1.3", alt=0, name="@Flash", serial="*****"

$ dfu-util -a 1 -U ob.bin; hexdump ob.bin # 读取Option bytes区域
dfu-util 0.11

Copyright 2005-2009 Weston Schmidt, Harald Welte and OpenMoko Inc.
Copyright 2010-2021 Tormod Volden and Stefan Schmidt
This program is Free Software and has ABSOLUTELY NO WARRANTY
Please report bugs to http://sourceforge.net/p/dfu-util/tickets/

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

$ dfu-util -a 0 -D blink.bin # 烧录闪灯测试程序
dfu-util 0.11

Copyright 2005-2009 Weston Schmidt, Harald Welte and OpenMoko Inc.
Copyright 2010-2021 Tormod Volden and Stefan Schmidt
This program is Free Software and has ABSOLUTELY NO WARRANTY
Please report bugs to http://sourceforge.net/p/dfu-util/tickets/

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