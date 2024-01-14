
#include "all_include.h"

const char head = '\0';
srxfixup_const_char_ptr_t force_to_data_0 = NULL;
srxfixup_const_char_ptr_t conffile;
srxfixup_const_char_ptr_t ofile = NULL;
srxfixup_const_char_ptr_t rfile = NULL;
srxfixup_const_char_ptr_t pfile;
srxfixup_const_char_ptr_t ffile = NULL;
unsigned int startaddr;
srxfixup_const_char_ptr_t entrysym = NULL;
unsigned int verbose = 0;
unsigned int dumpflag = 0;
unsigned int dispmod_flag = 0;
int irx1_flag = 0;
int br_conv = 0;
int print_config = 0;
Opttable opttable[] =
{
	{ "-v", 0, 'f', &verbose },
	{ "-d", 0, 'f', &dumpflag },
	{ "-r", 2, 's', &rfile },
	{ "-o", 2, 's', &ofile },
	{ "-c", 2, 's', &force_to_data_0 },
	{ "-f", 2, 's', &ffile },
	{ "-t", 2, 'h', &startaddr },
	{ "-e", 2, 's', &entrysym },
	{ "-m", 0, 'f', &dispmod_flag },
	{ "--irx1", 0, 'f', &irx1_flag },
	{ "--rb", 0, 'f', &br_conv },
	{ "--relative-branch", 0, 'f', &br_conv },
	{ "--print-internal-config", 0, 'f', &print_config },
	{ NULL, 0, '\0', NULL },
};
Opttable stripopttable[] =
{
	{ "-v", 0, 'f', &verbose },
	{ "-d", 0, 'f', &dumpflag },
	{ "-o", 2, 's', &ofile },
	{ "-c", 2, 's', &force_to_data_0 },
	{ "-e", 2, 's', &entrysym },
	{ "-m", 0, 'f', &dispmod_flag },
	{ "--irx1", 0, 'f', &irx1_flag },
	{ "--rb", 0, 'f', &br_conv },
	{ "--relative-branch", 0, 'f', &br_conv },
	{ "--print-internal-config", 0, 'f', &print_config },
	{ NULL, 0, '\0', NULL },
};

static void display_module_info(elf_file *elf);
static void convert_relative_branch_an_section(elf_section *relsect);
static void convert_relative_branch(elf_file *elf);

void usage(srxfixup_const_char_ptr_t myname)
{
	printf(
		"IOP/EE relocatable object converter\n"
		"%s\n"
		"usage: %s [options] <elf_input_file>\n",
		myname,
		myname);
	printf(
		"  options:\n"
		"    -v\n"
		"    -m\n"
		"    --irx1\n"
		"    -o <elf_relocatable_nosymbol_output_file>\n"
		"    -r <elf_relocatable_output_file>\n"
		"    -e <entry_point_symbol>\n"
		"    --relative-branch  or  --rb\n"
	);
	if ( verbose )
		printf(
			"    -t <.text start address>\n"
			"    -f <elf_fixedaddress_output_file>\n"
			"    -d<hex_flag>\n"
			"        hex_flag bits:\n"
			"           bit0: dump section table\n"
			"           bit1: dump relocation record\n"
			"           bit2: dump symbol table\n"
			"           bit3: disassemble program code\n"
			"           bit4: dump .data/.rodata/.sdata... sections by byte\n"
			"           bit5: dump .data/.rodata/.sdata... sections by half word\n"
			"           bit6: dump .data/.rodata/.sdata... sections by word\n"
			"           bit7: dump .data/.rodata/.sdata... sections by word with relocation data\n"
			"           bit8: dump file layout\n"
			"           bit9: dump .mdebug section\n"
		);
	if ( verbose > 1 )
		printf(
			"           bit12: dump srx genaration table\n"
			"    -c <config_file>\n"
			"    --print-internal-config\n"
		);
}

void stripusage(srxfixup_const_char_ptr_t myname)
{
	printf(
		"%s\n"
		"usage: %s [options] <elf_file>\n", myname, myname);
	printf(
		"  options:\n"
		"    -v\n"
		"    -m\n"
		"    -o <elf_relocatable_nosymbol_output_file>\n"
		"    --relative-branch  or  --rb\n"
	);
}

