#!/bin/bash
set -e

if ! command -v arm-mingw32ce-gcc &>/dev/null; then
    echo "arm-mingw32ce-gcc not found in host PATH; invoking via docker container..."
    docker run --rm -v "$(pwd):/work" -w /work 777shuang/docker-cegcc bash scripts/build_axim.sh
    rm -f build/cessh-axim-StorageCard.zip
    if command -v zip &>/dev/null; then
        (cd build && zip -r cessh-axim-StorageCard.zip "Storage Card")
    elif command -v python3 &>/dev/null; then
        python3 -c "import shutil; shutil.make_archive('build/cessh-axim-StorageCard', 'zip', 'build/Storage Card')"
    fi
    exit $?
fi

BUILD_DIR="build/axim"
mkdir -p "$BUILD_DIR"
mkdir -p build

CFLAGS="-O2 -Wall -Wextra \
-march=armv5te -mcpu=xscale -marm \
-Isrc -Isrc/lib/bearssl/inc -Isrc/lib/bearssl/src \
-DCESSH_WINCE=1 -DUNDER_CE=1 -DWIN32=1 \
-DBR_USE_URANDOM=0 -DBR_USE_GETENTROPY=0 -DBR_RDRAND=0 -DBR_AES_X86NI=0 -DBR_SSE2=0 \
-DBR_USE_WIN32_RAND=0 -DBR_USE_WIN32_TIME=0 \
-fno-strict-aliasing"

LDFLAGS="-nostartfiles -nodefaultlibs -Wl,-e,WinMainCRTStartup -lcoredll -lgcc \
-Wl,--major-os-version,3 -Wl,--minor-os-version,0 \
-Wl,--major-subsystem-version,3 -Wl,--minor-subsystem-version,0 \
-Wl,--stack,0x200000"

CC="arm-mingw32ce-gcc"

SRCS_C="
src/crt/freestanding.c
src/net/winsock_ce.c
src/net/ping.c
src/net/ftp.c
src/net/chat_cli.c
src/ui/font.c
src/ui/terminal.c
src/ui/win_main.c
src/ssh/ssh_buf.c
src/ssh/ssh_crypto.c
src/ssh/ssh2.c
src/cli/path_util.c
src/cli/note.c
src/cli/tar.c
src/cli/prompt.c
"

BEARSSL_SRCS="
src/lib/bearssl/src/symcipher/aes_big_ctr.c
src/lib/bearssl/src/symcipher/aes_big_enc.c
src/lib/bearssl/src/symcipher/aes_common.c
src/lib/bearssl/src/hash/sha2small.c
src/lib/bearssl/src/mac/hmac.c
src/lib/bearssl/src/codec/ccopy.c
src/lib/bearssl/src/codec/dec32be.c
src/lib/bearssl/src/codec/enc32be.c
src/lib/bearssl/src/ec/ec_c25519_m31.c
"

ASM_SRCS="
src/crt/crt_armv4.S
src/crt/armv4_div.S
"

OBJS=""

echo "[1/3] Compiling C object files (-march=armv5te -mcpu=xscale)..."
for src in $SRCS_C $BEARSSL_SRCS; do
    obj="$BUILD_DIR/$(echo "$src" | tr '/.' '__').o"
    if [ ! -f "$obj" ] || [ "$src" -nt "$obj" ]; then
        echo "  CC $src"
        $CC $CFLAGS -c "$src" -o "$obj"
    fi
    OBJS="$OBJS $obj"
done

echo "[2/3] Assembling ARM assembly sources..."
for asm in $ASM_SRCS; do
    obj="$BUILD_DIR/$(echo "$asm" | tr '/.' '__').o"
    if [ ! -f "$obj" ] || [ "$asm" -nt "$obj" ]; then
        echo "  AS $asm"
        $CC $CFLAGS -c "$asm" -o "$obj"
    fi
    OBJS="$OBJS $obj"
done

echo "[3/3] Linking build/cessh-axim.exe (Intel XScale PXA270 ARMv5TE)..."
$CC -o build/cessh-axim.exe $OBJS $LDFLAGS

echo "===> Build successful: build/cessh-axim.exe"
ls -la build/cessh-axim.exe
arm-mingw32ce-objdump -f build/cessh-axim.exe

echo "--> Auditing PE imports in build/cessh-axim.exe..."
arm-mingw32ce-objdump -p build/cessh-axim.exe | grep -E 'DLL Name|GetProcAddress|_ecvt|_fcvt|_isnan|fputc|GetCPInfo' || true

mkdir -p "build/Storage Card/CEssh/usr"
cp build/cessh-axim.exe "build/Storage Card/CEssh/"

cat << 'EOF' > "build/Storage Card/CEssh/README-Axim.txt"
================================================================================
  CEssh v1.1.0 for Dell Axim X50 / X50v / X51v (Windows Mobile / Pocket PC)
================================================================================

Target Hardware:
- Dell Axim X50v / X51v / X50 (Intel XScale PXA270 processor)
- Windows Mobile 2003 Second Edition / Windows Mobile 5.0 / 6.0
- Supports VGA 480x640 portrait, VGA 640x480 landscape, and standard QVGA 240x320

Installation:
1. Copy the entire 'CEssh' folder to your SD card or CF card:
   \Storage Card\CEssh\cessh-axim.exe
2. In File Explorer, tap cessh-axim.exe to run.

Features:
- Dynamically resizes to match portrait (480x640) or landscape (640x480) display
- Full VT100 ANSI terminal with auto-calculated rows and columns
- Interactive '?/' prompt with SSH-2, FTP, IRC chat, note editor, and tar archiver
- Modern cryptography: Curve25519 ECDH, AES128-CTR, HMAC-SHA256
- Zero DLL dependencies outside coredll.dll; dynamic Winsock loading for Wi-Fi

Commands:
  ssh [user@]host[:port]  - Connect to SSH-2 server
  ftp [user@]host[:port]  - Interactive FTP client
  chat [server] [nick]    - Interactive IRC client
  note [filename]         - Full-screen text editor
  tarc <out.tar> <files>  - POSIX tar archive creator
  tarx <archive.tar>      - Extract tar archive
  ls / dir [pattern]      - List files in usr/
  ping <host> [count]     - ICMP Ping
  tcpping <host> [port]   - TCP port connectivity test
  ip / net                - Show local IP and network status
  help                    - Display command list
  exit                    - Exit CEssh
================================================================================
EOF

if command -v zip &>/dev/null; then
    echo "--> Packaging build/cessh-axim-StorageCard.zip..."
    (cd build && rm -f cessh-axim-StorageCard.zip && zip -r cessh-axim-StorageCard.zip "Storage Card")
fi

echo "================================================================================"
echo "  Dell Axim X50v build COMPLETE!"
echo "  Binary: build/Storage Card/CEssh/cessh-axim.exe"
echo "  Zip:    build/cessh-axim-StorageCard.zip"
echo "================================================================================"
