/*
 * Ways into pmdapi from its clients.
 *
 * This file must be linked first, right after crt0: the offsets of the
 * stubs go into 16-bit interrupt vectors and far pointers of 16-bit
 * clients, so they have to stay below 64K.
 */
#include "asm.h"

.text
	.p2align 4
.globl stubs
stubs:
	.set i, 0
	.rept STUB_NUM
	.p2align 4
	pushl $i
	jmp common
	.set i, i+1
	.endr
	.p2align 4
/* the DPMI host calls a resident service provider with ds already ours */
.globl rsp_stub16
rsp_stub16:
	pushl $-1
	jmp common_rsp
.globl rsp_stub32
rsp_stub32:
	pushl $-2
	jmp common_rsp

/*
 * int 31h of the clients, for their exception handlers. Ours go straight
 * to the host: we make DPMI calls all the time, from our own code. Only
 * 32-bit clients are served, so the frame is a 32-bit one.
 */
.globl int31_entry
int31_entry:
	pushl %eax
	movw %cs, %ax
	cmpw %ax, 8(%esp)
	jne 1f
	/* Our code, but it is only us on our own stack: the loader that the
	 * resident runs in a program's place shares our code segment, and
	 * its exception handlers have to be recorded like anyone's. */
	cmpl $pmdapi_stack, %esp
	jb 1f
	cmpl $pmdapi_stack + STK_CHUNK * STK_DEPTH, %esp
	jae 1f
	popl %eax
	ljmpl *%cs:int31_prev
1:
	popl %eax
	jmp stubs + ID_INT31_STUB * STUB_SIZE

common:
	pushfl
	pushl %es
	pushl %ds
	pushl %eax
	pushl %ecx
	movl %cs:dseg32, %eax
	jmp have_ds
common_rsp:
	pushfl
	pushl %es
	pushl %ds
	pushl %eax
	pushl %ecx
	movl %ds, %eax
have_ds:
	movw %ax, %ds
	/* take a piece of our stack before using it, so that anything
	 * nesting in from here on goes below it */
	movl cur_sp, %ecx
	subl $STK_CHUNK, cur_sp
	movl %esp, EF_esp-EF_size(%ecx)
	movl $0, EF_ss-EF_size(%ecx)
	movw %ss, EF_ss-EF_size(%ecx)
	leal -EF_size(%ecx), %ecx
	movw %ax, %ss
	movl %ecx, %esp
	/* now copy what we pushed on the client's stack */
	movw EF_ss(%esp), %ax
	movw %ax, %es
	movl EF_esp(%esp), %ecx
	larl %eax, %eax
	testl $0x400000, %eax
	jnz 1f
	movzwl %cx, %ecx
1:
	movl %es:(%ecx), %eax
	movl %eax, PM_ecx(%esp)
	movl %es:4(%ecx), %eax
	movl %eax, PM_eax(%esp)
	movl %es:8(%ecx), %eax
	movw %ax, PM_ds(%esp)
	movl %es:12(%ecx), %eax
	movw %ax, PM_es(%esp)
	movl %es:16(%ecx), %eax
	movl %eax, PM_eflags(%esp)
	movl %es:20(%ecx), %eax
	movl %eax, EF_id(%esp)
	movl %ebx, PM_ebx(%esp)
	movl %edx, PM_edx(%esp)
	movl %esi, PM_esi(%esp)
	movl %edi, PM_edi(%esp)
	movl %ebp, PM_ebp(%esp)
	movw %fs, PM_fs(%esp)
	movw %gs, PM_gs(%esp)
	movl %ds, %eax
	movw %ax, %es
	cld
	movl %esp, %eax
	pushl %eax
	call pmdapi_entry
	addl $4, %esp
	/* pmdapi_entry() has put ebx, ds, eip, cs and eflags on the
	 * client's stack, and its esp points at them */
	movl %esp, %ebx
	movl PM_eax(%ebx), %eax
	movl PM_ecx(%ebx), %ecx
	movl PM_edx(%ebx), %edx
	movl PM_esi(%ebx), %esi
	movl PM_edi(%ebx), %edi
	movl PM_ebp(%ebx), %ebp
	movw PM_es(%ebx), %es
	movw PM_fs(%ebx), %fs
	movw PM_gs(%ebx), %gs
	lss PM_esp(%ebx), %esp
	addl $STK_CHUNK, cur_sp
	popl %ebx
	popl %ds
	iretl

