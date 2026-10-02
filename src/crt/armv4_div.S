    .arm
    .syntax unified
    .text

/*
 * Pure ARMv4 integer division and modulo routines for StrongARM SA-1110.
 *
 * GCC libgcc.a contains ARMv4T Thumb interworking (bx lr), which triggers
 * hardware exceptions on pure ARMv4 StrongARM SA-1110.
 * Providing these symbols directly prevents libgcc from linking them.
 */

    .global __aeabi_uidivmod
    .global __aeabi_uidiv
    .global __udivsi3
    .global __umodsi3

/*
 * Unsigned 32-bit divide & modulo:
 * Input:
 *   r0: numerator (dividend)
 *   r1: denominator (divisor)
 * Output:
 *   __aeabi_uidiv:    r0 = quotient
 *   __aeabi_uidivmod: r0 = quotient, r1 = remainder
 *   __udivsi3:        r0 = quotient
 *   __umodsi3:        r0 = remainder
 */
__aeabi_uidivmod:
__aeabi_uidiv:
__udivsi3:
    cmp     r1, #0
    beq     .Luidiv_by_zero

    cmp     r0, r1
    bcc     .Luidiv_less
    beq     .Luidiv_equal

    stmfd   sp!, {r4, r5, lr}

    mov     r2, r0          /* r2 = dividend / running remainder */
    mov     r3, r1          /* r3 = divisor */
    mov     r0, #0          /* r0 = quotient */

    mov     r4, #1          /* r4 = quotient bit */
.Luidiv_norm:
    cmp     r3, #0x80000000
    bcs     .Luidiv_sub
    cmp     r3, r2
    bcs     .Luidiv_sub
    mov     r3, r3, lsl #1
    mov     r4, r4, lsl #1
    b       .Luidiv_norm

.Luidiv_sub:
    cmp     r2, r3
    subcs   r2, r2, r3
    orrcs   r0, r0, r4
    movs    r4, r4, lsr #1
    movne   r3, r3, lsr #1
    bne     .Luidiv_sub

    mov     r1, r2          /* r1 = remainder */
    ldmfd   sp!, {r4, r5, pc}

.Luidiv_less:
    mov     r1, r0          /* remainder = dividend */
    mov     r0, #0          /* quotient = 0 */
    mov     pc, lr

.Luidiv_equal:
    mov     r0, #1          /* quotient = 1 */
    mov     r1, #0          /* remainder = 0 */
    mov     pc, lr

.Luidiv_by_zero:
    mov     r0, #0
    mov     r1, #0
    mov     pc, lr

__umodsi3:
    stmfd   sp!, {lr}
    bl      __aeabi_uidivmod
    mov     r0, r1          /* return remainder in r0 */
    ldmfd   sp!, {pc}

/*
 * Signed 32-bit divide & modulo:
 * Input:
 *   r0: numerator
 *   r1: denominator
 * Output:
 *   __aeabi_idiv:    r0 = quotient
 *   __aeabi_idivmod: r0 = quotient, r1 = remainder
 *   __divsi3:        r0 = quotient
 *   __modsi3:        r0 = remainder
 */
    .global __aeabi_idivmod
    .global __aeabi_idiv
    .global __divsi3
    .global __modsi3

__aeabi_idivmod:
__aeabi_idiv:
__divsi3:
    cmp     r1, #0
    beq     .Lidiv_by_zero

    stmfd   sp!, {r4, r5, lr}
    mov     r4, #0          /* r4 = sign of quotient: 0 pos, 1 neg */
    mov     r5, #0          /* r5 = sign of remainder: 0 pos, 1 neg */

    cmp     r0, #0
    bge     .Lnum_pos
    rsb     r0, r0, #0      /* r0 = -r0 */
    eor     r4, r4, #1
    mov     r5, #1          /* remainder has sign of dividend */
.Lnum_pos:

    cmp     r1, #0
    bge     .Lden_pos
    rsb     r1, r1, #0      /* r1 = -r1 */
    eor     r4, r4, #1
.Lden_pos:

    bl      __aeabi_uidivmod

    cmp     r4, #0
    rsbne   r0, r0, #0      /* negate quotient if negative */

    cmp     r5, #0
    rsbne   r1, r1, #0      /* negate remainder if negative */

    ldmfd   sp!, {r4, r5, pc}

.Lidiv_by_zero:
    mov     r0, #0
    mov     r1, #0
    mov     pc, lr

__modsi3:
    stmfd   sp!, {lr}
    bl      __aeabi_idivmod
    mov     r0, r1          /* return remainder in r0 */
    ldmfd   sp!, {pc}

/*
 * 64-bit unsigned divide & modulo (ARM EABI):
 * Input:
 *   r0:r1 = numerator (low:high)
 *   r2:r3 = denominator (low:high)
 * Output:
 *   r0:r1 = quotient (low:high)
 *   r2:r3 = remainder (low:high)
 */
    .global __aeabi_uldivmod
__aeabi_uldivmod:
    stmfd   sp!, {r4, r5, lr}
    sub     sp, sp, #12     /* 8 bytes for remainder buffer, 4 bytes padding */
    add     r4, sp, #4      /* r4 = &rem */
    str     r4, [sp, #0]    /* pass 5th argument (&rem) on stack */
    bl      __udivmoddi4
    ldr     r2, [sp, #4]    /* r2 = rem low */
    ldr     r3, [sp, #8]    /* r3 = rem high */
    add     sp, sp, #12
    ldmfd   sp!, {r4, r5, pc}

/*
 * 64-bit signed divide & modulo (ARM EABI):
 * Input:
 *   r0:r1 = numerator (low:high)
 *   r2:r3 = denominator (low:high)
 * Output:
 *   r0:r1 = quotient (low:high)
 *   r2:r3 = remainder (low:high)
 */
    .global __aeabi_ldivmod
__aeabi_ldivmod:
    stmfd   sp!, {r4, r5, r6, r7, lr}
    mov     r4, #0          /* quotient sign */
    mov     r5, #0          /* remainder sign */

    cmp     r1, #0
    bge     .Lldiv_num_pos
    rsbs    r0, r0, #0
    rsc     r1, r1, #0
    eor     r4, r4, #1
    mov     r5, #1
.Lldiv_num_pos:

    cmp     r3, #0
    bge     .Lldiv_den_pos
    rsbs    r2, r2, #0
    rsc     r3, r3, #0
    eor     r4, r4, #1
.Lldiv_den_pos:

    bl      __aeabi_uldivmod

    cmp     r4, #0
    beq     .Lldiv_rem_check
    rsbs    r0, r0, #0
    rsc     r1, r1, #0

.Lldiv_rem_check:
    cmp     r5, #0
    beq     .Lldiv_done
    rsbs    r2, r2, #0
    rsc     r3, r3, #0

.Lldiv_done:
    ldmfd   sp!, {r4, r5, r6, r7, pc}
