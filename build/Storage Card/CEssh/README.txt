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
