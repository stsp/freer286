/*
 * A copy of the LDT the program may read, and writes to it that we apply
 * through DPMI.
 *
 * Origin's wrapper finds the LDT the way a 286 extender's own program
 * would, through sgdt and sldt, then maps it and writes descriptors into it
 * as plain memory. dosemu2 lets that work by watching the page of its LDT
 * alias; no other DPMI host does. So we keep a table of our own, the
 * shadow, hand the program only read only selectors onto it, and take the
 * #GP its first write raises: decode the instruction, do its write on the
 * shadow, and pass each entry it touched on to the host with DPMI 000Ch.
 * This is what dosemu2's msdos plugin does for a read only alias of its
 * own (msdos_ldt.c), and what pmdapi carries to plain DPMI hosts.
 *
 * Reads are honest: the program sees what it wrote, and what we allocate
 * for it through DPMI, as every descriptor call of ours goes through the
 * wrappers below and copies the host's answer back into the shadow.
 *
 * MIT license, see LICENSE.
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <dpmi.h>
#include <sys/farptr.h>
#define LDT_NO_WRAP
#include "asm.h"
#include "run286.h"

#define LDT_ENT		8
#define AR_DATA16	0x00f2		/* data, writable */
#define AR_DATA16_RO	0x00f0		/* data, read only */

int ldt_shadow_on;
static __dpmi_meminfo shadow;
static uint16_t shadow_sel;		/* ours, writable */
static unsigned ldt_writes, ldt_faults, ldt_refused;

static void shadow_get(unsigned ent, unsigned char *d)
{
    unsigned i;

    for (i = 0; i < LDT_ENT; i++)
	d[i] = _farpeekb(shadow_sel, ent * LDT_ENT + i);
}

static void shadow_put(unsigned ent, const unsigned char *d)
{
    unsigned i;

    for (i = 0; i < LDT_ENT; i++)
	_farpokeb(shadow_sel, ent * LDT_ENT + i, d[i]);
}

/* Copy what the host has for n entries from sel on into the shadow. An
 * entry the host will not show is free, and a free entry reads as zero,
 * which is how the wrapper tells a selector it owns from one it does not. */
void ldt_sync(int sel, int n)
{
    unsigned ent = (unsigned)sel >> 3;
    unsigned char d[LDT_ENT];
    int i;

    if (!ldt_shadow_on || sel == -1)
	return;
    for (i = 0; i < n && ent + i < LDT_ENTRIES_USABLE; i++) {
	if (__dpmi_get_descriptor(((ent + i) << 3) | 7, d) == -1)
	    memset(d, 0, sizeof(d));
	shadow_put(ent + i, d);
    }
}

static void ldt_forget(int sel)
{
    static const unsigned char z[LDT_ENT];
    unsigned ent = (unsigned)sel >> 3;

    if (ldt_shadow_on && ent < LDT_ENTRIES_USABLE)
	shadow_put(ent, z);
}

int ldt_shadow_init(void)
{
    shadow.size = 0x10000;
    if (__dpmi_allocate_memory(&shadow) == -1)
	return -1;
    shadow_sel = __dpmi_allocate_ldt_descriptors(1);
    if (shadow_sel == (uint16_t)-1 ||
	    __dpmi_set_segment_base_address(shadow_sel, shadow.address) == -1 ||
	    __dpmi_set_segment_limit(shadow_sel, shadow.size - 1) == -1 ||
	    __dpmi_set_descriptor_access_rights(shadow_sel, AR_DATA16) == -1)
	return -1;
    ldt_shadow_on = 1;
    ldt_sync(0, LDT_ENTRIES_USABLE);
    ldt_lin = shadow.address;
    ldt_size = LDT_FULL_SIZE;
    trc("run286: ldt shadow at %#lx, %u entries\n",
	    (unsigned long)shadow.address, LDT_ENTRIES_USABLE);
    return 0;
}

int ldt_in_shadow(uint32_t lin)
{
    return ldt_shadow_on && lin >= shadow.address &&
	    lin < shadow.address + LDT_FULL_SIZE;
}

