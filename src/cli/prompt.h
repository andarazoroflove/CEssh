#ifndef CESSH_PROMPT_H
#define CESSH_PROMPT_H

#include <stdbool.h>

void prompt_init(void);
void prompt_handle_char(char c);
void prompt_handle_keydown(int vk);
bool prompt_is_ssh_active(void);
void prompt_end_ssh(void);

#endif /* CESSH_PROMPT_H */
