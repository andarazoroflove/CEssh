#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#include "tar.h"
#include "path_util.h"
#include "../ui/terminal.h"

#define TAR_BLOCK_SIZE 512

typedef struct {
    char name[100];
    char mode[8];
    char uid[8];
    char gid[8];
    char size[12];
    char mtime[12];
    char chksum[8];
    char typeflag;
    char linkname[100];
    char magic[6];
    char version[2];
    char uname[32];
    char gname[32];
    char devmajor[8];
    char devminor[8];
    char prefix[155];
    char pad[12];
} tar_header_t;

static void format_octal(char *buf, size_t len, uint32_t val) {
    if (len == 0) return;
    buf[len - 1] = '\0';
    for (int i = (int)len - 2; i >= 0; i--) {
        buf[i] = '0' + (val & 7);
        val >>= 3;
    }
}

static uint32_t parse_octal(const char *buf, size_t len) {
    uint32_t val = 0;
    while (len > 0 && (*buf == ' ' || *buf == '\0')) {
        buf++;
        len--;
    }
    while (len > 0 && *buf >= '0' && *buf <= '7') {
        val = (val << 3) + (*buf - '0');
        buf++;
        len--;
    }
    return val;
}

static uint32_t calc_checksum(const tar_header_t *h) {
    const uint8_t *p = (const uint8_t *)h;
    uint32_t sum = 0;
    for (int i = 0; i < TAR_BLOCK_SIZE; i++) {
        if (i >= 148 && i < 156) {
            sum += ' ';
        } else {
            sum += p[i];
        }
    }
    return sum;
}

static const char *get_basename(const char *path) {
    if (!path) return "file";
    const char *p = strrchr(path, '/');
    if (!p) p = strrchr(path, '\\');
    return p ? (p + 1) : path;
}