/* the program only ever gets to read the shadow */
void ldt_make_ro(int sel)
{
    unsigned int base;

    if (!ldt_shadow_on || sel == -1)
	return;
    if (__dpmi_get_segment_base_address(sel, &base) == 0 &&
	    ldt_in_shadow(base)) {
	__dpmi_set_descriptor_access_rights(sel, AR_DATA16_RO);
	ldt_sync(sel, 1);
    }
}

/*
 * Hand one entry of the shadow to the host. What the program wrote stays in
 * the shadow as it wrote it; the host gets a descriptor it will take: DPL 3,
 * and a free, not present one where the program wrote something that is not
 * a segment at all (a half built entry, or zeroes to free it).
 */
static void ldt_apply(unsigned ent)
{
    unsigned char d[LDT_ENT], cur[LDT_ENT];
    int sel = (ent << 3) | 7;

    shadow_get(ent, d);
    ldt_writes++;
    if (__dpmi_get_descriptor(sel, cur) == -1 &&
	    __dpmi_allocate_specific_ldt_descriptor(sel) == -1) {
	if (ldt_refused++ < 16)
	    trc("run286: ldt write to %04x, which the host will not give us\n",
		    sel);
	return;
    }
    if (!(d[5] & 0x10)) {
	memset(d, 0, sizeof(d));
	d[5] = 0x70;			/* data, DPL 3, not present */
    } else {
	d[5] |= 0x60;
    }
    if (__dpmi_set_descriptor(sel, d) == -1 && ldt_refused++ < 16)
	trc("run286: the host refused %04x: %02x %02x %02x %02x %02x %02x %02x %02x\n",
		sel, d[0], d[1], d[2], d[3], d[4], d[5], d[6], d[7]);
}

/* ---- the fault ---- */

/* the frame _exc_common leaves at gate_exc_ss:gate_exc_esp */
#define F_DS	0
#define F_ES	4
#define F_FS	8
#define F_GS	12
#define F_EDI	16
#define F_ESI	20
#define F_EBP	24
#define F_EBX	32
#define F_EDX	36
#define F_ECX	40
#define F_EAX	44
#define F_EIP	64
#define F_CS	68
#define F_FL	72
#define F_ESP	76
#define F_SS	80

/* x86 register number to the slot it was saved in */
static const unsigned char reg_slot[8] = {
    F_EAX, F_ECX, F_EDX, F_EBX, F_ESP, F_EBP, F_ESI, F_EDI
};

struct insn {
    unsigned fss, fsp;			/* where the frame is */
    unsigned cs, eip, len;
    int op32, ad32;
    int seg;				/* override, frame slot, or -1 */
    int rep;
};

static uint32_t fr(struct insn *x, unsigned slot)
{
    return _farpeekl(x->fss, x->fsp + slot);
}

static void fw(struct insn *x, unsigned slot, uint32_t v)
{
    _farpokel(x->fss, x->fsp + slot, v);
}

static uint8_t code(struct insn *x)
{
    return _farpeekb(x->cs, x->eip + x->len++);
}

static uint32_t code_n(struct insn *x, unsigned n)
{
    uint32_t v = 0;
    unsigned i;

    for (i = 0; i < n; i++)
	v |= (uint32_t)code(x) << (i * 8);
    return v;
}

static uint32_t get_reg(struct insn *x, unsigned r, unsigned size)
{
    if (size == 1) {
	uint32_t v = fr(x, reg_slot[r & 3]);
	return r & 4 ? (v >> 8) & 0xff : v & 0xff;
    }
    return size == 2 ? fr(x, reg_slot[r]) & 0xffff : fr(x, reg_slot[r]);
}

