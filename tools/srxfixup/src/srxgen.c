
#include "all_include.h"

typedef struct _sect_org_data
{
	unsigned int org_addr; 
	unsigned int org_gp_value;
} Sect_org_data;

static int  setup_start_entry(elf_file *elf, srxfixup_const_char_ptr_t entrysym, elf_section *modinfo, int needoutput);
static Elf_file_slot * search_order_slots(srxfixup_const_char_ptr_t ordstr, elf_file *elf, Elf_file_slot *order);
static void  fixlocation_an_rel(elf_section *relsect, unsigned int startaddr);
static void  save_org_addrs(elf_file *elf);
static elf_section * add_iopmod(elf_file *elf);
static elf_section * add_eemod(elf_file *elf);
static void  modify_eemod(elf_file *elf, elf_section *eemod);
static void  add_reserved_symbol_table(Srx_gen_table *tp, srxfixup_const_char_ptr_t name, int bind, int type, SegConf *segment, srxfixup_const_char_ptr_t sectname, int shindex, int base);
static void  define_special_section_symbols(elf_file *elf);
static void  create_need_section(elf_file *elf);
static int  sect_name_match(srxfixup_const_char_ptr_t pattern, srxfixup_const_char_ptr_t name);
static int  reorder_section_table(elf_file *elf);
static void  create_phdr(elf_file *elf);
static void  check_change_bit(int oldbit, int newbit, int *up, int *down);
static void  segment_start_setup(SegConf *seglist, int bitid, const int *moffset);
static void  add_section_to_segment(SegConf *seglist, elf_section *scp, int bitid);
static void  segment_end_setup(SegConf *seglist, int bitid, int *moffset, int ee);
static void  update_modinfo(elf_file *elf);
static void  update_mdebug(elf_file *elf);
static void  update_programheader(elf_file *elf);
static void  remove_unuse_section(elf_file *elf);
static int  layout_srx_memory(elf_file *elf);
static CreateSymbolConf * is_reserve_symbol(Srx_gen_table *tp, srxfixup_const_char_ptr_t name);
static int  check_undef_symboles_an_reloc(elf_section *relsect);
static int  check_undef_symboles(elf_file *elf);
static int  create_reserved_symbols(elf_file *elf);
static void  symbol_value_update(elf_file *elf);
static void  rebuild_relocation(elf_file *elf, unsigned int gpvalue);
static int  check_irx12(elf_file *elf, int cause_irx1);
static void  setup_module_info(elf_file *elf, elf_section *modsect, srxfixup_const_char_ptr_t modulesymbol);
static void  rebuild_an_relocation(elf_section *relsect, unsigned int gpvalue, int target);
static int  iopmod_size(Elf32_IopMod *modinfo);
static int  eemod_size(Elf32_EeMod *modinfo);

int  convert_rel2srx(elf_file *elf, srxfixup_const_char_ptr_t entrysym, int needoutput, int cause_irx1)
{
	unsigned int seginfo;
	Srx_gen_table *tp;
	elf_section *modinfo;

	tp = (Srx_gen_table *)elf->optdata;
	save_org_addrs(elf);
	if ( tp->target == 1 )
	{
		modinfo = add_iopmod(elf);
	}
	else
	{
		if ( tp->target != 2 )
		{
			fprintf(stderr, "Internal error: target unknown\n");
			exit(1);
		}
		modinfo = add_eemod(elf);
	}
	remove_unuse_section(elf);
	define_special_section_symbols(elf);
	create_need_section(elf);
	reorder_section_table(elf);
	create_phdr(elf);
	if ( layout_srx_memory(elf) )
		return 1;
	modify_eemod(elf, modinfo);
	if ( create_reserved_symbols(elf) )
		return 1;
	if ( check_undef_symboles(elf) )
		return 1;
	symbol_value_update(elf);
	seginfo = lookup_segment(tp, (srxfixup_const_char_ptr_t)"GLOBALDATA", 1)->addr + 0x7FF0;
	rebuild_relocation(elf, seginfo);
	if ( check_irx12(elf, cause_irx1) )
		return 1;
	if ( setup_start_entry(elf, entrysym, modinfo, needoutput) )
		return 1;
	{
		const char *module_info_symbol;
		elf_syment *syp;

		module_info_symbol = "Module";
		syp = search_global_symbol("_irx_id", elf);
		if ( is_defined_symbol(syp) != 0 )
		{
			module_info_symbol = "_irx_id";
		}
		setup_module_info(elf, modinfo, module_info_symbol);
	}
	return layout_srx_file(elf);
}

static int  setup_start_entry(elf_file *elf, srxfixup_const_char_ptr_t entrysym, elf_section *modinfo, int needoutput)
{
	unsigned int sh_type;
	elf_syment *syp;

	if ( entrysym )
	{
		syp = search_global_symbol(entrysym, elf);
		if ( !is_defined_symbol(syp) )
		{
			fprintf(stderr, "Error: Cannot find entry symbol %s\n", entrysym);
			return 1;
		}
		else
		{
			elf->ehp->e_entry = get_symbol_value(syp, elf);
		}
	}
	else
	{
		syp = search_global_symbol((srxfixup_const_char_ptr_t)"start", elf);
		if ( !syp )
			syp = search_global_symbol((srxfixup_const_char_ptr_t)"_start", elf);
		if ( !is_defined_symbol(syp) )
		{
			if ( modinfo->shr.sh_type == SHT_SCE_EEMOD )
			{
				elf->ehp->e_entry = -1;
			}
			else if ( needoutput )
			{
				fprintf(stderr, "warning: Cannot find entry symbol `start' and `_start'\n");
			}
		}
		else
		{
			elf->ehp->e_entry = get_symbol_value(syp, elf);
		}
	}
	sh_type = modinfo->shr.sh_type;
	if ( sh_type == SHT_SCE_IOPMOD || sh_type == SHT_SCE_EEMOD )
	{
		*((uint32_t *)modinfo->data + 1) = elf->ehp->e_entry;
	}
	return 0;
}

static Elf_file_slot * search_order_slots(srxfixup_const_char_ptr_t ordstr, elf_file *elf, Elf_file_slot *order)
{
	elf_section **scp;
	int n;

	if ( !strcmp(ordstr, "@Section_header_table") )
	{
		while ( order->type != 100 )
		{
			if ( order->type == 4 )
				return order;
			++order;
		}
		return 0;
	}
	else if ( !strncmp(ordstr, "@Program_header_data ", 0x15u) )
	{
		n = strtol(ordstr + 21, NULL, 10);
		while ( order->type != 100 )
		{
			if ( order->type == 3 && order->d.php == &elf->php[n] )
				return order;
			++order;
		}
		return 0;
	}
	else
	{
		while ( order->type != 100 )
		{
			if ( order->type == 5 && !sect_name_match(ordstr, order->d.scp->name /* XXX: Check if correct union deref */) )
				return order;
			if ( order->type == 3 )
			{
				for ( scp = order->d.php->scp; *scp; ++scp )
				{
					if ( !sect_name_match(ordstr, (*scp)->name) )
						return order;
				}
			}
			++order;
		}
		return 0;
	}
}

int  layout_srx_file(elf_file *elf)
{
	const char *name;
	int align;
	unsigned int sh_addralign;
	elf_section **scp;
	Srx_gen_table *tp;
	Elf_file_slot *slotp_1;
	Elf_file_slot *slotp_2;
	Elf_file_slot *nslotp;
	Elf_file_slot *neworder;
	Elf_file_slot *order;
	srxfixup_const_char_ptr_t *ordstr;
	int max_seg_align;
	int error;
	int maxslot;

	tp = (Srx_gen_table *)elf->optdata;
	error = 0;
	if ( tp->target == 1 )
	{
		max_seg_align = 16;
	}
	else
	{
		if ( tp->target != 2 )
		{
			fprintf(stderr, "Internal error: target unknown\n");
			return 1;
		}
		max_seg_align = 0x10000;
	}
	reorder_symtab(elf);
	rebuild_section_name_strings(elf);
	rebuild_symbol_name_strings(elf);
	order = build_file_order_list(elf);
	for ( maxslot = 0; order[maxslot].type != 100; ++maxslot )
		;
	neworder = (Elf_file_slot *)calloc(maxslot + 1, sizeof(Elf_file_slot));
	memcpy(neworder, order, sizeof(Elf_file_slot));
	nslotp = neworder + 1;
	order->type = 0;
	if ( elf->ehp->e_phnum )
	{
		memcpy(nslotp, &order[1], sizeof(Elf_file_slot));
		nslotp = neworder + 2;
		order[1].type = 0;
	}
	for ( ordstr = tp->file_layout_order; *ordstr; ++ordstr )
	{
		while ( 1 )
		{
			slotp_1 = search_order_slots(*ordstr, elf, order);
			if ( !slotp_1 )
				break;
			memcpy(nslotp++, slotp_1, sizeof(Elf_file_slot));
			slotp_1->type = 0;
		}
	}
	nslotp->type = 100;
	shrink_file_order_list(neworder);
	writeback_file_order_list(elf, neworder);
	for ( slotp_2 = neworder; slotp_2->type != 100; ++slotp_2 )
	{
		if ( slotp_2->type == 3 && slotp_2->d.php->phdr.p_type == PT_LOAD && max_seg_align < slotp_2->align )
		{
			align = slotp_2->align;
			fprintf(stderr, "Program Header Entry: unsupported align %d\n", align);
			++error;
			for ( scp = slotp_2->d.php->scp; *scp; ++scp )
			{
				if ( max_seg_align < (signed int)(*scp)->shr.sh_addralign )
				{
					sh_addralign = (*scp)->shr.sh_addralign;
					name = (*scp)->name;
					fprintf(stderr, "Section '%s' : unsupported section align %d\n", name, (int)sh_addralign);
					++error;
				}
			}
		}
	}
	free(order);
	free(neworder);
	return error;
}

