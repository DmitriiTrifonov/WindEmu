WindEmu is an attempt to emulate various Psion PDAs.

- Platform-independent core emulation library written in C/C++
- Qt5 front-end (currently quite barebones...)
- Very experimental
- Basic support for multiple devices

Psion 5mx (EPOC R5) features:

- ✅ LCD: implemented
- ✅ Keyboard: implemented
- ✅ Touch panel: implemented
- ❌ Audio: not implemented
- ❌ Serial/UART support: stubbed out
- ✅ ETNA (PCMCIA/CompactFlash): CF card backed by a disk image (see below); ETNA's UART is not emulated
- ✅ RTC: implemented
- ❌ RTC alarm: not implemented
- ❌ Standby mode: not implemented

Oregon Scientific Osaris (EPOC R4) features:

- ✅ LCD: implemented
- ✅ Keyboard: implemented
- ✅ Touch panel: implemented
- ❌ Audio: not implemented
- ❌ Serial/UART support: stubbed out
- ❌ PCMCIA: mostly stubbed out
- ✅ RTC: implemented (needs testing)
- ❌ RTC alarm: not implemented
- ❌ Standby mode: not implemented

Known issues:

- State is not saved (just like a real Psion :p)
- EPOC misbehaves massively with memory banks larger than 0x800000 (may be an OS design flaw? need to confirm)

CompactFlash card (Series 5mx)
------------------------------

Pass a folder or a raw disk image with `--cf` to put a card in the CF slot, where EPOC sees it as drive D:

    ./run.sh /path/to/5mx.bin --cf ~/psion-card
    ./run.sh /path/to/5mx.bin --cf card.img

A folder is turned into a FAT16 volume when the emulator starts, with 64MB of free space for EPOC to use. When the emulator quits, whatever EPOC created, changed or deleted on the card is copied back into the folder. Files that were changed in the folder while the emulator was running are kept rather than deleted, but EPOC's version wins if both sides changed the same file, so it's best to leave the folder alone until the emulator has quit. If the volume can't be read back (say, EPOC reformatted it as something other than FAT16), the emulator says so and keeps the volume's image in the temporary folder.

An image needs a partition table with a FAT16 partition, like a card formatted on a PC. To make an empty 32MB one:

    truncate -s 32M card.img
    echo 'start=32, type=6' | sfdisk card.img
    mkfs.vfat -F 16 -n PSION --offset 32 card.img 32752

Files can be copied in and out of it with mtools (the partition starts 16384 bytes in), for example to install software from a `.sis` file on the Psion:

    mcopy -i card.img@@16384 Program.sis ::

Change an image only while the emulator is closed. When a saved state is resumed, the emulator reports the card door as opened and closed, so EPOC looks at the card again. A state saved by an older WindEmu, before the CF slot existed, needs one `--cold-boot` for EPOC to find the slot.

Copyright
---------

The Psion-specific code is copyright (c) 2019 Ash Wolf.

The ARM disassembly code is a modified version of the one used in [mGBA](https://github.com/mgba-emu/mgba) by endrift. 

WindEmu is available under the Mozilla Public License 2.0.

Resources
---------

Special thanks to [PsiLinux/OpenPsion](http://linux-7110.sourceforge.net/index.shtml) for providing an avenue to learn about the 5mx hardware definitions (registers, etc).

More information on the 5mx hardware is available in the NetBSD port: http://cvsweb.netbsd.org/bsdweb.cgi/src/sys/arch/epoc32/?only_with_tag=MAIN

The EPOC C++ SDK is available here: https://web.archive.org/web/20071010101808/http://www.psionteklogix.com/teknet/pdk/netpad-pdk/epoc_downloads.htm

The ARM variant used in the 5mx is documented here: http://infocenter.arm.com/help/topic/com.arm.doc.ddi0033d/DDI0033D_710a_prelim_ds.pdf

The datasheet for the CL-PS7110 SoC used in the Series 5 (_not_ the 5mx) is available here: https://www.igorkov.org/revo/datasheets/CL-PS7110.pdf - while not identical to Windermere, some components operate in similar fashion.

The datasheet for the CL-PS7111 SoC used in the Osaris is available here: https://www.digchip.com/datasheets/parts/datasheet/096/CL-PS7111-pdf.php

The datasheet for the CL-PS6700 PCMCIA controller used in the Osaris is available here: https://pdf1.alldatasheet.com/datasheet-pdf/view/104907/CIRRUS/CL-PS6700.html




