/*
 * The resident: set up dosemu2's msdos plugin, have the DPMI host call it
 * for every new client (the resident service provider of DPMI 1.0), run
 * the programs bound to Phar Lap's 286 extender in its place when it
 * enters DPMI (see takeover() in msdoshlp.c), and stay resident.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dpmi.h>
#include <sys/segments.h>
#include <go32.h>
#include <crt0.h>
#include <sys/farptr.h>
#include "emudpmi.h"
#include "entry.h"
#include "msdoshlp.h"
#include "pmdapi.h"

/* libc's DOS calls go through the transfer buffer, and the one our stub
 * gave us goes away with our DOS process */
#define TB_PARAS 0x400
/* and its heap cannot grow in someone else's client, see pmdapi_install() */
#define HEAP_RESERVE (10 * 1024 * 1024)

int pmdapi_install(int (*run)(const char *path))
{
    ULONG ds_base, cs_base;
    unsigned short ds = _my_ds(), cs = _my_cs();
    int sel, seg;

    /*
     * Growing the heap resizes the block it is in and sets our limits
     * anew, and the block belongs to the client that installed us: grow
     * it now, while that is us, as far as the loader will ever need.
     */
    free(malloc(HEAP_RESERVE));
    seg = __dpmi_allocate_dos_memory(TB_PARAS, &sel);
    if (seg == -1) {
	printf("pmdapi: no DOS memory for the transfer buffer\n");
	return 1;
    }
    /* owned by the system, not by our DOS process */
    _farpokew(_dos_ds, (seg - 1) * 16 + 1, 8);
    __tb = seg * 16;
    __tb_size = TB_PARAS * 16;

    /* dosemu2's plugin reaches all of DOS memory through near pointers */
    if (__dpmi_get_segment_base_address(ds, &ds_base) == -1 ||
	    __dpmi_get_segment_base_address(cs, &cs_base) == -1 ||
	    __dpmi_set_segment_limit(ds, 0xffffffff) == -1 ||
	    __dpmi_set_segment_limit(cs, 0xffffffff) == -1) {
	printf("pmdapi: cannot map the address space\n");
	return 1;
    }
    /* entry.S patches its code through our data segment */
    if (cs_base != ds_base) {
	printf("pmdapi: code and data are not based alike\n");
	return 1;
    }
    /* and nothing may take the 4G limit back: growing the heap, for one,
     * would set the limits to the heap's size */
    _crt0_startup_flags |= _CRT0_FLAG_NEARPTR;
    mem_base = (unsigned char *)-ds_base;
    dseg32 = ds;
    cur_sp = (uintptr_t)pmdapi_stack + STK_CHUNK * STK_DEPTH;

    wrapper_init();
    msdos_plugin_init();
    pmdapi_set_takeover(run);
    /* installs the resident service provider, see rsp_init() */
    msdos_reset();

    printf("pmdapi: installed, Phar Lap 286 programs run under run286\n");
    fflush(stdout);
    if (__dpmi_terminate_and_stay_resident(0, 0) == -1) {
	printf("pmdapi: the DPMI host cannot keep us resident\n");
	return 1;
    }
    return 0;
}
