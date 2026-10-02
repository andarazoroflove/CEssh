# CEssh: Native Windows CE 3.0 SSH Client for HP Jornada 720

**CEssh** is a modern SSH-2 client engineered specifically for the **HP Jornada 720** Handheld PC running **Windows for Handheld PC 2000 (Windows CE 3.0)** on the **Intel StrongARM SA-1110 (206 MHz)**.

It provides a full-duplex interactive terminal to control modern Linux machines over Wi-Fi (such as an Orinoco Gold 802.11b card or CompactFlash Ethernet) directly from your Jornada pocket computer.

---

## Features
- **High-Contrast Reflective Display Engine**: Crisp black text on a pure white background (`#FFFFFF`) designed for readability on the Jornada 720's 640x240 color STN LCD.
- **Pixel-Perfect 80x24 VT100 Terminal**: Custom embedded bitmap font rendered into 8x10 cells, giving standard Linux terminal dimensions (`80 columns x 24 rows`) that fill the 640x240 screen without clipping or letterboxing.
- **Interactive `?/` Command Prompt**: Boots instantly into an interactive command prompt accepting commands like `ssh user@host`, `ping`, `ip`, `clear`, and `theme`.
- **Modern Embedded Cryptography (BearSSL)**:
  - **Key Exchange**: `curve25519-sha256`, `curve25519-sha256@libssh.org` (RFC 8731)
  - **Symmetric Cipher**: `aes128-ctr` (RFC 4344 128-bit streaming)
  - **Message Authentication**: `hmac-sha2-256` (RFC 6668)
  - **Host Key Support**: `ssh-ed25519`, `ecdsa-sha2-nistp256`, `rsa-sha2-256`, `ssh-rsa`
  - **Authentication**: Modern password authentication with `*` echo masking
  - **Interactive PTY**: VT100/ANSI streaming channel supporting `bash`, `nano`, `vi`, `htop`, etc.
- **Standard Windows CE Winsock Hooks**: Dynamically loads `ws2.dll` / `winsock.dll` via Unicode `GetProcAddressW`, compatible with any CE networking adapter (Orinoco Gold, Socket Communications CF, Proxim, etc.).
- **Pure StrongARM SA-1110 ARMv4 Compliance**:
  - Zero `bx` / `blx` instructions (100% immune to Vector 4 Undefined Instruction exceptions on SA-1110 cores without Thumb mode).
  - Standalone freestanding CRT and 64-bit integer division routines.
  - Zero missing dependencies: imports exclusively from `COREDLL.dll`.
  - Tiny memory footprint: Only **107 KB** executable size!

---

## Quick Start / Installation

1. Download [`build/cessh-j720-StorageCard.zip`](build/cessh-j720-StorageCard.zip).
2. Extract the `CEssh` folder onto your CompactFlash card:
   ```text
   \Storage Card\
       └── CEssh\
           ├── cessh.exe
           └── README.txt
   ```
3. Insert the CompactFlash card into your HP Jornada 720.
4. Open **File Explorer**, navigate to `\Storage Card\CEssh`, and double-tap `cessh.exe`.

---

## Command Reference (`?/` Prompt)

```text
================================================================================
  CEssh v1.0.0 (Windows CE 3.0 / HPC 2000)
  HP Jornada 720 StrongARM SA-1110 SSH-2 Terminal (80x24 VT100)
================================================================================
Type 'help' for commands or 'ssh [user@]host[:port]' to connect.

?/ 
```

| Command | Description | Example |
| :--- | :--- | :--- |
| `ssh [user@]host[:port]` | Connect to remote SSH-2 server | `ssh root@192.168.1.50`<br>`ssh pi@raspberrypi.local:2222` |
| `ftp [user@]host[:port]` | Interactive FTP client (transfers locked to CEssh folder) | `ftp 192.168.1.50`<br>`ftp user@ftpserver.local` |
| `ping <host> [count]` | Standard ICMP Ping (sends 4 echo packets, reports RTT & loss) | `ping 192.168.1.1`<br>`ping google.com 8` |
| `tcpping <host> [port]` | TCP port reachability & latency probe | `tcpping 192.168.1.50 80` |
| `ip` / `net` | Display Winsock status, local IP, and CEssh folder | `ip` |
| `clear` / `cls` | Clear terminal grid buffer | `clear` |
| `theme` | Toggle between high-contrast black-on-white and dark mode | `theme` |
| `help` / `?` | Display list of supported commands | `help` |
| `exit` / `quit` | Exit application | `exit` |

### FTP Commands (`ftp> ` Prompt)
When connected to an FTP server, CEssh operates in interactive FTP mode. **All transfers are strictly locked to the folder from which CEssh was launched** (e.g. `\Storage Card\CEssh\`):
- `ls` / `dir [path]`: List remote directory contents.
- `get <remote_file> [local_file]`: Download remote file directly into the `CEssh` folder.
- `put <local_file> [remote_file]`: Upload file from the `CEssh` folder to the FTP server.
- `cd <path>`: Change remote working directory.
- `pwd`: Print remote working directory.
- `binary` / `bin`: Enable 8-bit binary transfer mode (default).
- `ascii`: Enable ASCII transfer mode.
- `quote <cmd>`: Send raw FTP protocol commands.
- `bye` / `quit` / `exit`: Close FTP connection and return to `?/` prompt.

### In-Session Keystrokes
- **Keystrokes**: Normal alphanumeric keys, Enter, Backspace, Tab, and Esc are forwarded directly to the remote Linux shell.
- **Arrow Keys**: Handled transparently with ANSI escape sequences (`\x1b[A`, `\x1b[B`, etc.).
- **Disconnect**: Press **`Ctrl + ]`** (ASCII 29) at any time to terminate the active SSH session and return to the `?/` prompt.

---

## Building from Source

Build requires Docker and the CeGCC ARMv4 toolchain:

```bash
git clone https://github.com/andarazoroflove/CEssh.git
cd CEssh
bash scripts/build_j720.sh
```

The script will automatically compile all C and assembly sources, link `cessh.exe`, execute `scripts/armv4_patch.c` to enforce pure ARMv4 opcode compliance, audit the PE import table, and package the release ZIP to `build/cessh-j720-StorageCard.zip`.

---

## License
MIT License. Cryptographic operations powered by [BearSSL](https://bearssl.org/).