static void set_reg(struct insn *x, unsigned r, unsigned size, uint32_t v)
{
    uint32_t o;

    if (size == 4) {
	fw(x, reg_slot[r], v);
	return;
    }
    if (size == 2) {
	o = fr(x, reg_slot[r]);
	fw(x, reg_slot[r], (o & 0xffff0000) | (v & 0xffff));
	return;
    }
    o = fr(x, reg_slot[r & 3]);
    if (r & 4)
	o = (o & ~0xff00u) | ((v & 0xff) << 8);
    else
	o = (o & ~0xffu) | (v & 0xff);
    fw(x, reg_slot[r & 3], o);
}

static uint32_t seg_base(unsigned sel)
{
    unsigned int base = 0;

    __dpmi_get_segment_base_address(sel & 0xffff, &base);
    return base;
}

/* The linear address a ModRM memory operand names, or 0 with *reg set for
 * a register operand. */
static int modrm_addr(struct insn *x, uint8_t m, uint32_t *lin)
{
    unsigned mod = m >> 6, rm = m & 7;
    int dseg = F_DS;
    uint32_t ea = 0;

    if (mod == 3)
	return 0;
    if (!x->ad32) {
	static const signed char b[8] = { 3, 3, 5, 5, -1, -1, 5, 3 };
	static const signed char i[8] = { 6, 7, 6, 7, 6, 7, -1, -1 };

	if (mod == 0 && rm == 6) {
	    ea = code_n(x, 2);
	} else {
	    if (b[rm] >= 0)
		ea += get_reg(x, b[rm], 2);
	    if (i[rm] >= 0)
		ea += get_reg(x, i[rm], 2);
	    if (b[rm] == 5)
		dseg = F_SS;
	    if (mod == 1)
		ea += (int8_t)code(x);
	    else if (mod == 2)
		ea += code_n(x, 2);
	}
	ea &= 0xffff;
    } else {
	if (rm == 4) {
	    uint8_t sib = code(x);
	    unsigned base = sib & 7, idx = (sib >> 3) & 7;

	    if (idx != 4)
		ea += get_reg(x, idx, 4) << (sib >> 6);
	    if (base == 5 && mod == 0) {
		ea += code_n(x, 4);
	    } else {
		ea += get_reg(x, base, 4);
		if (base == 4 || base == 5)
		    dseg = F_SS;
	    }
	} else if (rm == 5 && mod == 0) {
	    ea = code_n(x, 4);
	} else {
	    ea = get_reg(x, rm, 4);
	    if (rm == 5)
		dseg = F_SS;
	}
	if (mod == 1)
	    ea += (int8_t)code(x);
	else if (mod == 2)
	    ea += code_n(x, 4);
    }
    if (x->seg >= 0)
	dseg = x->seg;
    *lin = seg_base(fr(x, dseg)) + ea;
    return 1;
}

static uint32_t mem_read(unsigned sel, uint32_t off, unsigned size)
{
    uint32_t v = 0;
    unsigned i;

    for (i = 0; i < size; i++)
	v |= (uint32_t)_farpeekb(sel, off + i) << (i * 8);
    return v;
}

/* Write into the shadow; returns the entries touched as [*lo, *hi]. */
static void shadow_write(uint32_t lin, unsigned size, uint32_t v,
	unsigned *lo, unsigned *hi)
{
    uint32_t off = lin - shadow.address;
    unsigned i;

    for (i = 0; i < size && off + i < LDT_FULL_SIZE; i++)
	_farpokeb(shadow_sel, off + i, v >> (i * 8));
    if (off / LDT_ENT < *lo)
	*lo = off / LDT_ENT;
    if ((off + size - 1) / LDT_ENT > *hi)
	*hi = (off + size - 1) / LDT_ENT;
    if (*hi >= LDT_ENTRIES_USABLE)
	*hi = LDT_ENTRIES_USABLE - 1;
}

static uint32_t shadow_read(uint32_t lin, unsigned size)
{
    uint32_t off = lin - shadow.address;
    uint32_t v = 0;
    unsigned i;

    for (i = 0; i < size && off + i < LDT_FULL_SIZE; i++)
	v |= (uint32_t)_farpeekb(shadow_sel, off + i) << (i * 8);
    return v;
}

