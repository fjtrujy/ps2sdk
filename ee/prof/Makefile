# _____     ___ ____     ___ ____
#  ____|   |    ____|   |        | |____|
# |     ___|   |____ ___|    ____| |    \    PS2DEV Open Source Project.
#-----------------------------------------------------------------------
# Copyright 2001-2004, ps2dev - http://www.ps2dev.org
# Licenced under Academic Free License version 2.0
# Review ps2sdk README & LICENSE files for further details.

EE_LIB = libprof.a

CORE_OBJS = timezone.o

INIT_OBJS = __libpthreadglue_init.o __libpthreadglue_deinit.o _libcglue_init.o _libcglue_deinit.o _libcglue_args_parse.o

SLEEP_OBJS = nanosleep.o

EE_OBJS = prof.o mcount.o

include $(PS2SDKSRC)/Defs.make
include $(PS2SDKSRC)/ee/Rules.lib.make
include $(PS2SDKSRC)/ee/Rules.make
include $(PS2SDKSRC)/ee/Rules.release