/* a 16-bit stack switch for the real mode helpers is not needed:
 * those are copied to DOS memory, see rmstubs below */

/* Real mode helpers, copied to DOS memory at startup.
 * The data fields are filled from C, see msdoshlp.c. */
	.code16
.globl rm_stubs
rm_stubs:
.globl rm_s_r
rm_s_r:
	.word 0, 0
.globl rm_s_r_len
rm_s_r_len:
	.word 0
/* msdos exec helper: entered with an iret frame, does int 21h/4bh
 * between saving and restoring the DPMI host's state */
.globl rm_exec
rm_exec:
	pushw %bp
	movw %sp, %bp
	subw %cs:rm_s_r_len-rm_stubs, %sp
	pushw %ax
	pushw %es
	pushw %di
	movb $0, %al
	pushw %ss
	popw %es
	movw %bp, %di
	subw %cs:rm_s_r_len-rm_stubs, %di
	lcallw *%cs:rm_s_r-rm_stubs
	popw %di
	popw %es
	popw %ax
	int $0x21
	pushfw
	pushw %ax
	pushw %es
	pushw %di
	movb $1, %al
	pushw %ss
	popw %es
	movw %bp, %di
	subw %cs:rm_s_r_len-rm_stubs, %di
	lcallw *%cs:rm_s_r-rm_stubs
	popw %di
	popw %es
	popw %ax
	popfw
	movw %bp, %sp
	popw %bp
	/* keep the flags int 21h returned */
	lretw $2
/* msdos terminate helper: entered with an iret frame and the PSP in si,
 * makes DOS come back there when the program terminates */
.globl rm_term
rm_term:
	pushw %bp
	movw %sp, %bp
	pushw %ds
	pushw %bx
	movw %si, %ds
	movw 2(%bp), %bx
	movw %bx, 0x0a
	movw 4(%bp), %bx
	movw %bx, 0x0c
	popw %bx
	popw %ds
	popw %bp
	int $0x21
	iretw
.globl rm_stubs_end
rm_stubs_end:
	.code32

/* int ldtmon_call32(eax, ebx, ecx, edx, const __dpmi_paddr *api) */
.globl ldtmon_call32
ldtmon_call32:
	pushl %ebx
	pushl %esi
	movl 12(%esp), %eax
	movl 16(%esp), %ebx
	movl 20(%esp), %ecx
	movl 24(%esp), %edx
	movl 28(%esp), %esi
	lcall *(%esi)
	popl %esi
	popl %ebx
	xorl %eax, %eax
	ret

/* the same for an entry that returns with a 16-bit retf: the return
 * address is below 64K because this file is linked first */
.globl ldtmon_call16
ldtmon_call16:
	pushl %ebx
	pushl %esi
	movl 28(%esp), %esi
	pushw %cs
	movl $1f, %eax
	pushw %ax
	movl 16(%esp), %eax
	movl 20(%esp), %ebx
	movl 24(%esp), %ecx
	movl 28(%esp), %edx
	ljmp *(%esi)
1:
	popl %esi
	popl %ebx
	xorl %eax, %eax
	ret

/*
 * The RSP call of a 16-bit client. The host takes a 16-bit code descriptor
 * and a 16-bit ip for it, and our code is nowhere near the first 64K, so
 * the descriptor we give it is based right here (see dpmi_install_rsp()).
 * A 16-bit client has no selector for our code as 32-bit code, so make
 * one and go on to rsp_stub16 through it.
 */
	.p2align 4