void  strip_elf(elf_file *elf)
{
	elf_syment **syp;
	elf_section *scp;
	int entrise;
	int d;
	int s;

	remove_section(elf, SHT_MIPS_DEBUG);
	scp = search_section(elf, SHT_SYMTAB);
	if ( scp == NULL )
	{
		return;
	}
	entrise = scp->shr.sh_size / scp->shr.sh_entsize;
	syp = (elf_syment **)scp->data;
	s = 1;
	d = 1;
	while ( entrise > s )
	{
		if ( syp[s]->refcount <= 0 )
			syp[s]->number = -1;
		else
			syp[d++] = syp[s];
		++s;
	}
	scp->shr.sh_size = d * scp->shr.sh_entsize;
}

SegConf * lookup_segment(Srx_gen_table *conf, srxfixup_const_char_ptr_t segname, int msgsw)
{
	SegConf *i;

	for ( i = conf->segment_list; i->name; ++i )
	{
		if ( !strcmp(segname, i->name) )
			return i;
	}
	if ( msgsw )
	{
		fprintf(stderr, "segment '%s' not found \n", segname);
	}
	return 0;
}

static void  fixlocation_an_rel(elf_section *relsect, unsigned int startaddr)
{
	int type;
	uint32_t r_offset;
	unsigned int sh_addr;
	unsigned int mhioff;
	int v16;
	int v17;
	int daddr1;
	uint8_t *datal;
	unsigned int data_1;
	uint32_t data_2;
	int data_3;
	unsigned int data_4;
	uint16_t data_5;
	elf_syment **symp;
	elf_rel *rp;
	signed int entrise;
	signed int i;

	entrise = relsect->shr.sh_size / relsect->shr.sh_entsize;
	rp = (elf_rel *)relsect->data;
	symp = (elf_syment **)relsect->link->data;
	for ( i = 0; entrise > i; ++i )
	{
		if ( *symp != rp->symptr )
		{
			fprintf(stderr, "Internal error: Illegal relocation entry\n");
			exit(1);
		}
		if ( relsect->info->shr.sh_addr > rp->rel.r_offset
			|| rp->rel.r_offset >= relsect->info->shr.sh_size + relsect->info->shr.sh_addr )
		{
			mhioff = relsect->info->shr.sh_size + relsect->info->shr.sh_addr;
			sh_addr = relsect->info->shr.sh_addr;
			r_offset = rp->rel.r_offset;
			fprintf(
				stderr,
				"Panic !! relocation #%d offset=0x%x range out (section limit addr=0x%x-0x%x)\n",
				i,
				r_offset,
				sh_addr,
				mhioff);
			exit(1);
		}
		datal = &relsect->info->data[rp->rel.r_offset - relsect->info->shr.sh_addr];
		type = rp->type;
		switch ( type )
		{
			case R_MIPS_16:
				data_1 = startaddr + (int16_t)*(uint32_t *)datal;
				if ( (uint16_t)(data_1 >> 16) )
				{
					if ( (uint16_t)(data_1 >> 16) != 0xFFFF )
					{
						fprintf(stderr, "REFHALF data overflow\n");
						exit(1);
					}
				}
				*(uint32_t *)datal &= 0xFFFF0000;
				*(uint32_t *)datal |= (uint16_t)data_1;
				break;
			case R_MIPS_32:
				*(uint32_t *)datal += startaddr;
				break;
			case R_MIPS_26:
				if ( rp->symptr->bind != STB_LOCAL )
				{
					fprintf(stderr, "R_MIPS_26 Unexcepted bind\n");
					exit(1);
				}
				data_2 = startaddr + ((rp->rel.r_offset & 0xF0000000) | (4 * (*(uint32_t *)datal & 0x3FFFFFF)));
				*(uint32_t *)datal &= 0xFC000000;
				*(uint32_t *)datal |= (16 * data_2) >> 6;
				break;
			case R_MIPS_HI16:
				if ( i == entrise + 1 || rp[1].type != R_MIPS_LO16 || rp[1].symptr != rp->symptr )
				{
					fprintf(stderr, "R_MIPS_HI16 without R_MIPS_LO16\n");
					exit(1);
				}
				data_4 = startaddr
							 + (int16_t)*(uint32_t *)&relsect->info->data[rp[1].rel.r_offset - relsect->info->shr.sh_addr]
							 + (*(uint32_t *)datal << 16);
				*(uint32_t *)datal &= 0xFFFF0000;
				*(uint32_t *)datal |= (uint16_t)(((data_4 >> 15) + 1) >> 1);
				break;
			case R_MIPS_LO16:
				data_5 = startaddr + *(uint32_t *)datal;
				*(uint32_t *)datal &= 0xFFFF0000;
				*(uint32_t *)datal |= data_5;
				break;
			case R_MIPS_GPREL16:
				fprintf(stderr, "Unexcepted R_MIPS_GPREL16\n");
				exit(1);
				return;
			case R_MIPS_LITERAL:
				fprintf(stderr, "Unexcepted R_MIPS_LITERAL\n");
				exit(1);
				return;
			case R_MIPSSCE_MHI16:
				if ( i == entrise + 1 || rp[1].type != R_MIPSSCE_ADDEND )
				{
					fprintf(stderr, "R_MIPSSCE_MHI16 without R_MIPSSCE_ADDEND\n");
					exit(1);
				}
				data_3 = (uint16_t)((((startaddr + rp[1].rel.r_offset) >> 15) + 1) >> 1);
				for ( daddr1 = 1; daddr1; datal += daddr1 )
				{
					daddr1 = *(uint16_t *)datal << 16 >> 14;
					*(uint32_t *)datal &= 0xFFFF0000;
					*(uint32_t *)datal |= data_3;
				}
				++rp;
				++i;
				break;
			case R_MIPS_REL32:
			case R_MIPS_GOT16:
			case R_MIPS_PC16:
			case R_MIPS_CALL16:
			case R_MIPS_GPREL32:
			case R_MIPS_GOTHI16:
			case R_MIPS_GOTLO16:
			case R_MIPS_CALLHI16:
			case R_MIPS_CALLLO16:
				v16 = rp->type;
				fprintf(stderr, "unacceptable relocation type: 0x%x\n", v16);
				exit(1);
				return;
			default:
				v17 = rp->type;
				fprintf(stderr, "unknown relocation type: 0x%x\n", v17);
				exit(1);
				return;
		}
		++rp;
	}
}

