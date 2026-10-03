#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "path_util.h"
#include "../ui/terminal.h"

static wchar_t s_app_dirW[MAX_PATH] = {0};
static char    s_app_dirA[MAX_PATH] = {0};
static bool    s_inited = false;

void path_util_init(void) {
    if (s_inited) return;

    wchar_t mod_path[MAX_PATH];
    if (GetModuleFileNameW(NULL, mod_path, MAX_PATH)) {
        wchar_t *p = NULL;
        for (int i = 0; mod_path[i] != L'\0'; i++) {
            if (mod_path[i] == L'\\' || mod_path[i] == L'/') {
                p = &mod_path[i];
            }
        }
        if (p) {
            *(p + 1) = L'\0';
        }
        wcscpy(s_app_dirW, mod_path);

        for (int i = 0; i < MAX_PATH; i++) {
            s_app_dirA[i] = (char)s_app_dirW[i];
            if (s_app_dirW[i] == L'\0') break;
        }
    } else {
        wcscpy(s_app_dirW, L"\\");
        strcpy(s_app_dirA, "\\");
    }

    s_inited = true;
}

const char *path_get_app_dirA(void) {
    path_util_init();
    return s_app_dirA;
}

const wchar_t *path_get_app_dirW(void) {
    path_util_init();
    return s_app_dirW;
}

static const char *strip_usr_prefix(const char *path) {
    if (!path) return "";
    while (*path == ' ' || *path == '\t') path++;

    /* Strip leading / or \ */
    if (*path == '/' || *path == '\\') path++;

    /* Check "usr/" or "usr\" */
    if ((path[0] == 'u' || path[0] == 'U') &&
        (path[1] == 's' || path[1] == 'S') &&
        (path[2] == 'r' || path[2] == 'R') &&
        (path[3] == '/' || path[3] == '\\')) {
        path += 4;
    }

    /* Strip any remaining leading slashes */
    while (*path == '/' || *path == '\\') path++;

    return path;
}

void path_resolve_usr(const char *in_path, wchar_t *out_pathW, char *out_pathA, size_t max_len) {
    path_util_init();
    const char *rel = strip_usr_prefix(in_path);
    if (!rel || *rel == '\0') rel = "file.txt";

    /* Build WCHAR path */
    if (out_pathW) {
        wcscpy(out_pathW, s_app_dirW);
        int len = wcslen(out_pathW);
        for (int i = 0; rel[i] != '\0' && (len + i) < (int)(max_len - 1); i++) {
            wchar_t wc = (rel[i] == '/') ? L'\\' : (wchar_t)rel[i];
            out_pathW[len + i] = wc;
            out_pathW[len + i + 1] = L'\0';
        }
    }

    /* Build ANSI path */
    if (out_pathA) {
        strncpy(out_pathA, s_app_dirA, max_len - 1);
        out_pathA[max_len - 1] = '\0';
        size_t len = strlen(out_pathA);
        for (int i = 0; rel[i] != '\0' && (len + i) < (max_len - 1); i++) {
            char c = (rel[i] == '/') ? '\\' : rel[i];
            out_pathA[len + i] = c;
            out_pathA[len + i + 1] = '\0';
        }
    }
}

void path_list_usr(const char *filter) {
    path_util_init();

    wchar_t search_pattern[MAX_PATH];
    wcscpy(search_pattern, s_app_dirW);
    int len = wcslen(search_pattern);

    const char *f = filter;
    if (f) f = strip_usr_prefix(f);
    if (!f || *f == '\0') f = "*.*";

    for (int i = 0; f[i] != '\0' && (len + i) < (MAX_PATH - 1); i++) {
        search_pattern[len + i] = (wchar_t)f[i];
        search_pattern[len + i + 1] = L'\0';
    }

    term_printf("\r\nDirectory of usr/ (%s):\r\n", s_app_dirA);
    term_puts("--------------------------------------------------------------------------------\r\n");

    WIN32_FIND_DATAW fd;
    HANDLE hFind = FindFirstFileW(search_pattern, &fd);
    if (hFind == INVALID_HANDLE_VALUE) {
        term_puts("  (No files found)\r\n\r\n");
        return;
    }

    int count = 0;
    uint32_t total_bytes = 0;

    do {
        char name[MAX_PATH];
        for (int i = 0; i < MAX_PATH; i++) {
            name[i] = (char)fd.cFileName[i];
            if (fd.cFileName[i] == L'\0') break;
        }

        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            term_printf("  <DIR>         %-30s\r\n", name);
        } else {
            uint32_t sz = fd.nFileSizeLow;
            total_bytes += sz;
            term_printf("  %8lu B   %-30s\r\n", (unsigned long)sz, name);
        }
        count++;
    } while (FindNextFileW(hFind, &fd));

    FindClose(hFind);
    term_puts("--------------------------------------------------------------------------------\r\n");
    term_printf("  %d File(s), %lu Total Bytes\r\n\r\n", count, (unsigned long)total_bytes);
}