.globl rsp16_tramp
rsp16_tramp:
	.code16
	pushl $-1
	pushl %eax
	pushl %ebx
	pushl %ecx
	pushl %edx
	movw %cs, %ax
	movl $rsp16_sel, %ebx
	movw %ax, (%ebx)
	xorw %ax, %ax
	movw $1, %cx
	int $0x31
	movw %ax, %bx
	.byte 0x66, 0xba	/* movl $imm32, %edx: the base of our code */
.globl rsp16_cs_base
rsp16_cs_base:
	.long 0
	movl %edx, %ecx
	shrl $16, %ecx
	movw $7, %ax
	int $0x31
	movw $0xffff, %cx
	movw $0xffff, %dx
	movw $8, %ax
	int $0x31
	movw $0xc0fb, %cx
	movw $9, %ax
	int $0x31
	movzwl %bx, %ebx
	pushl %ebx
	pushl $1f
	lretl
/* where host_reinit() has the host return to: the host has put aliases
 * of ours with a 64K limit in ds and ss, and returns with a 16-bit retf */
.globl rsp16_reinit_pad
rsp16_reinit_pad:
	movw %di, %ss
	movl %ebp, %esp
	movw %si, %ds
	ljmpl *(%ebx)
	.code32
1:
	popl %edx
	popl %ecx
	popl %ebx
	popl %eax
	jmp common_rsp

/*
 * int host_reinit(unsigned ax, const void *entry, unsigned ss, unsigned esp):
 * the host's DPMI_REINIT, from a 16-bit RSP call, so the host returns with
 * a 16-bit retf. The return lands on rsp16_reinit_pad through the 16-bit
 * code selector the host called us with, and comes back here. The host
 * puts aliases with a 64K limit in ds and ss, so the call is made on a
 * stack that fits one: the locked stack the RSP call came in on, as ss:esp.
 * Returns the carry.
 */
.globl host_reinit
host_reinit:
	pushl %ebp
	pushl %ebx
	pushl %esi
	pushl %edi
	pushl %es
	pushl %fs
	pushl %gs
	movl 32(%esp), %eax
	movl 36(%esp), %edx
	movl 40(%esp), %ecx
	movl 44(%esp), %ebp
	movl %cs, %ebx
	movw %bx, reinit_back+4
	movl $reinit_back, %ebx
	movl %ds, %esi
	movl %ss, %edi
	/* the load of ss holds interrupts off over the next instruction */
	movw %cx, %ss
	xchgl %ebp, %esp
	pushw rsp16_sel
	pushw $(rsp16_reinit_pad - rsp16_tramp)
	ljmpl *(%edx)
reinit_back32:
	sbbl %eax, %eax
	negl %eax
	popl %gs
	popl %fs
	popl %es
	popl %edi
	popl %esi
	popl %ebx
	popl %ebp
	ret

/* int call_on_stack(int (*fn)(void), void *top) */
.globl call_on_stack
call_on_stack:
	pushl %ebp
	movl %esp, %ebp
	movl 8(%ebp), %eax
	movl 12(%ebp), %esp
	call *%eax
	movl %ebp, %esp
	popl %ebp
	ret

/* where a helper sends the client to terminate */
.globl quit_stub
quit_stub:
	int $0x21

.data
	.p2align 2
.globl dseg32
dseg32:
	.long 0
.globl cur_sp
cur_sp:
	.long 0
/* the host's int 31h in the current client: offset, then selector */
.globl int31_prev
int31_prev:
	.long 0, 0
/* the 16-bit code selector of the current 16-bit RSP call */
.globl rsp16_sel
rsp16_sel:
	.word 0, 0
/* far pointer back into host_reinit() */
reinit_back:
	.long reinit_back32
	.word 0, 0

.bss
	.p2align 4
.globl pmdapi_stack
pmdapi_stack:
	.space STK_CHUNK * STK_DEPTH
