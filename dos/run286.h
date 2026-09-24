/*
 * The API the program sees: PHAPI and the OS/2 1.x DOSCALLS subset that
 * the Phar Lap bound programs actually import.
 *
 * MIT license, see LICENSE.
 */
#ifndef RUN286_H
#define RUN286_H

#include <stdint.h>

/*
 * One call from the program. gate_entry() has pushed, on the program's own
 * 16bit stack and in this order, PUSHAD, DS, ES; below that sits the iret
 * frame the DPMI host made, then the near return into the stub, then the
 * arguments. c->sp points at the saved ES.
 */
#define CALL_ES		0
#define CALL_DS		4
#define CALL_EDI	8
#define CALL_ESI	12
#define CALL_EBP	16
#define CALL_EBX	24
#define CALL_EDX	28
#define CALL_ECX	32
#define CALL_EAX	36		/* the saved EAX, for the result */
#define CALL_ARGS	56		/* the first byte of the arguments */

/*
 * What the LDT alias reports as its limit is only what dosemu2 has
 * allocated so far, not the size of the table, so we set it ourselves.
 *
 * One entry short of the 8192 dosemu2 has, on purpose. Origin's wrapper
 * turns the limit into a count with "mov ax,limit; inc ax; shr ax,3" in
 * 16bit registers, so a limit of 0xffff wraps to zero and every index it
 * then checks is out of range ("DESCRIPT.C, 276"). 8191 entries leave the
 * arithmetic alone and are as many as any of these programs asks for.
 */
#define LDT_ENTRIES_USABLE	8191
#define LDT_FULL_SIZE		(LDT_ENTRIES_USABLE * 8)

struct call {
    uint16_t ss;
    uint32_t sp;
};

/* An API entry. args is the number of argument bytes the callee has to pop
 * on the way out, which for the Pascal convention is all of them. */
struct api_fn {
    const char *name;			/* for imports by name */
    uint16_t ord;			/* for imports by ordinal */
    uint16_t args;
    uint16_t (*fn)(struct call *c);
};

/* The linear address of the LDT, as dosemu2 hands it out for int 2Fh
 * AX=1688h, and its size. Origin's wrapper writes descriptors straight
 * into the table it finds through sgdt, so this is where they have to go. */
extern int run286_trace;
void dump_hooked_vectors(void);	/* what is on each vector we hooked */		/* the API trace is on */
void trc(const char *fmt, ...);		/* where that trace goes */

extern uint32_t ldt_lin;
extern uint32_t ldt_size;
extern uint16_t ldt_sel_reg;		/* what sldt says */

const struct api_fn *phapi_lookup(const char *name);
const struct api_fn *doscalls_lookup(uint16_t ord);

/* An ordinal that is a constant rather than a function; 0 if it is one. */
int doscalls_const(uint16_t ord, uint16_t *val);

/* Linear memory out of the pool the programs want it from. */
uint16_t run286_lin_alloc(uint32_t size, uint32_t *linp);
uint16_t run286_lin_free(uint32_t lin);

/*
 * The LDT the program gets to see, in ldt.c: a shadow of the real one it
 * reads through read only selectors, its writes caught on the #GP and
 * applied through DPMI, so it works on any host, not just dosemu2.
 */
extern int ldt_shadow_on;
int ldt_shadow_init(void);
int ldt_in_shadow(uint32_t lin);
void ldt_make_ro(int sel);
void ldt_sync(int sel, int n);
int ldt_write_fault(unsigned fss, unsigned fsp);
void ldt_report(void);

/* Every descriptor call of ours goes through these, so that what the
 * program reads in the shadow is what the host holds. */
int ldt_alloc(int n);
int ldt_free(int sel);
int ldt_set_base(int sel, unsigned long base);
int ldt_set_limit(int sel, unsigned long lim);
int ldt_set_ar(int sel, int ar);
int ldt_alias(int sel);
int ldt_dos_alloc(int paras, int *selp);
int ldt_dos_free(int sel);
#ifndef LDT_NO_WRAP
#define __dpmi_allocate_ldt_descriptors(n)	ldt_alloc(n)
#define __dpmi_free_ldt_descriptor(s)		ldt_free(s)
#define __dpmi_set_segment_base_address(s, b)	ldt_set_base(s, b)
#define __dpmi_set_segment_limit(s, l)		ldt_set_limit(s, l)
#define __dpmi_set_descriptor_access_rights(s, a) ldt_set_ar(s, a)
#define __dpmi_create_alias_descriptor(s)	ldt_alias(s)
#define __dpmi_allocate_dos_memory(p, s)	ldt_dos_alloc(p, s)
#define __dpmi_free_dos_memory(s)		ldt_dos_free(s)
#endif

uint16_t call_argw(struct call *c, unsigned off);
uint32_t call_argd(struct call *c, unsigned off);
void call_setw(uint32_t fp, uint16_t val);
void call_setd(uint32_t fp, uint32_t val);

#endif
