#ifndef CESSH_PATH_UTIL_H
#define CESSH_PATH_UTIL_H

#include <windows.h>
#include <stdbool.h>
#include <stddef.h>

/* Initialize app directory from GetModuleFileName */
void path_util_init(void);

/* Get application directory as ANSI string (e.g. "\Storage Card\CEssh\") */
const char *path_get_app_dirA(void);

/* Get application directory as WCHAR string */
const wchar_t *path_get_app_dirW(void);

/*
 * Resolve any path (e.g. "notes.txt", "usr/notes.txt", "/usr/notes.txt")
 * strictly into the application directory (usr/).
 */
void path_resolve_usr(const char *in_path, wchar_t *out_pathW, char *out_pathA, size_t max_len);

/* List files currently in usr/ (app directory) to terminal */
void path_list_usr(const char *filter);

#endif /* CESSH_PATH_UTIL_H */