int main(int argc, char **argv)
{
	int v2;
	size_t v3;
	Srx_gen_table *srxgen_1;
	int e_type;
	const char *defaultconf;
	elf_file *elf;
	char *myname_1;
	char *myname_2;
	srxfixup_const_char_ptr_t source;

	myname_1 = rindex(*argv, '/');
	if ( !myname_1 )
		myname_1 = rindex(*argv, '\\');
	if ( myname_1 )
	{
		myname_2 = myname_1 + 1;
		v2 = strncmp(myname_2, "ee", 2u) != 0;
	}
	else
	{
		myname_2 = *argv;
		v2 = strncmp(*argv, "ee", 2u) != 0;
	}
	if ( v2 && strncmp(myname_2, "EE", 2u) != 0 )
		defaultconf = iop_defaultconf;
	else
		defaultconf = ee_defaultconf;
	if ( strlen(*argv) > 5 && (v3 = strlen(*argv), !strcmp(&(*argv)[v3 - 5], "strip")) )
	{
		int argca;

		argca = analize_arguments(stripopttable, argc, argv);
		if ( argca != 2 )
		{
			Srx_gen_table *srxgen_2;

			srxgen_2 = read_conf(defaultconf, force_to_data_0, print_config);
			if ( !srxgen_2 )
				exit(1);
			if ( (dumpflag & 0x1000) != 0 )
			{
				dump_srx_gen_table(srxgen_2);
				exit(0);
			}
		}
		if ( argca <= 1 )
		{
			stripusage(*argv);
			exit(1);
		}
		if ( argca > 2 )
		{
			fprintf(stderr, "Too many input file\n");
			stripusage(*argv);
			exit(1);
		}
		if ( !ofile )
			ofile = argv[1];
	}
	else
	{
		int argcb;

		argcb = analize_arguments(opttable, argc, argv);
		if ( argcb != 2 )
		{
			Srx_gen_table *srxgen_3;

			srxgen_3 = read_conf(defaultconf, force_to_data_0, print_config);
			if ( !srxgen_3 )
				exit(1);
			if ( (dumpflag & 0x1000) != 0 )
			{
				dump_srx_gen_table(srxgen_3);
				exit(0);
			}
		}
		if ( argcb <= 1 )
		{
			usage(*argv);
			exit(1);
		}
		if ( argcb > 2 )
		{
			fprintf(stderr, "Too many input file\n");
			usage(*argv);
			exit(1);
		}
	}
	source = argv[1];
	elf = read_elf(source);
	if ( !elf )
		exit(1);
	if ( (elf->ehp->e_flags & 0xF0FF0000) == 0x20920000 )
		srxgen_1 = read_conf(ee_defaultconf, force_to_data_0, print_config);
	else
		srxgen_1 = read_conf(iop_defaultconf, force_to_data_0, print_config);
	if ( !srxgen_1 )
		exit(1);
	if ( (dumpflag & 0x1000) != 0 )
	{
		dump_srx_gen_table(srxgen_1);
		exit(0);
	}
	if ( (dumpflag & 0xFFF) != 0 )
	{
		print_elf(elf, dumpflag & 0xFFF);
		exit(0);
	}
	elf->optdata = (void *)srxgen_1;
	if ( elf->ehp->e_type == 1 )
	{
		int v7;

		v7 = 0;
		if ( rfile || ofile || ffile )
			v7 = 1;
		if ( convert_rel2srx(elf, entrysym, v7, irx1_flag) )
			exit(1);
	}
	else if ( elf->ehp->e_type != 0xFF80
				 && elf->ehp->e_type != 0xFF81
				 && elf->ehp->e_type != 0xFF91
				 && elf->ehp->e_type != 2 )
	{
		e_type = elf->ehp->e_type;
		fprintf(stderr, "Error: '%s' is unsupport Type Elf file(type=%x)\n", source, e_type);
		exit(1);
	}
	if ( dispmod_flag )
		display_module_info(elf);
	if ( elf->ehp->e_type == 0xFF80 || elf->ehp->e_type == 0xFF81 || elf->ehp->e_type == 0xFF91 )
	{
		if ( br_conv )
			convert_relative_branch(elf);
		if ( rfile )
		{
			if ( layout_srx_file(elf) )
				exit(1);
			write_elf(elf, rfile);
		}
		if ( ofile )
		{
			strip_elf(elf);
			if ( layout_srx_file(elf) )
				exit(1);
			write_elf(elf, ofile);
		}
	}
	else if ( rfile || ofile )
	{
		fprintf(stderr, "Error: Cannot generate IRX/ERX file.  '%s' file type is ET_EXEC.\n", source);
		exit(1);
	}
	if ( ffile || startaddr != -1 )
	{
		if ( elf->ehp->e_type == 0xFF80 || elf->ehp->e_type == 0xFF81 || elf->ehp->e_type == 0xFF91 )
		{
			elf->ehp->e_type = 2;
			fixlocation_elf(elf, startaddr);
		}
		if ( ffile )
		{
			if ( layout_srx_file(elf) )
				exit(1);
			write_elf(elf, ffile);
		}
	}
	return 0;
}

