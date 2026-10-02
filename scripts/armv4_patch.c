#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

/*
 * armv4_patch: converts any remaining ARMv4T Thumb interworking instructions
 * (bx{cond} Rm) in a PE/COFF binary into pure ARMv4 (mov{cond} pc, Rm).
 *
 * StrongARM SA-1110 (HP Jornada 720) does not support bx/blx instructions.
 */

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <binary>\n", argv[0]);
        return 1;
    }

    FILE *f = fopen(argv[1], "rb+");
    if (!f) {
        perror("fopen");
        return 1;
    }

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    uint8_t *buf = (uint8_t *)malloc(size);
    if (!buf) {
        fprintf(stderr, "malloc failed\n");
        fclose(f);
        return 1;
    }

    if (fread(buf, 1, size, f) != (size_t)size) {
        fprintf(stderr, "fread failed\n");
        free(buf);
        fclose(f);
        return 1;
    }

    int count = 0;
    for (long i = 0; i <= size - 4; i += 4) {
        uint32_t inst = (uint32_t)buf[i] |
                        ((uint32_t)buf[i + 1] << 8) |
                        ((uint32_t)buf[i + 2] << 16) |
                        ((uint32_t)buf[i + 3] << 24);

        /* Match bx{cond} Rm: bits [27:4] == 0x012fff1 */
        if ((inst & 0x0ffffff0) == 0x012fff10) {
            uint32_t cond = inst & 0xf0000000;
            uint32_t rm   = inst & 0x0000000f;
            /* Replace with mov{cond} pc, Rm: bits [27:4] = 0x01a0f00 */
            uint32_t new_inst = cond | 0x01a0f000 | rm;

            buf[i]     = (uint8_t)(new_inst & 0xff);
            buf[i + 1] = (uint8_t)((new_inst >> 8) & 0xff);
            buf[i + 2] = (uint8_t)((new_inst >> 16) & 0xff);
            buf[i + 3] = (uint8_t)((new_inst >> 24) & 0xff);
            count++;
        }
    }

    fseek(f, 0, SEEK_SET);
    fwrite(buf, 1, size, f);
    fclose(f);
    free(buf);

    printf("armv4_patch: successfully converted %d bx instruction(s) to mov pc, Rm\n", count);
    return 0;
}