void  fixlocation_elf(elf_file *elf, unsigned int startaddr)
{
	int entrise;
	int i;
	int k;
	int d;
	int s;
	int j;
	elf_syment **syp;
	elf_section *scp;
	elf_section *modsect_1;
	elf_section *modsect_2;

	if ( elf->scp == NULL )
	{
		return;
	}
	s = 1;
	d = 1;
	while ( s < elf->ehp->e_shnum )
	{
		if ( elf->scp[s]->shr.sh_type == SHT_REL )
			fixlocation_an_rel(elf->scp[s], startaddr);
		else
			elf->scp[d++] = elf->scp[s];
		++s;
	}
	elf->ehp->e_shnum = d;
	elf->ehp->e_entry += startaddr;
	modsect_1 = search_section(elf, SHT_SCE_IOPMOD);
	if ( modsect_1 )
	{
		Elf32_IopMod *iopmodinfo;

		iopmodinfo = (Elf32_IopMod *)modsect_1->data;
		if ( iopmodinfo->moduleinfo != -1 )
			iopmodinfo->moduleinfo += startaddr;
		iopmodinfo->entry += startaddr;
		iopmodinfo->gp_value += startaddr;
	}
	modsect_2 = search_section(elf, SHT_SCE_EEMOD);
	if ( modsect_2 )
	{
		Elf32_EeMod *eemodinfo;

		eemodinfo = (Elf32_EeMod *)modsect_2->data;
		if ( eemodinfo->moduleinfo != -1 )
			eemodinfo->moduleinfo += startaddr;
		eemodinfo->entry += startaddr;
		eemodinfo->gp_value += startaddr;
	}
	for ( i = 0; i < elf->ehp->e_phnum; ++i )
	{
		if ( elf->php[i].phdr.p_type == PT_LOAD )
		{
			elf->php[i].phdr.p_vaddr = startaddr;
			elf->php[i].phdr.p_paddr = startaddr;
			break;
		}
	}
	for ( j = 1; j < elf->ehp->e_shnum; ++j )
	{
		unsigned int sh_type;

		sh_type = elf->scp[j]->shr.sh_type;
		if ( sh_type == SHT_PROGBITS || sh_type == SHT_NOBITS )
			elf->scp[j]->shr.sh_addr += startaddr;
	}
	scp = search_section(elf, SHT_SYMTAB);
	if ( scp == NULL )
	{
		return;
	}
	entrise = scp->shr.sh_size / scp->shr.sh_entsize;
	syp = (elf_syment **)scp->data;
	for ( k = 1; entrise > k; ++k )
	{
		if ( syp[k]->sym.st_shndx == SHN_RADDR || (syp[k]->sym.st_shndx && syp[k]->sym.st_shndx <= 0xFEFFu) )
			syp[k]->sym.st_value += startaddr;
		if ( syp[k]->sym.st_shndx == SHN_RADDR )
			syp[k]->sym.st_shndx = -15;
	}
}

static void  save_org_addrs(elf_file *elf)
{
	Elf32_RegInfo *data;
	Elf32_RegInfo *reginfop;
	elf_section *reginfosec;
	int i;

	reginfosec = search_section(elf, SHT_MIPS_REGINFO);
	if ( reginfosec )
		data = (Elf32_RegInfo *)reginfosec->data;
	else
		data = 0;
	reginfop = data;
	for ( i = 1; i < elf->ehp->e_shnum; ++i )
	{
		Sect_org_data *org;

		org = (Sect_org_data *)calloc(1u, sizeof(Sect_org_data));
		org->org_addr = elf->scp[i]->shr.sh_addr;
		if ( reginfop )
			org->org_gp_value = reginfop->ri_gp_value;
		elf->scp[i]->optdata = (void *)org;
	}
}

static elf_section * add_iopmod(elf_file *elf)
{
	Elf32_IopMod *iopmodp;
	elf_section *modsect;

	modsect = (elf_section *)malloc(sizeof(elf_section));
	memset(modsect, 0, sizeof(elf_section));
	iopmodp = (Elf32_IopMod *)malloc(sizeof(Elf32_IopMod));
	memset(iopmodp, 0, sizeof(Elf32_IopMod));
	iopmodp->moduleinfo = -1;
	modsect->name = strdup(".iopmod");
	modsect->data = (uint8_t *)iopmodp;
	modsect->shr.sh_type = SHT_SCE_IOPMOD;
	modsect->shr.sh_size = iopmod_size(iopmodp);
	modsect->shr.sh_addralign = 4;
	modsect->shr.sh_entsize = 0;
	add_section(elf, modsect);
	return modsect;
}

static elf_section * add_eemod(elf_file *elf)
{
	Elf32_EeMod *eemodp;
	elf_section *modsect;

	modsect = (elf_section *)malloc(sizeof(elf_section));
	memset(modsect, 0, sizeof(elf_section));
	eemodp = (Elf32_EeMod *)malloc(sizeof(Elf32_EeMod));
	memset(eemodp, 0, sizeof(Elf32_EeMod));
	eemodp->moduleinfo = -1;
	modsect->name = strdup(".eemod");
	modsect->data = (uint8_t *)eemodp;
	modsect->shr.sh_type = SHT_SCE_EEMOD;
	modsect->shr.sh_size = eemod_size(eemodp);
	modsect->shr.sh_addralign = 4;
	modsect->shr.sh_entsize = 0;
	add_section(elf, modsect);
	return modsect;
}

static void  modify_eemod(elf_file *elf, elf_section *eemod)
{
	elf_section *scp_1;
	elf_section *scp_2;
	Elf32_EeMod *moddata;

	if ( eemod->shr.sh_type != SHT_SCE_EEMOD )
	{
		return;
	}
	moddata = (Elf32_EeMod *)eemod->data;
	scp_1 = search_section_by_name(elf, (srxfixup_const_char_ptr_t)".erx.lib");
	if ( scp_1 )
	{
		moddata->erx_lib_addr = scp_1->shr.sh_addr;
		moddata->erx_lib_size = scp_1->shr.sh_size;
	}
	else
	{
		moddata->erx_lib_addr = -1;
		moddata->erx_lib_size = 0;
	}
	scp_2 = search_section_by_name(elf, (srxfixup_const_char_ptr_t)".erx.stub");
	if ( scp_2 )
	{
		moddata->erx_stub_addr = scp_2->shr.sh_addr;
		moddata->erx_stub_size = scp_2->shr.sh_size;
	}
	else
	{
		moddata->erx_stub_addr = -1;
		moddata->erx_stub_size = 0;
	}
}

static void  add_reserved_symbol_table(Srx_gen_table *tp, srxfixup_const_char_ptr_t name, int bind, int type, SegConf *segment, srxfixup_const_char_ptr_t sectname, int shindex, int base)
{
	CreateSymbolConf *newent_1;
	CreateSymbolConf *newent_2;
	int entries;

	entries = 1;
	for ( newent_1 = tp->create_symbols; newent_1->name; ++newent_1 )
		++entries;
	tp->create_symbols = (CreateSymbolConf *)realloc(tp->create_symbols, (entries + 1) * sizeof(CreateSymbolConf));
	memset(&tp->create_symbols[entries], 0, sizeof(tp->create_symbols[entries]));
	newent_2 = &tp->create_symbols[entries - 1];
	newent_2->name = strdup(name);
	newent_2->bind = bind;
	newent_2->type = type;
	newent_2->segment = segment;
	newent_2->sectname = strdup(sectname);
	newent_2->shindex = shindex;
	newent_2->seflag = base;
}

const char *bos_str = "_begin_of_section_";
const char *eos_str = "_end_of_section_";
static void  define_special_section_symbols(elf_file *elf)
{
	int v2;
	size_t v3;
	elf_syment *v4;
	size_t v5;
	elf_syment *v6;
	char *sectname;
	elf_syment *sym;
	elf_syment **syp;
	elf_section *scp;
	int entrise;
	int i;
	Srx_gen_table *tp;

	tp = (Srx_gen_table *)(elf->optdata);
	scp = search_section(elf, SHT_SYMTAB);
	if ( scp == NULL )
	{
		return;
	}
	sectname = (char *)__builtin_alloca(4 * ((elf->shstrptr->shr.sh_size + 22) >> 2));
	v2 = scp->shr.sh_size / scp->shr.sh_entsize;
	entrise = v2;
	syp = (elf_syment **)(scp->data);
	for ( i = 1; entrise > i; ++i )
	{
		sym = syp[i];
		if ( sym->bind == STB_GLOBAL && !sym->sym.st_shndx )
		{
			v3 = strlen(bos_str);
			if ( !strncmp(bos_str, sym->name, v3) )
			{
				strcpy(sectname, ".");
				v4 = sym;
				strcat(sectname, &v4->name[strlen(bos_str)]);
				add_reserved_symbol_table(tp, sym->name, 2, 1, 0, sectname, 0, 0);
			}
			v5 = strlen(eos_str);
			if ( !strncmp(eos_str, sym->name, v5) )
			{
				strcpy(sectname, ".");
				v6 = sym;
				strcat(sectname, &v6->name[strlen(eos_str)]);
				add_reserved_symbol_table(tp, sym->name, 2, 1, 0, sectname, 65311, 1);
			}
		}
	}
}

