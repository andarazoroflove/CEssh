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