#define FL_CF	0x001
#define FL_PF	0x004
#define FL_AF	0x010
#define FL_ZF	0x040
#define FL_SF	0x080
#define FL_DF	0x400
#define FL_OF	0x800
#define FL_ARITH (FL_CF | FL_PF | FL_AF | FL_ZF | FL_SF | FL_OF)

static uint32_t szp(uint32_t r, unsigned size)
{
    uint32_t msb = 1u << (size * 8 - 1);
    uint32_t mask = msb | (msb - 1);
    uint32_t f = 0;
    unsigned p = r & 0xff;

    p ^= p >> 4;
    p ^= p >> 2;
    p ^= p >> 1;
    if (!(p & 1))
	f |= FL_PF;
    if (!(r & mask))
	f |= FL_ZF;
    if (r & msb)
	f |= FL_SF;
    return f;
}

/*
 * One ALU operation the way the processor does it, with its flags.
 * op: 0 add, 1 or, 2 adc, 3 sbb, 4 and, 5 sub, 6 xor, 8 inc, 9 dec,
 * 10 not, 11 neg. Returns the result; *fl is updated in the arithmetic bits.
 */
static uint32_t alu(unsigned op, uint32_t a, uint32_t b, unsigned size,
	uint32_t *fl)
{
    uint32_t msb = 1u << (size * 8 - 1);
    uint32_t mask = msb | (msb - 1);
    uint32_t cin = *fl & FL_CF ? 1 : 0;
    uint32_t r = 0, f = 0;
    uint64_t wide;

    a &= mask;
    b &= mask;
    switch (op) {
    case 1: r = a | b; break;
    case 4: r = a & b; break;
    case 6: r = a ^ b; break;
    case 10:
	r = ~a & mask;
	return r;			/* not touches no flags */
    case 8:
	cin = 0;
	b = 1;
	/* fall through */
    case 0:
    case 2:
	if (op != 2)
	    cin = 0;
	wide = (uint64_t)a + b + cin;
	r = wide & mask;
	if (wide > mask)
	    f |= FL_CF;
	if (~(a ^ b) & (a ^ r) & msb)
	    f |= FL_OF;
	f |= (a ^ b ^ r) & FL_AF;
	break;
    case 11:
	b = a;
	a = 0;
	cin = 0;
	/* fall through */
    case 9:
	if (op == 9) {
	    b = 1;
	    cin = 0;
	}
	/* fall through */
    case 3:
    case 5:
	if (op == 5)
	    cin = 0;
	r = (a - b - cin) & mask;
	if ((uint64_t)b + cin > a)
	    f |= FL_CF;
	if ((a ^ b) & (a ^ r) & msb)
	    f |= FL_OF;
	f |= (a ^ b ^ r) & FL_AF;
	break;
    }
    f |= szp(r, size);
    if (op == 8 || op == 9)		/* inc and dec leave CF alone */
	f = (f & ~FL_CF) | (*fl & FL_CF);
    *fl = (*fl & ~FL_ARITH) | f;
    return r;
}

/*
 * Called for a #GP. Returns nonzero if it was a write of the program's to
 * the shadow, which is then done, and the program goes on after it.
 */