static void display_module_info(elf_file *elf)
{
	elf_section *modsect_1;
	elf_section *modsect_2;

	modsect_1 = search_section(elf, 0x70000080);
	if ( modsect_1 )
	{
		Elf32_IopMod *iopmodinfo;

		iopmodinfo = (Elf32_IopMod *)modsect_1->data;
		if ( iopmodinfo->moduleinfo != -1 )
			printf(
				"name:%s version:%d.%d\n",
				iopmodinfo->modulename,
				HIBYTE(iopmodinfo->moduleversion),
				(uint8_t)iopmodinfo->moduleversion);
	}
	modsect_2 = search_section(elf, 0x70000090);
	if ( modsect_2 )
	{
		Elf32_EeMod *eemodinfo;

		eemodinfo = (Elf32_EeMod *)modsect_2->data;
		if ( eemodinfo->moduleinfo != -1 )
			printf(
				"name:%s version:%d.%d\n",
				eemodinfo->modulename,
				HIBYTE(eemodinfo->moduleversion),
				(uint8_t)eemodinfo->moduleversion);
	}
}

static void convert_relative_branch_an_section(elf_section *relsect)
{
	elf_syment **symp;
	elf_rel *rp;
	int rmcount;
	signed int entrise;
	signed int i;

	entrise = relsect->shr.sh_size / relsect->shr.sh_entsize;
	rp = (elf_rel *)relsect->data;
	symp = (elf_syment **)relsect->link->data;
	rmcount = 0;
	for ( i = 0; entrise > i; ++i )
	{
		int type;
		uint8_t *daddr;

		if ( *symp != rp->symptr )
		{
			fprintf(stderr, "Internal error: Illegal relocation entry\n");
			exit(1);
		}
		if ( relsect->info->shr.sh_addr > rp->rel.r_offset
			|| rp->rel.r_offset >= relsect->info->shr.sh_size + relsect->info->shr.sh_addr )
		{
			uint32_t r_offset;
			unsigned int sh_addr;
			unsigned int s_;

			s_ = relsect->info->shr.sh_size + relsect->info->shr.sh_addr;
			sh_addr = relsect->info->shr.sh_addr;
			r_offset = rp->rel.r_offset;
			fprintf(
				stderr,
				"Panic !! relocation #%d offset=0x%x range out (section limit addr=0x%x-0x%x)\n",
				i,
				r_offset,
				sh_addr,
				s_);
			exit(1);
		}
		daddr = &relsect->info->data[rp->rel.r_offset - relsect->info->shr.sh_addr];
		type = rp->type;
		if ( type )
		{
			if ( type == 4 )
			{
				uint32_t raddr;
				unsigned int data;

				data = *(uint32_t *)daddr;
				if ( rp->symptr->bind )
				{
					fprintf(stderr, "R_MIPS_26 Unexcepted bind\n");
					exit(1);
				}
				raddr = rp->rel.r_offset + relsect->info->shr.sh_addr;
				if ( !((((rp->rel.r_offset & 0xF0000000) | (4 * (data & 0x3FFFFFF))) - 4 - raddr) >> 18)
					|| (((rp->rel.r_offset & 0xF0000000) | (4 * (data & 0x3FFFFFF))) - 4 - raddr) >> 18 == 0x3FFF )
				{
					int jaddr;

					jaddr = (uint16_t)((((rp->rel.r_offset & 0xF0000000) | (4 * (data & 0x3FFFFFF))) - 4 - raddr) >> 2);
					if ( data >> 26 == 2 )
					{
						*(uint32_t *)daddr = jaddr | 0x10000000;
						rp->type = 0;
						++rmcount;
					}
					else if ( data >> 26 == 3 )
					{
						*(uint32_t *)daddr = jaddr | 0x4110000;
						rp->type = 0;
						++rmcount;
					}
				}
			}
		}
		else
		{
			++rmcount;
		}
		++rp;
	}
	if ( rmcount > 0 )
	{
		elf_rel *s;
		elf_rel *d;
		elf_rel *newtab;
		signed int j;

		newtab = (elf_rel *)calloc(entrise - rmcount, sizeof(elf_rel));
		d = newtab;
		s = (elf_rel *)relsect->data;
		for ( j = 0; entrise > j; ++j )
		{
			if ( s->type )
				memcpy(d++, s, sizeof(elf_rel));
			++s;
		}
		free(relsect->data);
		relsect->data = (uint8_t *)newtab;
		relsect->shr.sh_size = relsect->shr.sh_entsize * (entrise - rmcount);
	}
}

static void convert_relative_branch(elf_file *elf)
{
	int i;

	if ( elf->scp == NULL )
	{
		return;
	}
	for ( i = 1; i < elf->ehp->e_shnum; ++i )
	{
		if ( elf->scp[i]->shr.sh_type == 9 )
			convert_relative_branch_an_section(elf->scp[i]);
	}
}


















