static void  create_need_section(elf_file *elf)
{
	elf_section *scp;
	int i;
	CreateSymbolConf *csym;
	Srx_gen_table *tp;

	tp = (Srx_gen_table *)elf->optdata;
	scp = search_section(elf, SHT_SYMTAB);
	if ( scp == NULL )
	{
		return;
	}
	for ( i = 1; i < (signed int)(scp->shr.sh_size / scp->shr.sh_entsize); ++i )
	{
		elf_syment *syment;

		syment = *(elf_syment **)&scp->data[i * sizeof(void *)];
		if ( !syment->sym.st_shndx )
		{
			csym = is_reserve_symbol(tp, syment->name);
			if ( csym )
			{
				if ( csym->segment )
				{
					elf_section *addscp_1;

					addscp_1 = csym->segment->empty_section;
					if ( addscp_1 )
					{
						if ( !search_section_by_name(elf, addscp_1->name) )
							add_section(elf, addscp_1);
					}
				}
				else if ( csym->sectname && !search_section_by_name(elf, csym->sectname) )
				{
					SectConf *odr;

					for ( odr = tp->section_list; odr->sect_name_pattern; ++odr )
					{
						if ( !sect_name_match(odr->sect_name_pattern, csym->sectname) && odr->secttype && odr->sectflag )
						{
							elf_section *addscp_2;

							addscp_2 = (elf_section *)calloc(1u, sizeof(elf_section));
							addscp_2->name = strdup(csym->sectname);
							addscp_2->shr.sh_type = odr->secttype;
							addscp_2->shr.sh_flags = odr->sectflag;
							addscp_2->shr.sh_size = 0;
							addscp_2->shr.sh_addralign = 4;
							addscp_2->shr.sh_entsize = 0;
							add_section(elf, addscp_2);
							break;
						}
					}
				}
			}
		}
	}
}

static int  sect_name_match(srxfixup_const_char_ptr_t pattern, srxfixup_const_char_ptr_t name)
{
	size_t v3;
	size_t v4;
	const char *v5;

	while ( *pattern && *name && *pattern != '*' )
	{
		if ( *name > *pattern )
			return -1;
		if ( *name < *pattern )
			return 1;
		++pattern;
		++name;
	}
	if ( !*pattern && !*name )
		return 0;
	if ( *pattern == '*' && !pattern[1] )
		return 0;
	if ( *pattern != '*' )
		return strcmp(pattern, name);
	++pattern;
	v3 = strlen(name);
	if ( v3 < strlen(pattern) )
		return strcmp(pattern, name);
	v4 = strlen(name);
	v5 = &name[v4 - strlen(pattern)];
	return strcmp(pattern, v5);
}

static int  reorder_section_table(elf_file *elf)
{
	srxfixup_const_char_ptr_t *secorder;
	int sections;
	elf_section **scp;
	int d;
	int s;

	sections = elf->ehp->e_shnum;
	secorder = ((Srx_gen_table *)elf->optdata)->section_table_order;
	scp = (elf_section **)calloc(sections + 1, sizeof(elf_section *));
	memcpy(scp, elf->scp, sections * sizeof(elf_section *));
	*elf->scp = *scp;
	d = 1;
	while ( *secorder )
	{
		for ( s = 1; sections > s; ++s )
		{
			if ( scp[s] )
			{
				if ( !sect_name_match(*secorder, scp[s]->name) )
				{
					elf->scp[d] = scp[s];
					scp[s] = 0;
					++d;
				}
			}
		}
		++secorder;
	}
	free(scp);
	reorder_symtab(elf);
	return 0;
}

static void  create_phdr(elf_file *elf)
{
	const char *section_name;
	PheaderInfo *phip;
	int i;
	int j;

	phip = ((Srx_gen_table *)(elf->optdata))->program_header_order;
	for ( i = 0; phip[i].sw; ++i )
		;
	elf->php = (elf_proghead *)malloc(i * sizeof(elf_proghead));
	memset(elf->php, 0, i * sizeof(elf_proghead));
	elf->ehp->e_phentsize = 32;
	elf->ehp->e_phnum = i;
	for ( j = 0; phip[j].sw; ++j )
	{
		int sw;

		sw = phip[j].sw;
		if ( sw == 1 )
		{
			elf->php[j].phdr.p_flags = 4;
			elf->php[j].phdr.p_align = 4;
			if ( !strcmp(".iopmod", phip[j].d.section_name) )
			{
				elf->php[j].phdr.p_type = PT_SCE_IOPMOD;
				elf->php[j].phdr.p_filesz = 28;
				elf->php[j].scp = (elf_section **)calloc(2u, sizeof(elf_section *));
				*elf->php[j].scp = search_section(elf, SHT_SCE_IOPMOD);
			}
			else if ( !strcmp(".eemod", phip[j].d.section_name) )
			{
				elf->php[j].phdr.p_type = PT_SCE_EEMOD;
				elf->php[j].phdr.p_filesz = 44;
				elf->php[j].scp = (elf_section **)calloc(2u, sizeof(elf_section *));
				*elf->php[j].scp = search_section(elf, SHT_SCE_EEMOD);
			}
			else
			{
				section_name = phip[j].d.section_name;
				fprintf(stderr, "Unsuport section '%s' for program header\n", section_name);
			}
		}
		else if ( sw == 2 )
		{
			elf->php[j].phdr.p_type = PT_LOAD;
			elf->php[j].phdr.p_flags = 7;
			elf->php[j].phdr.p_align = 16;
		}
	}
}

static void  check_change_bit(int oldbit, int newbit, int *up, int *down)
{
	*up = ~oldbit & newbit & (newbit ^ oldbit);
	*down = ~newbit & oldbit & (newbit ^ oldbit);
}

static void  segment_start_setup(SegConf *seglist, int bitid, const int *moffset)
{
	while ( seglist->name )
	{
		if ( (seglist->bitid & bitid) != 0 )
		{
			seglist->addr = *moffset;
			seglist->size = 0;
		}
		++seglist;
	}
}

static void  add_section_to_segment(SegConf *seglist, elf_section *scp, int bitid)
{
	while ( seglist->name )
	{
		if ( (seglist->bitid & bitid) != 0 )
		{
			if ( !seglist->nsect )
				seglist->addr = scp->shr.sh_addr;
			seglist->scp = (elf_section **)realloc(seglist->scp, (++seglist->nsect + 1) * sizeof(elf_section *));
			seglist->scp[seglist->nsect - 1] = scp;
			seglist->scp[seglist->nsect] = 0;
		}
		++seglist;
	}
}

static void  segment_end_setup(SegConf *seglist, int bitid, int *moffset, int ee)
{
	while ( seglist->name )
	{
		if ( (seglist->bitid & bitid) != 0 )
		{
			if ( ee )
			{
				if ( !strcmp(seglist->name, "TEXT") )
					*moffset += 32;
			}
			seglist->size = *moffset - seglist->addr;
		}
		++seglist;
	}
}

static void  update_modinfo(elf_file *elf)
{
	Srx_gen_table *tp;
	SegConf *seginfo;
	SegConf *seginfo_4;
	SegConf *seginfo_8;
	SegConf *seginfo_12;
	elf_section *scp_1;
	elf_section *scp_2;

	tp = (Srx_gen_table *)elf->optdata;
	seginfo = lookup_segment(tp, (srxfixup_const_char_ptr_t)"TEXT", 1);
	seginfo_4 = lookup_segment(tp, (srxfixup_const_char_ptr_t)"DATA", 1);
	seginfo_8 = lookup_segment(tp, (srxfixup_const_char_ptr_t)"BSS", 1);
	seginfo_12 = lookup_segment(tp, (srxfixup_const_char_ptr_t)"GLOBALDATA", 1);
	if ( !seginfo || !seginfo_4 || !seginfo_8 || !seginfo_12 )
	{
		fprintf(stderr, "TEXT,DATA,BSS,GLOBALDATA segment missing abort");
		exit(1);
	}
	scp_1 = search_section(elf, SHT_SCE_IOPMOD);
	if ( scp_1 )
	{
		Elf32_IopMod *imp;

		scp_1->shr.sh_addr = 0;
		imp = (Elf32_IopMod *)scp_1->data;
		imp->text_size = seginfo_4->addr - seginfo->addr;
		imp->data_size = seginfo_8->addr - seginfo_4->addr;
		imp->bss_size = seginfo_8->size;
		imp->gp_value = seginfo_12->addr + 0x7FF0;
	}
	scp_2 = search_section(elf, SHT_SCE_EEMOD);
	if ( scp_2 )
	{
		Elf32_EeMod *emp;

		scp_2->shr.sh_addr = 0;
		emp = (Elf32_EeMod *)scp_2->data;
		emp->text_size = seginfo_4->addr - seginfo->addr;
		emp->data_size = seginfo_8->addr - seginfo_4->addr;
		emp->bss_size = seginfo_8->size;
		emp->gp_value = seginfo_12->addr + 0x7FF0;
	}
}

