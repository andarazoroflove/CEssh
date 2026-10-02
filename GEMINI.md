# CEssh: Windows CE 3.0 / HP Jornada 720 Technical Reference

## Hardware & OS Specifications
- **Device**: HP Jornada 720 Handheld PC
- **Processor**: Intel StrongARM SA-1110 @ 206 MHz (ARMv4 architecture, rev 8)
- **Display**: 640x240 pixels, 16-bit color (5:6:5 / 5:5:5) reflective color STN LCD with frontlight
- **OS**: Windows for Handheld PC 2000 (Windows CE 3.0 build 9546-126)
- **RAM**: 32 MB SDRAM
- **ROM**: 32 MB Flash ROM
- **Expansion**: CompactFlash Type I, PCMCIA Type II (e.g. Orinoco Gold 802.11b Wi-Fi card)
- **Input**: Built-in 3/4-pitch QWERTY keyboard + resistive touch screen / stylus

---

## StrongARM SA-1110 Architecture & Kernel Rules
1. **Pure ARMv4 (No Thumb, No `bx` / `blx`)**:
   - The StrongARM SA-1110 is a pure ARMv4 core. It does **not** support the Thumb instruction set.
   - Any `bx Rm` (`0xE12FFF1x`) or `blx` instruction will immediately trigger an **Undefined Instruction exception (Vector 4)**, silently killing the process before `WinMain` executes.
   - All function returns must use `mov pc, lr` or `ldmfd sp!, {..., pc}`.
   - CeGCC generated code can inject `bx lr` in `libgcc` helpers or compiler stubs. The post-link patcher `scripts/armv4_patch.c` audits the entire `.text` section and safely converts `bx Rm` to `mov pc, Rm` (`0xE1A0F00x`).

2. **Windows CE 3.0 PE Header & Subsystem Requirements**:
   - `MajorOperatingSystemVersion`: `3`
   - `MinorOperatingSystemVersion`: `0`
   - `MajorSubsystemVersion`: `3`
   - `MinorSubsystemVersion`: `0`
   - `Subsystem`: `9` (`IMAGE_SUBSYSTEM_WINDOWS_CE_GUI`)
   - `ImageBase`: `0x00010000`
   - Stack reserve: `0x200000` (2 MB)

3. **Strict Import Binding (COREDLL.dll Only)**:
   - On Windows CE 3.0, the PE loader validates all imported DLLs and function names at launch time. If even one symbol cannot be found in the target DLL, the application is silently terminated with "Cannot find 'x' or one of its components".
   - **Unicode-Only API**: `COREDLL.dll` only exports `GetProcAddressW`. It does **not** export `GetProcAddressA`. Any reference to `GetProcAddressA` causes launch failure.
   - **No Missing CRT Functions**: Never link against default `libmingwex.a` or `libceoldname.a` without care. They inject calls to `_ecvt`, `_fcvt`, `_isnan`, `fputc`, and `GetCPInfo`, which do not exist in WinCE 3.0 `COREDLL.dll`.
   - Use `-nodefaultlibs -lcoredll -lgcc` and provide freestanding in-binary implementations of `vsnprintf`, `snprintf`, `sprintf`, and `atoi`.

4. **Dynamic Winsock Loading**:
   - Do not statically link `ws2_32.lib` or `wsock32.lib`.
   - Dynamically load `ws2.dll` (with fallback to `winsock.dll`) at runtime using `LoadLibraryW` and `GetProcAddressW` / ordinal lookups. This guarantees seamless compatibility with Orinoco Gold Wi-Fi drivers and Ethernet CF cards.

---

## Terminal Geometry & Visual Layout
- **Screen Resolution**: 640x240 pixels.
- **Font Dimensions**: 8 pixels wide by 10 pixels high (embedded 8x8 font with 1-line top padding and 1-line bottom baseline padding).
- **Terminal Grid**:
  - Horizontal: `640 / 8 = 80` columns.
  - Vertical: `240 / 10 = 24` rows.
  - Exactly matches standard Linux `80x24` VT100 dimensions with zero clipping or letterboxing.
- **High-Contrast Palette**:
  - Canvas / Background: Pure White (`#FFFFFF`).
  - Text / Foreground: Crisp Black (`#000000`).
  - Dark Gray accents for prompts and dim text.
  - Full 16-color ANSI support tuned for legibility on reflective STN LCDs.
  - Invert mode available via `theme` command.

---

## Toolchain & Build Command
Build runs inside the Docker container `777shuang/docker-cegcc`:
```bash
# Compiler flags
CFLAGS="-O2 -Wall -Wextra \
-march=armv4 -mcpu=strongarm -marm \
-Isrc -Isrc/lib/bearssl/inc -Isrc/lib/bearssl/src \
-DCESSH_WINCE=1 -DUNDER_CE=1 -DWIN32=1 \
-DBR_USE_URANDOM=0 -DBR_USE_GETENTROPY=0 -DBR_RDRAND=0 -DBR_AES_X86NI=0 -DBR_SSE2=0 \
-DBR_USE_WIN32_RAND=0 -DBR_USE_WIN32_TIME=0 \
-fno-strict-aliasing"

# Linker flags
LDFLAGS="-nostartfiles -nodefaultlibs -Wl,-e,WinMainCRTStartup -lcoredll -lgcc \
-Wl,--major-os-version,3 -Wl,--minor-os-version,0 \
-Wl,--major-subsystem-version,3 -Wl,--minor-subsystem-version,0 \
-Wl,--stack,0x200000"

# Compile and link
arm-mingw32ce-gcc $CFLAGS -c ...
arm-mingw32ce-gcc -o build/cessh.exe *.o $LDFLAGS

# Patch StrongARM illegal opcodes
./build/armv4_patch build/cessh.exe

# Audit for zero bx/blx instructions
arm-mingw32ce-objdump -d build/cessh.exe | grep -c -E '\sbx\s|\sblx\s'  # Must output 0
```
