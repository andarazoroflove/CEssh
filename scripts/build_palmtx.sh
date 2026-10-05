#!/bin/bash
set -e

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$REPO_ROOT"

echo "=== CEssh Palm T|X / Palm OS Garnet Build ==="

BUILD_DIR="$REPO_ROOT/build/palmos"
mkdir -p "$BUILD_DIR"

BEARSSL_DIR="$REPO_ROOT/src/lib/bearssl"

# Palm OS SDK and CFLAGS
CFLAGS="-Os -palmos4 -DPALMOS=1 \
    -I$REPO_ROOT/src \
    -I$REPO_ROOT/src/palmos \
    -I$BEARSSL_DIR/inc \
    -I$BEARSSL_DIR/src \
    -fno-builtin-printf -fno-builtin-fprintf"

echo "[1/4] Compiling BearSSL crypto modules for Palm OS 68K..."
m68k-palmos-gcc $CFLAGS -c $BEARSSL_DIR/src/symcipher/aes_big_ctr.c -o $BUILD_DIR/aes_big_ctr.o
m68k-palmos-gcc $CFLAGS -c $BEARSSL_DIR/src/symcipher/aes_big_enc.c -o $BUILD_DIR/aes_big_enc.o
m68k-palmos-gcc $CFLAGS -c $BEARSSL_DIR/src/symcipher/aes_common.c -o $BUILD_DIR/aes_common.o
m68k-palmos-gcc $CFLAGS -c $BEARSSL_DIR/src/hash/sha2small.c -o $BUILD_DIR/sha2small.o
m68k-palmos-gcc $CFLAGS -c $BEARSSL_DIR/src/mac/hmac.c -o $BUILD_DIR/hmac.o
m68k-palmos-gcc $CFLAGS -c $BEARSSL_DIR/src/codec/ccopy.c -o $BUILD_DIR/ccopy.o
m68k-palmos-gcc $CFLAGS -c $BEARSSL_DIR/src/codec/dec32be.c -o $BUILD_DIR/dec32be.o
m68k-palmos-gcc $CFLAGS -c $BEARSSL_DIR/src/codec/enc32be.c -o $BUILD_DIR/enc32be.o
m68k-palmos-gcc $CFLAGS -c $BEARSSL_DIR/src/ec/ec_c25519_m31.c -o $BUILD_DIR/ec_c25519_m31.o

echo "[2/4] Compiling SSH and Palm OS networking / UI modules..."
m68k-palmos-gcc $CFLAGS -c src/ssh/ssh_buf.c -o $BUILD_DIR/ssh_buf.o
m68k-palmos-gcc $CFLAGS -c src/ssh/ssh_crypto.c -o $BUILD_DIR/ssh_crypto.o
m68k-palmos-gcc $CFLAGS -c src/ssh/ssh2.c -o $BUILD_DIR/ssh2.o
m68k-palmos-gcc $CFLAGS -c src/palmos/palmos_net.c -o $BUILD_DIR/palmos_net.o
m68k-palmos-gcc $CFLAGS -c src/palmos/palmos_main.c -o $BUILD_DIR/palmos_main.o

echo "[3/4] Compiling Palm OS resources with pilrc..."
pilrc -ro -I src/palmos src/palmos/cessh.rcp $BUILD_DIR/cessh.ro

echo "[4/4] Linking Palm OS binary and generating PRC..."
OBJS="$BUILD_DIR/palmos_main.o \
      $BUILD_DIR/palmos_net.o \
      $BUILD_DIR/ssh2.o \
      $BUILD_DIR/ssh_crypto.o \
      $BUILD_DIR/ssh_buf.o \
      $BUILD_DIR/aes_big_ctr.o \
      $BUILD_DIR/aes_big_enc.o \
      $BUILD_DIR/aes_common.o \
      $BUILD_DIR/sha2small.o \
      $BUILD_DIR/hmac.o \
      $BUILD_DIR/ccopy.o \
      $BUILD_DIR/dec32be.o \
      $BUILD_DIR/enc32be.o \
      $BUILD_DIR/ec_c25519_m31.o"

m68k-palmos-gcc -palmos4 $OBJS -o $BUILD_DIR/cessh_app

build-prc -o build/CEssh.prc -c Cssh -n "CEssh" $BUILD_DIR/cessh.ro $BUILD_DIR/cessh_app

# Packaging zip for Palm T|X deployment
mkdir -p "$BUILD_DIR/package/Palm/Launcher"
cp build/CEssh.prc "$BUILD_DIR/package/Palm/Launcher/CEssh.prc"
cat << 'EOF' > "$BUILD_DIR/package/README-PalmTX.txt"
CEssh v0.9a for Palm T|X and Palm OS Garnet 5.4.9
=================================================

Installation options:
1. SD / CompactFlash Card:
   Copy the 'Palm' folder onto the root of your SD card:
   /Palm/Launcher/CEssh.prc
   Insert card into your Palm T|X - CEssh appears in your launcher.

2. Palm Desktop / HotSync:
   Use Palm Desktop Install Tool or QuickInstall to install CEssh.prc
   during your next HotSync.

Features:
- Pure SSH-2 client: Curve25519 Key Exchange, AES-128-CTR encryption,
  HMAC-SHA2-256 integrity, BearSSL crypto engine.
- Wi-Fi and NetLib integration for instant connectivity on Palm T|X.
- Full ANSI terminal screen emulator with Graffiti text entry,
  on-screen keyboard, and 5-way navigator support.
- Built-in commands:
  - ssh <host> [user] [pass]
  - ping <host>
  - clear
  - help
  - exit

Keyboard Shortcuts:
- Ctrl+] or Menu -> Disconnect: Disconnect active SSH session.
- Menu -> Clear Screen: Clear terminal screen.
EOF

python3 -c "import shutil; shutil.make_archive('$REPO_ROOT/build/cessh-palmtx', 'zip', '$BUILD_DIR/package')"

echo "=== Palm T|X Build Complete ==="
ls -lh build/CEssh.prc build/cessh-palmtx.zip