static void  update_mdebug(elf_file *elf)
{
	elf_section *scp;

	scp = search_section(elf, SHT_MIPS_DEBUG);
	if ( scp )
		scp->shr.sh_addr = 0;
}

static void  update_programheader(elf_file *elf)
{
	int v1;
	SegConf **segp;
	elf_section **scp;
	PheaderInfo *phip;
	int minsegalign;
	signed int align;
	int nsect_1;
	int nsect_2;
	int nseg_1;
	int nseg_2;
	int s;
	int n;

	phip = ((Srx_gen_table *)(elf->optdata))->program_header_order;
	v1 = ((Srx_gen_table *)elf->optdata)->target;
	if ( v1 == 1 )
	{
		minsegalign = 16;
	}
	else
	{
		if ( v1 != 2 )
		{
			fprintf(stderr, "Internal error: target unknown\n");
			exit(1);
		}
		minsegalign = 128;
	}
	for ( n = 0; phip[n].sw; ++n )
	{
		if ( phip[n].sw == 2 )
		{
			segp = phip[n].d.segment_list;
			if ( segp )
			{
				elf->php[n].phdr.p_vaddr = (*segp)->addr;
				align = minsegalign;
				nseg_1 = 0;
				nsect_1 = 0;
				while ( segp[nseg_1] )
					nsect_1 += segp[nseg_1++]->nsect;
				scp = (elf_section **)calloc(nsect_1 + 1, sizeof(elf_section *));
				elf->php[n].scp = scp;
				nseg_2 = 0;
				nsect_2 = 0;
				while ( segp[nseg_2] )
				{
					memcpy(&scp[nsect_2], segp[nseg_2]->scp, segp[nseg_2]->nsect * sizeof(elf_section *));
					nsect_2 += segp[nseg_2++]->nsect;
				}
				for ( s = 0; nsect_2 > s && scp[s]->shr.sh_type == SHT_PROGBITS; ++s )
				{
					if ( (signed int)scp[s]->shr.sh_addralign > align )
						align = scp[s]->shr.sh_addralign;
				}
				elf->php[n].phdr.p_filesz = scp[s - 1]->shr.sh_size + scp[s - 1]->shr.sh_addr - (*scp)->shr.sh_addr;
				elf->php[n].phdr.p_memsz = scp[nsect_2 - 1]->shr.sh_size + scp[nsect_2 - 1]->shr.sh_addr - (*scp)->shr.sh_addr;
				while ( nsect_2 > s )
				{
					if ( (signed int)scp[s]->shr.sh_addralign > align )
						align = scp[s]->shr.sh_addralign;
					++s;
				}
				elf->php[n].phdr.p_align = align;
			}
		}
	}
}

static void  remove_unuse_section(elf_file *elf)
{
	srxfixup_const_char_ptr_t *sectnames;
	int sections;
	elf_section **dscp;
	int d;
	int i;
	int j;

	sections = elf->ehp->e_shnum;
	sectnames = ((Srx_gen_table *)(elf->optdata))->removesection_list;
	dscp = (elf_section **)calloc(sections + 1, sizeof(elf_section *));
	memset(dscp, 0, sections * sizeof(elf_section *));
	d = 0;
	while ( *sectnames )
	{
		for ( i = 1; sections > i; ++i )
		{
			if ( elf->scp[i] )
			{
				if ( !sect_name_match(*sectnames, elf->scp[i]->name) )
					dscp[d++] = elf->scp[i];
			}
		}
		++sectnames;
	}
	for ( j = 0; sections > j; ++j )
	{
		if ( dscp[j] )
			remove_section_by_name(elf, dscp[j]->name);
	}
	free(dscp);
}

static int  layout_srx_memory(elf_file *elf)
{
	const char *r;
	int s_3;
	elf_section **scp;
	SectConf *odr;
	Srx_gen_table *tp;
	int error;
	int is_ee;
	int sections;
	int s_1;
	int s_2;
	int downdelta;
	int updelta;
	int oldbitid;
	int moffset;

	tp = (Srx_gen_table *)elf->optdata;
	moffset = 0;
	oldbitid = 0;
	error = 0;
	is_ee = tp->target == 2;
	if ( !elf->scp )
		return 1;
	sections = elf->ehp->e_shnum;
	scp = (elf_section **)calloc(sections + 1, sizeof(elf_section *));
	memcpy(scp, elf->scp, sections * sizeof(elf_section *));
	for ( odr = tp->section_list; odr->sect_name_pattern; ++odr )
	{
		check_change_bit(oldbitid, odr->flag, &updelta, &downdelta);
		if ( updelta )
			segment_start_setup(tp->segment_list, updelta, &moffset);
		for ( s_1 = 1; sections > s_1; ++s_1 )
		{
			if ( scp[s_1] && !sect_name_match(odr->sect_name_pattern, scp[s_1]->name) && (scp[s_1]->shr.sh_flags & 2) != 0 )
			{
				moffset = adjust_align(moffset, scp[s_1]->shr.sh_addralign);
				scp[s_1]->shr.sh_addr = moffset;
				moffset += scp[s_1]->shr.sh_size;
				add_section_to_segment(tp->segment_list, scp[s_1], odr->flag);
				scp[s_1] = 0;
			}
		}
		oldbitid = odr->flag;
		check_change_bit(oldbitid, odr[1].flag, &updelta, &downdelta);
		if ( downdelta )
			segment_end_setup(tp->segment_list, downdelta, &moffset, is_ee);
	}
	for ( s_2 = 1; sections > s_2; ++s_2 )
	{
		if ( scp[s_2] && (scp[s_2]->shr.sh_flags & 2) != 0 )
		{
			for ( s_3 = 1;
						sections > s_3
				 && (!scp[s_3] || (scp[s_3]->shr.sh_type != SHT_RELA && scp[s_3]->shr.sh_type != SHT_REL) || scp[s_2] != scp[s_3]->info);
						++s_3 );
			r = scp[s_2]->name;
			if ( sections > s_3 )
			{
				fprintf(stderr, "Error: section '%s' needs allocation and has relocation data but not in program segment\n", r);
				scp[s_3] = 0;
				++error;
			}
			else
			{
				fprintf(stderr, "Warning: section '%s' needs allocation but not in program segment\n", r);
				scp[s_2] = 0;
			}
		}
	}
	free(scp);
	if ( !error )
	{
		update_modinfo(elf);
		update_mdebug(elf);
		update_programheader(elf);
	}
	return error;
}

static CreateSymbolConf * is_reserve_symbol(Srx_gen_table *tp, srxfixup_const_char_ptr_t name)
{
	CreateSymbolConf *csyms;

	if ( !name )
		return 0;
	for ( csyms = tp->create_symbols; csyms->name; ++csyms )
	{
		if ( !strcmp(csyms->name, name) )
			return csyms;
	}
	return 0;
}

static int  check_undef_symboles_an_reloc(elf_section *relsect)
{
	const char *v5;
	const char *v6;
	const char *name;
	int st_shndx;
	elf_syment **symp;
	elf_rel *rp;
	int undefcount;
	signed int entrise;
	signed int i;

	entrise = relsect->shr.sh_size / relsect->shr.sh_entsize;
	rp = (elf_rel *)relsect->data;
	symp = (elf_syment **)relsect->link->data;
	undefcount = 0;
	for ( i = 0; entrise > i; ++i )
	{
		if ( *symp != rp->symptr )
		{
			if ( rp->symptr->sym.st_shndx )
			{
				if ( rp->symptr->sym.st_shndx > 0xFEFFu
					&& rp->symptr->sym.st_shndx != SHN_ABS
					&& rp->symptr->sym.st_shndx != SHN_RADDR )
				{
					if ( rp->symptr->sym.st_shndx == SHN_COMMON )
					{
						name = rp->symptr->name;
						fprintf(stderr, "  unallocated variable `%s'\n", name);
					}
					else
					{
						st_shndx = rp->symptr->sym.st_shndx;
						v5 = rp->symptr->name;
						fprintf(stderr, "  `%s' unknown symbol type %x\n", v5, st_shndx);
					}
					++undefcount;
				}
			}
			else if ( rp->symptr->bind != STB_WEAK )
			{
				v6 = rp->symptr->name;
				fprintf(stderr, "  undefined reference to `%s'\n", v6);
				++undefcount;
			}
		}
		++rp;
	}
	return undefcount;
}

