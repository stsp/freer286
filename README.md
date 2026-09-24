# run286 — an open loader for Phar Lap 286|DOS-Extender programs

The games Origin shipped on 286|DOS-Extender (Ultima VIII, BioForge,
Crusader) are not flat `.EXP` images. Each `.EXE` is a real-mode RUN286B
stub, followed by the extender's own `P2` image, a `DLLD` directory of the
DLLs bound into the file, and finally the program itself as a **16bit
segmented NE** marked `ne_exetyp = 0x81`. The program talks to the extender
through two DLLs in the OS/2 1.x style, PHAPI and DOSCALLS, and through
plain `int 21h`.

run286 is an ordinary **DPMI client in ring 3** that does itself what
RUN286.EXE does: it reads the NE image out of a bound executable, builds its
segments as LDT descriptors, resolves its imports and enters it. PHAPI and
DOSCALLS are implemented on top of DPMI, so the program ends up in ring 3
with a DPMI host underneath it instead of in ring 0 with Phar Lap's own
extender. Nothing here writes a descriptor table by hand.

## What is here

* `neexe.[ch]` — the bound-file and NE parsers. Finds the stub, the
  extender image, the DLL directory and the program's NE; reads the segment
  table, the module reference table and the entry table. Plain NE files
  (the games' own `AILXMI.DLL`, `ASYLUM.DLL`) are handled too.
* `neload.[ch]` — applying NE relocations: internal references including
  moveable segments through the entry table, imports by ordinal and by
  name, additive fixups and fixup chains.
* `dos/` — the loader itself, built with dj64: the DPMI backend, PHAPI,
  DOSCALLS and the interrupt gates.
* `nedump.c` — a host-side harness. It lays the image out in malloc'ed
  memory, runs every relocation against a dummy backend and reports what it
  found. `test.sh` runs it over the three games.
* `restub286.c` — a host tool that puts `run286.exe` in front of a bound
  program, so the game starts as a DOS program of its own.

## Building

    make               # host tools: nedump, restub286
    make -C dos        # the loader, needs dj64dev installed

With djstub installed as well, `make -C dos exe` builds `run286.exe`.

## Running

Put a `RUN286.CFG` next to the program, holding the image name on the first
line; a second line of any text turns on a trace of every API call, which
goes to the dosemu log.

    dosemu -dumb -K . -l <path>/dos/libtmp.so -g 1

Or restub the game once and start it directly:

    ./restub286 dos/run286.exe GAME.EXE GAME286.EXE

## Memory

The programs care where their memory lands, so the host's layout matters.
BioForge builds its arena out of blocks it asks for one at a time and keeps
only the ones ending below linear 30Mb, so DPMI memory has to be reachable
below that. DosAllocLinMem takes its blocks from the linear pool DPMI 1.0
keeps below `$_dpmi_base`, which means `$_dpmi_base` itself has to be under
the ceiling. A layout that works under dosemu2:

    $_ext_mem = (1024)
    $_xms = (1024)
    $_dpmi_base = (0x1e00000)

With the default 32Mb base the same run reports Largest=0 and the game
gives up on its arena.

## Data files

The games read their own data through DOS, so they have to be started in
their installed directory, not next to a lone `.EXE`. BioForge takes every
path from `RED.OPT`, whose `Dir=` line normally names the drive the
installer wrote.

BioForge wants the directory `Dir=` names to sit on a drive presented as a
CDROM, or it stops at its own CD check; a plain directory does not satisfy
it, even under the right volume label. It also wants `GameDir=` somewhere
writable, since it builds its savegame directory there on the way up. With

    $_hostfs_drives = "/path/to/BIOFORGE:c"

and `Dir=` pointing into that drive it gets through the check, initialises
its screen, palette, sound, music and both its interrupt handlers, and
copies its initial game state into `GameDir\gamedat`.

## Parser status

```
$ ./nedump BIOFORGE.EXE
  real-mode stub  0..0xf71e (63262 bytes)
  extender image  0xf71e..0x339c0 (148130 bytes, 'P2')
  DLL directory   0x339c0, 3 entries: doscalls@0x33a90 phapi@0x36090 int33@0x36290
  program image   0x36f70
NE: exetyp 0x81 flags 0x9 segments 84 modules 3 align 512
relocations: 12557 records, 12557 locations patched
    unresolved 0, out of range 0
```

Same for `u8.exe` (149 segments, 12113 records) and `CRUSADER.EXE`
(145 segments, 11898 records).