int ldt_write_fault(unsigned fss, unsigned fsp)
{
    struct insn x = { fss, fsp };
    unsigned lo = ~0u, hi = 0;
    unsigned char d[LDT_ENT];
    uint32_t lin, fl, v, cnt;
    unsigned size, ent;
    uint8_t op, m = 0;
    int cs32;

    if (!ldt_shadow_on)
	return 0;
    x.cs = fr(&x, F_CS) & 0xffff;
    x.eip = fr(&x, F_EIP);
    if (__dpmi_get_descriptor(x.cs, d) == -1)
	return 0;
    cs32 = !!(d[6] & 0x40);
    x.op32 = x.ad32 = cs32;
    x.seg = -1;
    fl = fr(&x, F_FL);
    for (;;) {
	op = code(&x);
	switch (op) {
	case 0x26: x.seg = F_ES; continue;
	case 0x2e: x.seg = F_CS; continue;
	case 0x36: x.seg = F_SS; continue;
	case 0x3e: x.seg = F_DS; continue;
	case 0x64: x.seg = F_FS; continue;
	case 0x65: x.seg = F_GS; continue;
	case 0x66: x.op32 = !cs32; continue;
	case 0x67: x.ad32 = !cs32; continue;
	case 0xf2:
	case 0xf3: x.rep = 1; continue;
	}
	break;
    }
    size = (op & 1) ? (x.op32 ? 4 : 2) : 1;

    switch (op) {
    /* mov moffs, al/ax */
    case 0xa2:
    case 0xa3:
	lin = code_n(&x, x.ad32 ? 4 : 2);
	lin += seg_base(fr(&x, x.seg >= 0 ? x.seg : F_DS));
	if (!ldt_in_shadow(lin))
	    return 0;
	shadow_write(lin, size, get_reg(&x, 0, size), &lo, &hi);
	break;

    /* stos and movs, rep or not: the destination is always es:di */
    case 0xaa:
    case 0xab:
    case 0xa4:
    case 0xa5: {
	uint32_t amask = x.ad32 ? 0xffffffff : 0xffff;
	uint32_t di = get_reg(&x, 7, 4), si = get_reg(&x, 6, 4);
	uint32_t esb = seg_base(fr(&x, F_ES));
	unsigned ssel = fr(&x, x.seg >= 0 ? x.seg : F_DS) & 0xffff;
	uint32_t ssb = seg_base(ssel);
	int step = fl & FL_DF ? -(int)size : (int)size;

	if (!ldt_in_shadow(esb + (di & amask)))
	    return 0;
	cnt = x.rep ? get_reg(&x, 1, 4) & amask : 1;
	while (cnt) {
	    uint32_t dst = esb + (di & amask);

	    if (!ldt_in_shadow(dst))
		break;			/* let the host fault the rest */
	    if (op >= 0xaa)
		v = get_reg(&x, 0, size);
	    else if (ldt_in_shadow(ssb + (si & amask)))
		v = shadow_read(ssb + (si & amask), size);
	    else
		v = mem_read(ssel, si & amask, size);
	    shadow_write(dst, size, v, &lo, &hi);
	    di = (di & ~amask) | ((di + step) & amask);
	    if (op < 0xaa)
		si = (si & ~amask) | ((si + step) & amask);
	    cnt--;
	}
	if (x.ad32) {
	    set_reg(&x, 7, 4, di);
	    if (op < 0xaa)
		set_reg(&x, 6, 4, si);
	    if (x.rep)
		set_reg(&x, 1, 4, cnt);
	} else {
	    set_reg(&x, 7, 2, di);
	    if (op < 0xaa)
		set_reg(&x, 6, 2, si);
	    if (x.rep)
		set_reg(&x, 1, 2, cnt);
	}
	if (cnt)
	    x.len = 0;			/* not done: run it again */
	break;
    }

    /* mov r/m, reg; mov r/m, imm; xchg */
    case 0x88:
    case 0x89:
    case 0x86:
    case 0x87:
    case 0xc6:
    case 0xc7:
	m = code(&x);
	if (!modrm_addr(&x, m, &lin) || !ldt_in_shadow(lin))
	    return 0;
	if (op >= 0xc6) {
	    if ((m >> 3) & 7)
		return 0;
	    v = code_n(&x, size == 4 ? 4 : size);
	} else {
	    v = get_reg(&x, (m >> 3) & 7, size);
	}
	if (op == 0x86 || op == 0x87)
	    set_reg(&x, (m >> 3) & 7, size, shadow_read(lin, size));
	shadow_write(lin, size, v, &lo, &hi);
	break;

    /* alu r/m, reg */
    case 0x00: case 0x01: case 0x08: case 0x09: case 0x10: case 0x11:
    case 0x18: case 0x19: case 0x20: case 0x21: case 0x28: case 0x29:
    case 0x30: case 0x31:
	m = code(&x);
	if (!modrm_addr(&x, m, &lin) || !ldt_in_shadow(lin))
	    return 0;
	v = alu(op >> 3, shadow_read(lin, size),
		get_reg(&x, (m >> 3) & 7, size), size, &fl);
	shadow_write(lin, size, v, &lo, &hi);
	break;

    /* alu r/m, imm */
    case 0x80:
    case 0x81:
    case 0x83: {
	uint32_t imm;

	m = code(&x);
	if (((m >> 3) & 7) == 7)	/* cmp does not write */
	    return 0;
	if (!modrm_addr(&x, m, &lin) || !ldt_in_shadow(lin))
	    return 0;
	if (op == 0x83)
	    imm = (uint32_t)(int32_t)(int8_t)code(&x);
	else
	    imm = code_n(&x, size);
	v = alu((m >> 3) & 7, shadow_read(lin, size), imm, size, &fl);
	shadow_write(lin, size, v, &lo, &hi);
	break;
    }

    /* inc, dec; not, neg */
    case 0xfe:
    case 0xff:
    case 0xf6:
    case 0xf7: {
	unsigned r;

	m = code(&x);
	r = (m >> 3) & 7;
	if (op >= 0xfe ? r > 1 : (r != 2 && r != 3))
	    return 0;
	if (!modrm_addr(&x, m, &lin) || !ldt_in_shadow(lin))
	    return 0;
	v = alu(8 + r, shadow_read(lin, size), 0, size, &fl);
	shadow_write(lin, size, v, &lo, &hi);
	break;
    }

    default:
	return 0;
    }

    ldt_faults++;
    if (ldt_faults == 1 || !(ldt_faults & 0xfff))
	trc("run286: ldt write #%u, op %02x at %04x:%04x, entries %#x..%#x\n",
		ldt_faults, op, x.cs, x.eip, lo, hi);
    for (ent = lo; ent <= hi && lo != ~0u; ent++)
	ldt_apply(ent);
    fw(&x, F_EIP, x.eip + x.len);
    fw(&x, F_FL, fl);
    return 1;
}