static int  check_undef_symboles(elf_file *elf)
{
	int err;
	int s;

	if ( !elf->scp )
		return 0;
	err = 0;
	for ( s = 1; s < elf->ehp->e_shnum; ++s )
	{
		if ( elf->scp[s]->shr.sh_type == SHT_REL )
			err += check_undef_symboles_an_reloc(elf->scp[s]);
	}
	return err;
}

static srxfixup_const_char_ptr_t SymbolType[] = { "STT_NOTYPE", "STT_OBJECT", "STT_FUNC", "STT_SECTION", "STT_FILE" };
static int  create_reserved_symbols(elf_file *elf)
{
	int v2;
	int seflag;
	const char *size;
	const char *addr;
	int csyms_;
	unsigned int sh_size;
	unsigned int sh_addr;
	elf_section *scp;
	elf_syment *sym;
	CreateSymbolConf *csyms;
	Srx_gen_table *tp;

	tp = (Srx_gen_table *)elf->optdata;
	csyms_ = 0;
	if ( !search_section(elf, SHT_SYMTAB) )
		return 1;
	for ( csyms = tp->create_symbols; csyms->name; ++csyms )
	{
		sym = search_global_symbol(csyms->name, elf);
		if ( !sym
			&& (csyms->shindex > 65279
			 || (csyms->segment && csyms->segment->scp)
			 || (!csyms->segment && csyms->sectname && search_section_by_name(elf, csyms->sectname))) )
		{
			sym = add_symbol(elf, csyms->name, csyms->bind, 0, 0, 0, 0);
		}
		if ( sym )
		{
			sh_size = 0;
			sh_addr = 0;
			scp = 0;
			if ( csyms->segment )
			{
				sh_addr = csyms->segment->addr;
				sh_size = csyms->segment->size;
				if ( csyms->shindex <= 65279 )
					scp = *csyms->segment->scp;
			}
			else if ( csyms->sectname != NULL )
			{
				scp = search_section_by_name(elf, csyms->sectname);
				if ( scp )
				{
					sh_addr = scp->shr.sh_addr;
					sh_size = scp->shr.sh_size;
				}
			}
			if ( csyms->segment || scp )
			{
				if ( sym->sym.st_shndx )
				{
					addr = SymbolType[sym->type];
					size = sym->name;
					fprintf(stderr, "Unexcepted Symbol \"%s\":%s \n", size, addr);
					++csyms_;
				}
				else
				{
					sym->bind = csyms->bind;
					if ( !sym->type )
						sym->type = csyms->type;
					sym->sym.st_info = ((csyms->bind & 0xFF) << 4) + (csyms->type & 0xF);
					if ( csyms->shindex > 0xFEFF )
					{
						sym->sym.st_shndx = csyms->shindex;
						sym->shptr = 0;
						seflag = csyms->seflag;
						if ( seflag == 1 )
						{
							sym->sym.st_value = sh_size + sh_addr;
						}
						else if ( seflag > 1 )
						{
							if ( seflag == 2 )
								sym->sym.st_value = sh_addr + 0x7FF0;
						}
						else if ( !seflag )
						{
							sym->sym.st_value = sh_addr;
						}
					}
					else
					{
						sym->sym.st_shndx = 1;
						sym->shptr = scp;
						v2 = csyms->seflag;
						if ( v2 )
						{
							if ( v2 == 1 )
								sym->sym.st_value = sh_size;
						}
						else
						{
							sym->sym.st_value = 0;
						}
					}
				}
				continue;
			}
			if ( csyms->bind == STB_WEAK && (sym->bind == STB_GLOBAL || sym->bind == STB_WEAK) && !sym->sym.st_shndx )
			{
				if ( !sym->type )
					sym->type = csyms->type;
				sym->bind = csyms->bind;
				sym->sym.st_info = ((csyms->bind & 0xFF) << 4) + (csyms->type & 0xF);
			}
		}
	}
	return csyms_;
}

static void  symbol_value_update(elf_file *elf)
{
	int entrise;
	int i;
	elf_syment **syp;
	elf_section *scp;
	int target;

	target = ((Srx_gen_table *)(elf->optdata))->target;
	if ( elf->ehp->e_type != ET_REL )
	{
		return;
	}
	if ( target == 1 )
		elf->ehp->e_type = ET_SCE_IOPRELEXEC;
	if ( target == 2 )
		elf->ehp->e_type = ET_SCE_EERELEXEC2;
	scp = search_section(elf, SHT_SYMTAB);
	if ( scp == NULL )
	{
		return;
	}
	entrise = scp->shr.sh_size / scp->shr.sh_entsize;
	syp = (elf_syment **)scp->data;
	for ( i = 1; entrise > i; ++i )
	{
		if ( syp[i]->sym.st_shndx )
		{
			if ( syp[i]->sym.st_shndx <= 0xFEFFu )
				syp[i]->sym.st_value += syp[i]->shptr->shr.sh_addr;
		}
	}
}

static void  rebuild_relocation(elf_file *elf, unsigned int gpvalue)
{
	int s;

	if ( elf->scp == NULL )
	{
		return;
	}
	for ( s = 1; s < elf->ehp->e_shnum; ++s )
	{
		if ( elf->scp[s]->shr.sh_type == SHT_REL )
			rebuild_an_relocation(elf->scp[s], gpvalue, ((Srx_gen_table *)(elf->optdata))->target);
	}
}

static int  check_irx12(elf_file *elf, int cause_irx1)
{
	int s;

	if ( elf->ehp->e_type != ET_SCE_IOPRELEXEC )
		return 0;
	for ( s = 1; s < elf->ehp->e_shnum; ++s )
	{
		if ( elf->scp[s]->shr.sh_type == SHT_REL && relocation_is_version2(elf->scp[s]) )
		{
			if ( cause_irx1 )
			{
				fprintf(stderr, "R_MIPS_LO16 without R_MIPS_HI16\n");
				return 1;
			}
			elf->ehp->e_type = ET_SCE_IOPRELEXEC2;
		}
	}
	return 0;
}

static void  setup_module_info(elf_file *elf, elf_section *modsect, srxfixup_const_char_ptr_t modulesymbol)
{
	unsigned int *section_data;
	int i;
	char *buf;
	char *name;
	int woff;
	size_t buflen;
	unsigned int *modnamep;
	unsigned int *modidatap;
	unsigned int modiaddr;
	elf_syment *syp;

	syp = search_global_symbol(modulesymbol, elf);
	if ( is_defined_symbol(syp) == 0 )
	{
		return;
	}
	modiaddr = get_symbol_value(syp, elf);
	modidatap = get_section_data(elf, modiaddr);
	section_data = get_section_data(elf, *modidatap);
	woff = (uintptr_t)section_data & 3;
	modnamep = (unsigned int *)((char *)section_data - woff);
	buflen = (strlen((const char *)section_data - woff + 4) + 15) & 0xFFFFFFFC;
	buf = (char *)malloc(buflen);
	memcpy(buf, modnamep, buflen);
	swapmemory(buf, (srxfixup_const_char_ptr_t)"l", buflen >> 2);
	name = &buf[woff];
	if ( modsect->shr.sh_type == SHT_SCE_IOPMOD )
	{
		Elf32_IopMod *iopmodp_1;
		Elf32_IopMod *iopmodp_2;

		iopmodp_1 = (Elf32_IopMod *)modsect->data;
		iopmodp_1->moduleinfo = modiaddr;
		iopmodp_1->moduleversion = *((uint16_t *)modidatap + 2);
		iopmodp_2 = (Elf32_IopMod *)realloc(iopmodp_1, strlen(name) + sizeof(Elf32_IopMod));
		strcpy(iopmodp_2->modulename, name);
		modsect->data = (uint8_t *)iopmodp_2;
		modsect->shr.sh_size = iopmod_size(iopmodp_2);
	}
	if ( modsect->shr.sh_type == SHT_SCE_EEMOD )
	{
		Elf32_EeMod *eemodp_1;
		Elf32_EeMod *eemodp_2;
		eemodp_1 = (Elf32_EeMod *)modsect->data;
		eemodp_1->moduleinfo = modiaddr;
		eemodp_1->moduleversion = *((uint16_t *)modidatap + 2);
		eemodp_2 = (Elf32_EeMod *)realloc(eemodp_1, strlen(name) + sizeof(Elf32_EeMod));
		strcpy(eemodp_2->modulename, name);
		modsect->data = (uint8_t *)eemodp_2;
		modsect->shr.sh_size = eemod_size(eemodp_2);
	}
	free(buf);
	if ( elf->php == NULL )
	{
		return;
	}
	for ( i = 0; i < elf->ehp->e_phnum; ++i )
	{
		if ( elf->php[i].scp && (modsect == *elf->php[i].scp) )
		{
			elf->php[i].phdr.p_filesz = modsect->shr.sh_size;
		}
	}
}

