/*
 * The trace to the dosemu log. The dosemu helper interrupt, int E6h with
 * AL=13h, prints the string at ES:EDX, and dosemu's DOS API translator
 * takes it straight from protected mode: no DPMI call, no DOS memory, and
 * nothing that could re-enter DOS from the program's interrupt handlers.
 *
 * MIT license, see LICENSE.
 */
#include <libc/asmdefs.h>
#include "asm.h"

.text

/* void dosemu_log(const char *s) */
.global _dosemu_log
_dosemu_log:
    pushl %es
    movl 8(%esp), %edx
    movl %ds, %eax
    movl %eax, %es
    movl $0x13, %eax
    int $0xe6
    popl %es
    ret
