# pmdapi, the resident part of run286, built with dj64dev for dj32.
# It is a library that run286.exe links: "run286 /tsr" installs it.
DJTGT ?= dj32
SOURCES = startup.c wrapper.c msdoshlp.c dpmi_api.c \
    msdos/msdos.c msdos/segreg.c msdos/msdos_ldt.c msdos/instr_dec.c \
    msdos/callbacks.c msdos/emm.c msdos/xms.c msdos/lio.c msdos/hlpmisc.c
AS_SOURCES = entry.S
PMDAPI_A = libpmdapi.a

# the below vars must be named exactly like this for dj64.mk
CFLAGS = -Wall -O2 -ggdb3 -fms-extensions -fplan9-extensions -I. -Imsdos
DJCFLAGS = $(CFLAGS)
OBJECTS = $(SOURCES:.c=.o)
AS_OBJECTS = $(AS_SOURCES:.S=.o)

all: $(PMDAPI_A)

$(PMDAPI_A): $(AS_OBJECTS) $(OBJECTS)
	$(RM) $@
	$(CROSS_PREFIX)ar rcs $@ $^

clean: clean_$(DJTGT)
	$(RM) $(PMDAPI_A) $(OBJECTS) $(AS_OBJECTS)


DJMK = $(shell pkg-config --silence-errors --variable=makeinc $(DJTGT))
ifeq ($(wildcard $(DJMK)),)
ifeq ($(filter clean,$(MAKECMDGOALS)),)
$(error $(DJTGT) not installed)
endif
clean_$(DJTGT):
else
include $(DJMK)
endif

$(OBJECTS): $(wildcard *.h msdos/*.h)