static void  rebuild_an_relocation(elf_section *relsect, unsigned int gpvalue, int target)
{
	int v4;
	uint32_t r_offset;
	uint32_t v19;
	unsigned int sh_size;
	unsigned int v21;
	int v22;
	int type;
	elf_rel *s;
	elf_rel *d;
	elf_rel *newtab;
	int newentrise;
	uint32_t symvalue;
	void * daddr_1;
	void * daddr_2;
	void * daddr_3;
	uint32_t data32_1;
	int data32_2;
	int datah;
	uint32_t data_1;
	int data_2;
	uint32_t data_33;
	uint16_t data_4;
	unsigned int data_5;
	unsigned int data_6;
	uint32_t data_7;
	uint32_t step;
	elf_syment **symp;
	elf_rel *rp;
	int next;
	int rmflag;
	signed int entrise;
	signed int j_1;
	int j_2;
	int j_3;
	signed int i_1;
	signed int i_2;

	entrise = relsect->shr.sh_size / relsect->shr.sh_entsize;
	rp = (elf_rel *)relsect->data;
	symp = (elf_syment **)relsect->link->data;
	rmflag = 0;
	i_1 = 0;
	while ( entrise > i_1 )
	{
		if ( relsect->info->shr.sh_size <= rp->rel.r_offset )
		{
			sh_size = relsect->info->shr.sh_size;
			r_offset = rp->rel.r_offset;
			fprintf(stderr, "Panic !! relocation #%d offset=0x%x overflow (section size=0x%x)\n", i_1, r_offset, sh_size);
			exit(1);
		}
		next = 1;
		daddr_1 = (void *)&relsect->info->data[rp->rel.r_offset];
		symvalue = 0;
		if ( rp->symptr->sym.st_shndx )
			symvalue = rp->symptr->sym.st_value;
		v4 = 0;
		if ( (rp->symptr->sym.st_shndx && rp->symptr->sym.st_shndx <= 0xFEFFu) || rp->symptr->sym.st_shndx == SHN_RADDR )
			v4 = 1;
		switch ( rp->type )
		{
			case R_MIPS_NONE:
				rmflag = 1;
				break;
			case R_MIPS_16:
				data_1 = symvalue + (int16_t)*(uint32_t *)daddr_1;
				if ( (uint16_t)(data_1 >> 16) && (uint16_t)(data_1 >> 16) != 0xFFFF )
				{
					fprintf(stderr, "REFHALF data overflow\n");
					exit(1);
				}
				*(uint32_t *)daddr_1 &= 0xFFFF0000;
				*(uint32_t *)daddr_1 |= (uint16_t)data_1;
				if ( !v4 )
				{
					rp->type = R_MIPS_NONE;
					rmflag = 1;
				}
				break;
			case R_MIPS_32:
				*(uint32_t *)daddr_1 += symvalue;
				if ( !v4 )
				{
					rp->type = R_MIPS_NONE;
					rmflag = 1;
				}
				break;
			case R_MIPS_26:
				data_2 = *(uint32_t *)daddr_1;
				if ( rp->symptr->bind != STB_LOCAL )
					data_33 = data_2 << 6 >> 4;
				else
					data_33 = ((relsect->info->shr.sh_addr + rp->rel.r_offset) & 0xF0000000) | (4 * (data_2 & 0x3FFFFFF));
				*(uint32_t *)daddr_1 &= 0xFC000000;
				*(uint32_t *)daddr_1 |= (16 * (symvalue + data_33)) >> 6;
				if ( !v4 )
				{
					rp->type = R_MIPS_NONE;
					rmflag = 1;
				}
				break;
			case R_MIPS_HI16:
				datah = *(uint32_t *)daddr_1 << 16;
				for ( j_1 = i_1 + 1; entrise > j_1 && rp[next].type == R_MIPS_HI16; ++j_1 )
				{
					if ( rp[next].symptr != rp->symptr )
					{
						fprintf(stderr, "R_MIPS_HI16 without R_MIPS_LO16\n");
						exit(1);
					}
					if ( relsect->info->shr.sh_size <= rp[next].rel.r_offset )
					{
						v21 = relsect->info->shr.sh_size;
						v19 = rp[next].rel.r_offset;
						fprintf(stderr, "Panic !! relocation #%d offset=0x%x overflow (section size=0x%x)\n", i_1 + next, v19, v21);
						exit(1);
					}
					daddr_1 = (void *)&relsect->info->data[rp[next].rel.r_offset];
					if ( datah != *(uint32_t *)daddr_1 << 16 )
					{
						fprintf(stderr, "R_MIPS_HI16s not same offsets\n");
						exit(1);
					}
					++next;
				}
				if ( j_1 == entrise + 1 || rp[next].type != R_MIPS_LO16 || rp[next].symptr != rp->symptr )
				{
					fprintf(stderr, "R_MIPS_HI16 without R_MIPS_LO16\n");
					exit(1);
				}
				data32_1 = symvalue + (int16_t)*(uint32_t *)&relsect->info->data[rp[next].rel.r_offset] + datah;
				if ( next == 1 )
				{
					*(uint32_t *)daddr_1 &= 0xFFFF0000;
					*(uint32_t *)daddr_1 |= (uint16_t)(((data32_1 >> 15) + 1) >> 1);
					if ( !v4 )
					{
						rp->type = R_MIPS_NONE;
						rmflag = 1;
					}
				}
				else if ( v4 )
				{
					for ( j_2 = 0; next > j_2; ++j_2 )
					{
						daddr_2 = (void *)&relsect->info->data[rp[j_2].rel.r_offset];
						*(uint32_t *)daddr_2 &= 0xFFFF0000;
						if ( j_2 < next - 1 )
						{
							step = rp[j_2 + 1].rel.r_offset - rp[j_2].rel.r_offset;
							if ( step >> 18 && step >> 18 != 0x3FFF )
							{
								fprintf(stderr, "R_MIPS_HI16s too long distance\n");
								exit(1);
							}
							*(uint32_t *)daddr_2 |= (uint16_t)(step >> 2);
						}
						rp[j_2].type = R_MIPS_NONE;
						--rp[j_2].symptr->refcount;
						rp[j_2].symptr = *symp;
					}
					rp->type = R_MIPSSCE_MHI16;
					rp->rel.r_offset += relsect->info->shr.sh_addr;
					rp[1].type = R_MIPSSCE_ADDEND;
					rp[1].rel.r_offset = data32_1;
					rmflag = 1;
					rp += next;
					i_1 += next;
					next = 0;
				}
				else
				{
					data32_2 = (uint16_t)(((data32_1 >> 15) + 1) >> 1);
					for ( j_3 = 0; next > j_3; ++j_3 )
					{
						daddr_3 = (void *)&relsect->info->data[rp[j_3].rel.r_offset];
						*(uint32_t *)daddr_3 &= 0xFFFF0000;
						*(uint32_t *)daddr_3 |= data32_2;
						rp[j_3].type = R_MIPS_NONE;
					}
					rmflag = 1;
				}
				break;
			case R_MIPS_LO16:
				data_4 = symvalue + *(uint32_t *)daddr_1;
				*(uint32_t *)daddr_1 &= 0xFFFF0000;
				*(uint32_t *)daddr_1 |= data_4;
				if ( !v4 )
				{
					rp->type = R_MIPS_NONE;
					rmflag = 1;
				}
				break;
			case R_MIPS_GPREL16:
				data_5 = (int16_t)*(uint32_t *)daddr_1;
				if ( rp->symptr->type == STT_SECTION )
				{
					data_5 += ((Sect_org_data *)(rp->symptr->shptr->optdata))->org_gp_value + symvalue - ((Sect_org_data *)(rp->symptr->shptr->optdata))->org_addr - gpvalue;
				}
				else if ( rp->symptr->bind == STB_GLOBAL || (rp->symptr->bind == STB_WEAK && rp->symptr->sym.st_shndx) )
				{
					data_5 += symvalue - gpvalue;
				}
				else if ( rp->symptr->bind != STB_WEAK || rp->symptr->sym.st_shndx )
				{
					fprintf(stderr, "R_MIPS_GPREL16 unknown case abort\n");
					exit(1);
				}
				if ( (uint16_t)(data_5 >> 16) && (uint16_t)(data_5 >> 16) != 0xFFFF )
				{
					fprintf(stderr, "R_MIPS_GPREL16 data overflow\n");
					exit(1);
				}
				*(uint32_t *)daddr_1 &= 0xFFFF0000;
				*(uint32_t *)daddr_1 |= (uint16_t)data_5;
				rp->type = R_MIPS_NONE;
				rmflag = 1;
				break;
			case R_MIPS_LITERAL:
				if ( rp->symptr->type != STT_SECTION )
				{
					fprintf(stderr, "R_MIPS_LITERAL unknown case abort\n");
					exit(1);
				}
				data_6 = ((Sect_org_data *)(rp->symptr->shptr->optdata))->org_gp_value
					+ symvalue
					- ((Sect_org_data *)(rp->symptr->shptr->optdata))->org_addr
					- gpvalue
					+ (int16_t)*(uint32_t *)daddr_1;
				if ( (uint16_t)(data_6 >> 16) && (uint16_t)(data_6 >> 16) != 0xFFFF )
				{
					fprintf(stderr, "R_MIPS_LITERAL data overflow\n");
					exit(1);
				}
				*(uint32_t *)daddr_1 &= 0xFFFF0000;
				*(uint32_t *)daddr_1 |= (uint16_t)data_6;
				rp->type = R_MIPS_NONE;
				rmflag = 1;
				break;
			case R_MIPS_DVP_27_S4:
				if ( target != 2 )
				{
					fprintf(stderr, "R_MIPS_DVP_27_S4 can use only for EE.\n");
					exit(1);
				}
				data_7 = symvalue + (*(uint32_t *)daddr_1 & 0x7FFFFFF0);
				*(uint32_t *)daddr_1 &= 0x8000000F;
				*(uint32_t *)daddr_1 |= data_7 & 0x7FFFFFF0;
				if ( !v4 )
				{
					rp->type = R_MIPS_NONE;
					rmflag = 1;
				}
				break;
			case R_MIPS_REL32:
			case R_MIPS_GOT16:
			case R_MIPS_PC16:
			case R_MIPS_CALL16:
			case R_MIPS_GPREL32:
			case R_MIPS_GOTHI16:
			case R_MIPS_GOTLO16:
			case R_MIPS_CALLHI16:
			case R_MIPS_CALLLO16:
				v22 = rp->type;
				fprintf(stderr, "unacceptable relocation type: 0x%x\n", v22);
				exit(1);
				return;
			default:
				type = rp->type;
				fprintf(stderr, "unknown relocation type: 0x%x\n", type);
				exit(1);
				return;
		}
		while ( next > 0 )
		{
			rp->rel.r_offset += relsect->info->shr.sh_addr;
			--rp->symptr->refcount;
			rp->symptr = *symp;
			++i_1;
			++rp;
			--next;
		}
	}
	if ( rmflag > 0 )
	{
		newtab = (elf_rel *)calloc(entrise, sizeof(elf_rel));
		d = newtab;
		s = (elf_rel *)relsect->data;
		i_2 = 0;
		newentrise = 0;
		while ( entrise > i_2 )
		{
			if ( s->type )
			{
				memcpy(d++, s, sizeof(elf_rel));
				++newentrise;
			}
			++i_2;
			++s;
		}
		free(relsect->data);
		relsect->data = (uint8_t *)newtab;
		relsect->shr.sh_size = relsect->shr.sh_entsize * newentrise;
	}
}