int tar_create_cmd(int argc, char **argv) {
    if (argc < 3) {
        term_puts("Usage: tarc <archive.tar> <file1> [file2 ...]\r\n");
        return -1;
    }

    const char *tar_name = argv[1];
    wchar_t tar_pathW[MAX_PATH];
    path_resolve_usr(tar_name, tar_pathW, NULL, 0);

    HANDLE hTar = CreateFileW(tar_pathW, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hTar == INVALID_HANDLE_VALUE) {
        term_printf("Error: Could not create archive 'usr/%s'\r\n", tar_name);
        return -1;
    }

    int files_added = 0;

    for (int i = 2; i < argc; i++) {
        const char *src_file = argv[i];
        wchar_t src_pathW[MAX_PATH];
        path_resolve_usr(src_file, src_pathW, NULL, 0);

        HANDLE hSrc = CreateFileW(src_pathW, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        if (hSrc == INVALID_HANDLE_VALUE) {
            term_printf("tarc: warning: cannot open '%s' (skipping)\r\n", src_file);
            continue;
        }

        DWORD file_size = GetFileSize(hSrc, NULL);
        if (file_size == INVALID_FILE_SIZE) file_size = 0;

        tar_header_t hdr;
        memset(&hdr, 0, sizeof(hdr));

        const char *base = get_basename(src_file);
        strncpy(hdr.name, base, sizeof(hdr.name) - 1);
        format_octal(hdr.mode, sizeof(hdr.mode), 0644);
        format_octal(hdr.uid, sizeof(hdr.uid), 0);
        format_octal(hdr.gid, sizeof(hdr.gid), 0);
        format_octal(hdr.size, sizeof(hdr.size), (uint32_t)file_size);
        format_octal(hdr.mtime, sizeof(hdr.mtime), 1700000000);
        hdr.typeflag = '0'; /* Regular file */
        memcpy(hdr.magic, "ustar", 5);
        hdr.magic[5] = '\0';
        memcpy(hdr.version, "00", 2);
        strncpy(hdr.uname, "root", sizeof(hdr.uname) - 1);
        strncpy(hdr.gname, "root", sizeof(hdr.gname) - 1);

        uint32_t chk = calc_checksum(&hdr);
        format_octal(hdr.chksum, 7, chk);
        hdr.chksum[6] = '\0';
        hdr.chksum[7] = ' ';

        DWORD written = 0;
        WriteFile(hTar, &hdr, TAR_BLOCK_SIZE, &written, NULL);

        /* Write file content */
        uint8_t block[TAR_BLOCK_SIZE];
        DWORD bytes_left = file_size;
        while (bytes_left > 0) {
            DWORD to_read = (bytes_left > TAR_BLOCK_SIZE) ? TAR_BLOCK_SIZE : bytes_left;
            memset(block, 0, TAR_BLOCK_SIZE);
            DWORD read_bytes = 0;
            ReadFile(hSrc, block, to_read, &read_bytes, NULL);
            WriteFile(hTar, block, TAR_BLOCK_SIZE, &written, NULL);
            bytes_left -= to_read;
        }

        CloseHandle(hSrc);
        term_printf("a %s\r\n", base);
        files_added++;
    }

    /* Append two 512-byte blocks of zeroes (tar EOF marker) */
    uint8_t zero_block[TAR_BLOCK_SIZE * 2];
    memset(zero_block, 0, sizeof(zero_block));
    DWORD written = 0;
    WriteFile(hTar, zero_block, sizeof(zero_block), &written, NULL);
    CloseHandle(hTar);

    term_printf("\r\nArchive created: usr/%s (%d files)\r\n\r\n", tar_name, files_added);
    return 0;
}

int tar_extract_cmd(int argc, char **argv) {
    if (argc < 2) {
        term_puts("Usage: tarx <archive.tar>\r\n");
        return -1;
    }

    const char *tar_name = argv[1];
    wchar_t tar_pathW[MAX_PATH];
    path_resolve_usr(tar_name, tar_pathW, NULL, 0);

    HANDLE hTar = CreateFileW(tar_pathW, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hTar == INVALID_HANDLE_VALUE) {
        term_printf("Error: Could not open archive 'usr/%s'\r\n", tar_name);
        return -1;
    }

    int files_extracted = 0;
    tar_header_t hdr;
    DWORD read_bytes = 0;

    while (ReadFile(hTar, &hdr, TAR_BLOCK_SIZE, &read_bytes, NULL) && read_bytes == TAR_BLOCK_SIZE) {
        /* Check for end of archive (block of all zeroes) */
        if (hdr.name[0] == '\0') {
            break;
        }

        /* Check ustar magic */
        if (memcmp(hdr.magic, "ustar", 5) != 0 && hdr.magic[0] != '\0') {
            /* Old tar format without magic is also possible */
        }

        uint32_t file_size = parse_octal(hdr.size, sizeof(hdr.size));
        const char *base = get_basename(hdr.name);

        wchar_t out_pathW[MAX_PATH];
        path_resolve_usr(base, out_pathW, NULL, 0);

        if (hdr.typeflag == '5' || hdr.name[strlen(hdr.name) - 1] == '/') {
            /* Directory */
            CreateDirectoryW(out_pathW, NULL);
            term_printf("x %s/ (dir)\r\n", base);
        } else {
            /* Regular file */
            HANDLE hOut = CreateFileW(out_pathW, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
            DWORD bytes_left = file_size;
            uint8_t block[TAR_BLOCK_SIZE];

            while (bytes_left > 0) {
                DWORD to_read = (bytes_left > TAR_BLOCK_SIZE) ? TAR_BLOCK_SIZE : bytes_left;
                DWORD r = 0;
                ReadFile(hTar, block, TAR_BLOCK_SIZE, &r, NULL);
                if (hOut != INVALID_HANDLE_VALUE) {
                    DWORD w = 0;
                    WriteFile(hOut, block, to_read, &w, NULL);
                }
                bytes_left -= to_read;
            }

            if (hOut != INVALID_HANDLE_VALUE) {
                CloseHandle(hOut);
            }
            term_printf("x %s (%lu bytes)\r\n", base, (unsigned long)file_size);
            files_extracted++;
        }
    }

    CloseHandle(hTar);
    term_printf("\r\nExtracted %d file(s) from usr/%s\r\n\r\n", files_extracted, tar_name);
    return 0;
}
