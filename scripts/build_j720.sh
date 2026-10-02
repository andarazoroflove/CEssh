#!/bin/bash
set -e

if ! command -v arm-mingw32ce-gcc &>/dev/null; then
    echo "arm-mingw32ce-gcc not found in host PATH; invoking via docker container..."
    docker run --rm -v "$(pwd):/work" -w /work 777shuang/docker-cegcc bash scripts/build_j720.sh
    rm -f build/cessh-j720-StorageCard.zip
    if command -v zip &>/dev/null; then
        (cd build && zip -r cessh-j720-StorageCard.zip "Storage Card")
    elif command -v python3 &>/dev/null; then
        python3 -c "import shutil; shutil.make_archive('build/cessh-j720-StorageCard', 'zip', 'build/Storage Card')"
    fi
    exit $?
fi

BUILD_DIR="build/wince"
mkdir -p "$BUILD_DIR"
mkdir -p build

CFLAGS="-O2 -Wall -Wextra \
-march=armv4 -mcpu=strongarm -marm \
-Isrc -Isrc/lib/bearssl/inc -Isrc/lib/bearssl/src \
-DCESSH_WINCE=1 -DUNDER_CE=1 -DWIN32=1 \
-DBR_USE_URANDOM=0 -DBR_USE_GETENTROPY=0 -DBR_RDRAND=0 -DBR_AES_X86NI=0 -DBR_SSE2=0 \
-DBR_USE_WIN32_RAND=0 -DBR_USE_WIN32_TIME=0 \
-fno-strict-aliasing"

LDFLAGS="-nostartfiles -nodefaultlibs -Wl,-e,WinMainCRTStartup -lcoredll -lgcc -Wl,--major-os-version,3 -Wl,--minor-os-version,0 -Wl,--major-subsystem-version,3 -Wl,--minor-subsystem-version,0 -Wl,--stack,0x200000"

CC="arm-mingw32ce-gcc"

SRCS_C="
src/crt/freestanding.c
src/net/winsock_ce.c
src/net/ping.c
src/net/ftp.c
src/ui/font.c
src/ui/terminal.c
src/ui/win_main.c
src/ssh/ssh_buf.c
src/ssh/ssh_crypto.c
src/ssh/ssh2.c
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

echo "[1/3] Compiling C object files (-march=armv4 -mcpu=strongarm)..."
for src in $SRCS_C $BEARSSL_SRCS; do
    obj="$BUILD_DIR/$(echo "$src" | tr '/.' '__').o"
    if [ ! -f "$obj" ] || [ "$src" -nt "$obj" ]; then
        echo "  CC $src"
        $CC $CFLAGS -c "$src" -o "$obj"
    fi
    OBJS="$OBJS $obj"
done

echo "[2/3] Assembling ARMv4 assembly sources..."
for asm in $ASM_SRCS; do
    obj="$BUILD_DIR/$(echo "$asm" | tr '/.' '__').o"
    if [ ! -f "$obj" ] || [ "$asm" -nt "$obj" ]; then
        echo "  AS $asm"
        $CC $CFLAGS -c "$asm" -o "$obj"
    fi
    OBJS="$OBJS $obj"
done

echo "[3/3] Linking build/cessh.exe (pure ARMv4, -nostartfiles)..."
$CC -o build/cessh.exe $OBJS $LDFLAGS

echo "===> Build successful: build/cessh.exe"
ls -la build/cessh.exe
arm-mingw32ce-objdump -f build/cessh.exe

echo "--> Compiling and running ARMv4 StrongARM patcher..."
gcc -O2 scripts/armv4_patch.c -o build/armv4_patch
./build/armv4_patch build/cessh.exe

echo "--> Auditing binary for illegal ARMv4 instructions (bx / blx)..."
BX_COUNT=$(arm-mingw32ce-objdump -d build/cessh.exe | grep -c -E '\sbx\s|\sblx\s' || true)
if [ "$BX_COUNT" -ne 0 ]; then
    echo "ERROR: Found $BX_COUNT bx/blx instructions in build/cessh.exe!"
    arm-mingw32ce-objdump -d build/cessh.exe | grep -E '\sbx\s|\sblx\s' | head -n 30
    exit 1
fi
echo "AUDIT PASSED: ZERO bx/blx instructions found in build/cessh.exe! 100% StrongARM SA-1110 safe."

echo "--> Auditing PE imports in build/cessh.exe..."
arm-mingw32ce-objdump -p build/cessh.exe | grep -E 'DLL Name|GetProcAddress|_ecvt|_fcvt|_isnan|fputc|GetCPInfo' || true

mkdir -p "build/Storage Card/CEssh"
cp build/cessh.exe "build/Storage Card/CEssh/"

cat << 'EOF' > "build/Storage Card/CEssh/README.txt"
================================================================================
  CEssh v1.0.0 for HP Jornada 720 (Windows CE 3.0 / HPC 2000)
================================================================================

Installation:
1. Copy the entire 'CEssh' folder to your CompactFlash Storage Card:
   \Storage Card\CEssh\cessh.exe
2. Double-tap cessh.exe from WinCE File Explorer to launch.

Features:
- High-contrast 80x24 VT100 terminal (black on white)
- Native Windows CE look and feel
- Interactive '?/' command prompt
- Modern SSH-2 client with Curve25519 ECDH, AES128-CTR, HMAC-SHA256
- Interactive FTP client (transfers locked to CEssh folder on Storage Card)
- Standard ICMP Ping (4 echo packets with latency statistics)
- Dynamic Winsock hooks for Orinoco Gold 802.11b Wi-Fi card

Commands at '?/' prompt:
  ssh [user@]host[:port]  - Connect to SSH server (e.g. ssh root@192.168.1.50)
  ftp [user@]host[:port]  - Interactive FTP client (get/put/ls/cd/pwd)
  ping <host> [count]     - Standard ICMP ping (default 4 echo packets)
  tcpping <host> [port]   - TCP port connectivity test
  ip / net                - Show Winsock and network status
  clear                   - Clear screen
  theme                   - Toggle black-on-white / white-on-black theme
  help                    - Display command reference
  exit                    - Exit CEssh

Tips:
- Press Ctrl+] during an SSH session to return to '?/'.
- All FTP files are downloaded to or uploaded from \Storage Card\CEssh\.
================================================================================
EOF

if command -v zip &>/dev/null; then
    echo "--> Packaging build/cessh-j720-StorageCard.zip..."
    (cd build && rm -f cessh-j720-StorageCard.zip && zip -r cessh-j720-StorageCard.zip "Storage Card")
fi

echo "================================================================================"
echo "  CEssh build and packaging COMPLETE!"
echo "  Binary: build/Storage Card/CEssh/cessh.exe"
echo "  Zip:    build/cessh-j720-StorageCard.zip"
echo "================================================================================"
