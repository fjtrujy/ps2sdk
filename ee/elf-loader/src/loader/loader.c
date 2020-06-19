/*
# _____     ___ ____     ___ ____
#  ____|   |    ____|   |        | |____|
# |     ___|   |____ ___|    ____| |    \    PS2DEV Open Source Project.
#-----------------------------------------------------------------------
# (c) 20020 Francisco Javier Trujillo Mata <fjtrujy@gmail.com>
# Licenced under Academic Free License version 2.0
# Review ps2sdk README & LICENSE files for further details.
*/

#include <kernel.h>
#include <loadfile.h>
#include <sifrpc.h>
#include <errno.h>
#include <string.h>

//--------------------------------------------------------------
//Start of function code:
//--------------------------------------------------------------
// Clear user memory
// PS2Link (C) 2003 Tord Lindstrom (pukko@home.se)
//         (C) 2003 adresd (adresd_ps2dev@yahoo.com)
//--------------------------------------------------------------
static void wipeUserMem(void)
{
	int i;
	for (i = 0x100000; i < GetMemorySize(); i += 64) {
		asm volatile(
		    "\tsq $0, 0(%0) \n"
		    "\tsq $0, 16(%0) \n"
		    "\tsq $0, 32(%0) \n"
		    "\tsq $0, 48(%0) \n" ::"r"(i));
	}
}

//--------------------------------------------------------------
//End of func:  void wipeUserMem(void)
//--------------------------------------------------------------
// *** MAIN ***
//--------------------------------------------------------------
int main(int argc, char *argv[])
{
	static t_ExecData elfdata;
	char *elfpath;
	int i, ret, new_argc;

	if (argc < 1) {  // arg1=path to ELF
		return -EINVAL;
	} else if (argc == 1) {
		new_argc = 1; // I don't why ExecPS2 expect to have at least one parameter
	} else {
		new_argc = argc - 1;
	}

	char *new_argv[new_argc];

	// Initialize
	SifInitRpc(0);
	wipeUserMem();

	elfpath = argv[0];
	for (i = 1; i < argc; i++ ) {
		strcpy(new_argv[i-1], argv[i]);
	}

	//Writeback data cache before loading ELF.
	FlushCache(0);
	ret = SifLoadElf(elfpath, &elfdata);
	if (ret == 0) {
		SifExitRpc();
		FlushCache(0);
		FlushCache(2);

		return ExecPS2((void *)elfdata.epc, (void *)elfdata.gp, new_argc, new_argv);
	} else {
		SifExitRpc();
		return -ENOENT;
	}
}

//--------------------------------------------------------------
//End of func:  int main(int argc, char *argv[])
//--------------------------------------------------------------
//End of file:  loader.c
//--------------------------------------------------------------