static int  iopmod_size(Elf32_IopMod *modinfo)
{
	return strlen(modinfo->modulename) + 27;
}

static int  eemod_size(Elf32_EeMod *modinfo)
{
	return strlen(modinfo->modulename) + 43;
}

int  relocation_is_version2(elf_section *relsect)
{
	elf_rel *rp;
	signed int entrise;
	signed int i;

	entrise = relsect->shr.sh_size / relsect->shr.sh_entsize;
	rp = (elf_rel *)relsect->data;
	for ( i = 0; entrise > i; ++i )
	{
		if ( rp->type == R_MIPS_LO16 )
			return 1;
		if ( rp->type == R_MIPS_HI16 )
		{
			if ( i == entrise + 1 || rp[1].type != R_MIPS_LO16 || rp[1].symptr != rp->symptr )
				return 1;
			++rp;
			++i;
		}
		if ( rp->type == R_MIPSSCE_MHI16 || rp->type == R_MIPSSCE_ADDEND )
			return 1;
		++rp;
	}
	return 0;
}

void  dump_srx_gen_table(Srx_gen_table *tp)
{
	const char *v1;
	int v2;
	const char *name;
	int scnfpp_;
	int scnfp_;
	int v8;
	int scp_;
	int scp_a;
	int nsegment;
	int b;
	int i;
	CreateSymbolConf *csyms;
	PheaderInfo *phip;
	SectConf *sctp;
	const char ***scnfpp;
	SegConf *scnfp;
	elf_section **scp;
	char segsig[32];
	const char **strp;

	if ( tp == NULL )
	{
		return;
	}
	if ( tp->target == 1 )
	{
		v1 = "IOP";
	}
	else if ( tp->target == 2 )
	{
		v1 = "EE";
	}
	else
	{
		v1 = "??";
	}
	printf("===============\nTarget is %s(%d)\n", v1, tp->target);
	printf("Segment list\n");
	v8 = 0;
	for ( scnfp = tp->segment_list; scnfp->name; ++scnfp )
	{
		printf("  %2d:segment %s\n", v8, scnfp->name);
		printf(
			"      addr,size=0x%x,0x%x  bitid,nsect= 0x%x,%d\n      ",
			scnfp->addr,
			scnfp->size,
			scnfp->bitid,
			scnfp->nsect);
		if ( scnfp->sect_name_patterns )
		{
			for ( strp = (const char **)scnfp->sect_name_patterns; *strp; ++strp )
				printf("%s ", *strp);
			printf("\n");
		}
		if ( scnfp->empty_section )
			printf("        Auto add section: %s\n", scnfp->empty_section->name);
		if ( scnfp->scp )
		{
			for ( scp = scnfp->scp; *scp; ++scp )
				printf("        %p: %s\n", *scp, (*scp)->name);
		}
		segsig[v8++] = *scnfp->name;
	}
	scnfpp_ = v8;
	printf("\nProgram header order\n");
	scp_ = 0;
	for ( phip = tp->program_header_order; phip->sw; ++phip )
	{
		if ( phip->sw == 1 )
		{
			printf("  %2d: section %s\n", scp_, phip->d.section_name);
		}
		else if ( phip->sw == 2 )
		{
			printf("  %2d: Segments ", scp_);
			for ( scnfpp = (const char ***)phip->d.section_name; *scnfpp; ++scnfpp )
				printf("%s ", **scnfpp);
			printf("\n");
		}
		++scp_;
	}
	printf("\nRemove section list\n");
	scp_a = 0;
	for ( strp = (const char **)tp->removesection_list; *strp; printf("  %2d: %s\n", scp_a++, *strp++) )
		;
	printf("\nSection table order\n");
	nsegment = 0;
	for ( strp = (const char **)tp->section_table_order; *strp; printf("  %2d: %s\n", nsegment++, *strp++) )
		;
	printf("\nFile layout order\n");
	b = 0;
	for ( strp = (const char **)tp->file_layout_order; *strp; printf("  %2d: %s\n", b++, *strp++) )
		;
	printf("\nmemory layout order\n");
	i = 0;
	for ( sctp = tp->section_list; sctp->sect_name_pattern; ++sctp )
	{
		printf("  %2d: [", i);
		for ( scnfp_ = 0; scnfpp_ > scnfp_; ++scnfp_ )
		{
			if ( (sctp->flag & (1 << scnfp_)) != 0 )
				v2 = (unsigned char)(segsig[scnfp_]);
			else
				v2 = 46;
			printf("%c", v2);
		}
		printf("] %s", sctp->sect_name_pattern);
		if ( sctp->secttype )
			printf("\t: Auto create type=%x flag=%x", sctp->secttype, sctp->sectflag);
		printf("\n");
		++i;
	}
	printf("\nReserved symbols\n");
	for ( csyms = tp->create_symbols; csyms->name; ++csyms )
	{
		const char *sectname;

		sectname = csyms->sectname;
		if ( !sectname )
			sectname = "-";
		if ( csyms->segment )
			name = csyms->segment->name;
		else
			name = "-";
		printf(
			"   %-8s: bind=%d, type=%d, shindex=0x%04x, seflag=%d, seg=%s, sect=%s\n",
			csyms->name,
			csyms->bind,
			csyms->type,
			csyms->shindex,
			csyms->seflag,
			name,
			sectname);
	}
	printf("\n\n");
}


















































