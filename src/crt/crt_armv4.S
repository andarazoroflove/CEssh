    .arm
    .syntax unified
    .text

    .global WinMainCRTStartup
    .global mainCRTStartup

/*
 * Pure ARMv4 freestanding entry point for Windows CE (HP Jornada 720 / StrongARM SA-1110).
 *
 * StrongARM SA-1110 does NOT support Thumb mode and has NO bx / blx instructions.
 * CeGCC's default crt3.o and libmingw32.a use bx lr / bx r2 / bx r3, which triggers
 * an Undefined Instruction hardware exception (Vector 4) and immediately crashes before
 * WinMain can execute.
 *
 * This entry point uses strictly pure ARMv4 instructions (mov pc, lr / pop {..., pc}).
 */
WinMainCRTStartup:
mainCRTStartup:
    /* WinCE kernel passes:
       r0 = hInstance
       r1 = hPrevInstance
       r2 = lpCmdLine (LPWSTR)
       r3 = nShowCmd
     */
    stmfd   sp!, {r4, r5, r6, r7, lr}
    mov     r4, r0
    mov     r5, r1
    mov     r6, r2
    mov     r7, r3

    /* Call WinMain directly */
    bl      WinMain

    /* Exit process: TerminateProcess(66, exit_code)
       In Windows CE, (HANDLE)66 is current process pseudo-handle */
    mov     r1, r0
    mov     r0, #66
    bl      TerminateProcess

    /* Fallback halt loop if TerminateProcess returns */
1:  b       1b

/* Freestanding CRT stubs */
    .global exit
exit:
    mov     r1, r0
    mov     r0, #66
    bl      TerminateProcess
2:  b       2b

    .global __gccmain
__gccmain:
    mov     pc, lr

    .global _fpreset
_fpreset:
    mov     pc, lr

    .global _pei386_runtime_relocator
_pei386_runtime_relocator:
    mov     pc, lr

    .global __atexit_init
__atexit_init:
    mov     pc, lr

    .global __atexit_first
__atexit_first:
    mov     r0, #0
    mov     pc, lr

    .global atexit
atexit:
    mov     r0, #0
    mov     pc, lr

    .global _cexit
_cexit:
    mov     pc, lr

    .global _c_exit
_c_exit:
    mov     pc, lr

    .global _onexit
_onexit:
    mov     r0, #0
    mov     pc, lr

    .global __dllonexit
__dllonexit:
    mov     r0, #0
    mov     pc, lr

    .global __dll_exit
__dll_exit:
    mov     pc, lr

    .global __div0
__div0:
    mov     r0, #0
    mov     pc, lr