void ldt_report(void)
{
    if (ldt_shadow_on)
	trc("run286: ldt shadow: %u writes caught, %u entries applied, %u refused\n",
		ldt_faults, ldt_writes, ldt_refused);
}

/* ---- every descriptor call of ours, so the shadow stays true ---- */

int ldt_alloc(int n)
{
    int sel = __dpmi_allocate_ldt_descriptors(n);

    if (sel != -1)
	ldt_sync(sel, n);
    return sel;
}

int ldt_free(int sel)
{
    int rc = __dpmi_free_ldt_descriptor(sel);

    ldt_forget(sel);
    return rc;
}

int ldt_set_base(int sel, unsigned long base)
{
    int rc = __dpmi_set_segment_base_address(sel, base);

    ldt_sync(sel, 1);
    return rc;
}

int ldt_set_limit(int sel, unsigned long lim)
{
    int rc = __dpmi_set_segment_limit(sel, lim);

    ldt_sync(sel, 1);
    return rc;
}

int ldt_set_ar(int sel, int ar)
{
    int rc = __dpmi_set_descriptor_access_rights(sel, ar);

    ldt_sync(sel, 1);
    return rc;
}

int ldt_alias(int sel)
{
    int a = __dpmi_create_alias_descriptor(sel);

    ldt_sync(a, 1);
    ldt_make_ro(a);
    return a;
}

int ldt_dos_alloc(int paras, int *selp)
{
    int rc = __dpmi_allocate_dos_memory(paras, selp);

    if (rc != -1)
	ldt_sync(*selp, (paras + 0xfff) >> 12);
    return rc;
}

int ldt_dos_free(int sel)
{
    int rc = __dpmi_free_dos_memory(sel);

    ldt_sync(sel, 1);
    return rc;
}
