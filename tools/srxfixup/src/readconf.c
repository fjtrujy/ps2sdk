
#include "all_include.h"

typedef struct _lowtoken 
{
	srxfixup_const_char_ptr_t str; 
	int line; 
	int col;
} LowToken;
enum TokenCode 
{
	TC_NULL = 0, 
	TC_STRING = 1, 
	TC_VECTOR = 2, 
	TC_IOP = 3, 
	TC_EE = 4, 
	TC_Define = 5, 
	TC_Segments_name = 6, 
	TC_Memory_segment = 7, 
	TC_remove = 8, 
	TC_Program_header_order = 9, 
	TC_Program_header_data = 10, 
	TC_Segment_data = 11, 
	TC_segment = 12, 
	TC_createinfo = 13, 
	TC_Section_header_table = 14, 
	TC_CreateSymbols = 15, 
	TC_MAX_NUMBER = 16
};
typedef struct _TokenTree 
{
	enum TokenCode tkcode; 
	union __anon_struct_57
	{
		struct _TokenTree *subtree; 
		LowToken *lowtoken;
	} value;
} TokenTree;
struct fstrbuf 
{
	int line; 
	int col; 
	srxfixup_const_char_ptr_t cp; 
	srxfixup_const_char_ptr_t ep; 
	char buf[1];
};

static int  bgetc(struct fstrbuf *fb);
static void  bungetc(struct fstrbuf *fb);
static int  skipsp(struct fstrbuf *fb);
static int  skip_to_eol(struct fstrbuf *fb);
static int  gettoken(char **strbuf, struct fstrbuf *fb);
static void  split_conf(LowToken *result, char *strbuf, struct fstrbuf *fb);
static TokenTree * make_conf_vector(LowToken **lowtokens);
static TokenTree * make_conf_tree(LowToken *lowtokens);
static int  get_vector_len(TokenTree *ttp);
static int  get_stringvector_len(srxfixup_const_char_ptr_t *str);
static srxfixup_const_char_ptr_t * add_stringvector(srxfixup_const_char_ptr_t *str, srxfixup_const_char_ptr_t newstr);
static int  setup_reserved_symbol_table(CreateSymbolConf *result, TokenTree *ttp, Srx_gen_table *conf);
static int  gen_define(TokenTree *ttp, Srx_gen_table *result);
static void  get_section_type_flag(TokenTree *ttp, int *rtype, int *rflag);
static elf_section * make_empty_section(srxfixup_const_char_ptr_t name, int type, int flag);
static Srx_gen_table * make_srx_gen_table(TokenTree *tokentree);
static void  check_change_bit(int oldbit, int newbit, int *up, int *down);
static int  check_srx_gen_table(Srx_gen_table *tp);

Srx_gen_table * read_conf(srxfixup_const_char_ptr_t indata, srxfixup_const_char_ptr_t infile, int dumpopt)
{
	LowToken *lowtokens;
	struct fstrbuf *fbuf;
	int fsize;
	FILE *fp;

	fp = 0;
	if ( !indata )
	{
		fprintf(stderr, "internal error read_conf()\n");
		return 0;
	}
	if ( infile )
	{
		fp = fopen(infile, "r");
		if ( !fp )
		{
			fprintf(stderr, "\"%s\" can't open\n", infile);
			return 0;
		}
		fseek(fp, 0, 2);
		fsize = ftell(fp) + 4;
		fseek(fp, 0, 0);
	}
	else
	{
		fsize = strlen(indata);
	}
	lowtokens = (LowToken *)calloc(((sizeof(LowToken) + 2) * (fsize + 1) + (sizeof(LowToken) - 1)) / sizeof(LowToken), sizeof(LowToken));
	fbuf = (struct fstrbuf *)malloc(fsize + sizeof(struct fstrbuf) + 1);
	fbuf->cp = fbuf->buf;
	fbuf->line = 1;
	fbuf->col = 1;
	if ( infile )
	{
		fbuf->ep = &fbuf->cp[fread(fbuf->buf, 1u, fsize, fp)];
		fclose(fp);
	}
	else
	{
		strcpy(fbuf->buf, indata);
		fbuf->ep = &fbuf->cp[fsize];
	}
	if ( dumpopt == 1 )
	{
		while ( 1 )
		{
			int ch_;

			ch_ = bgetc(fbuf);
			if ( ch_ == -1 )
				break;
			fputc(ch_, stdout);
		}
		return 0;
	}
	else
	{
		Srx_gen_table *srx_gen_table;

		split_conf(lowtokens, (char *)&lowtokens[fsize + 1], fbuf);
		free(fbuf);
		srx_gen_table = make_srx_gen_table(make_conf_tree(lowtokens));
		if ( check_srx_gen_table(srx_gen_table) )
			return 0;
		else
			return srx_gen_table;
	}
}

static int  bgetc(struct fstrbuf *fb)
{
	if ( fb->ep <= fb->cp )
		return -1;
	if ( *fb->cp == '\n' )
	{
		++fb->line;
		fb->col = 0;
	}
	else
	{
		++fb->col;
	}
	return *fb->cp++;
}

static void  bungetc(struct fstrbuf *fb)
{
	if ( fb->cp > fb->buf )
	{
		--fb->cp;
		--fb->col;
		if ( *fb->cp == '\n' )
			--fb->line;
	}
}

static int  skipsp(struct fstrbuf *fb)
{
	int ch_;

	do
		ch_ = bgetc(fb);
	while ( ch_ != -1 && isspace(ch_) != 0 );
	if ( ch_ != -1 )
		bungetc(fb);
	return ch_ != -1;
}

static int  skip_to_eol(struct fstrbuf *fb)
{
	int ch_;

	do
		ch_ = bgetc(fb);
	while ( ch_ != -1 && ch_ != '\n' && ch_ != '\r' );
	return ch_ != -1;
}

static int  gettoken(char **strbuf, struct fstrbuf *fb)
{
	int ch_;
	char *cp;

	for ( cp = *strbuf; ; *cp = 0 )
	{
		ch_ = bgetc(fb);
		if ( ch_ == -1 || (ch_ != '.' && ch_ != '_' && ch_ != '*' && isalnum(ch_) == 0) )
			break;
		*cp++ = ch_;
	}
	if ( ch_ != -1 )
		bungetc(fb);
	*strbuf = cp + 1;
	return ch_ != -1;
}

static void  split_conf(LowToken *result, char *strbuf, struct fstrbuf *fb)
{
	char *cp;

	cp = strbuf;
	while ( skipsp(fb) )
	{
		char cuchar;

		cuchar = bgetc(fb);
		if ( cuchar == '@'
			|| cuchar == '.'
			|| cuchar == '_'
			|| cuchar == '*'
			|| isalnum(cuchar) != 0 )
		{
			result->str = cp;
			result->line = fb->line;
			result->col = fb->col;
			++result;
			result->str = 0;
			*cp++ = cuchar;
			gettoken(&cp, fb);
		}
		else if ( cuchar == '#' )
		{
			skip_to_eol(fb);
		}
		else if ( isprint(cuchar) != 0 )
		{
			result->str = cp;
			result->line = fb->line;
			result->col = fb->col;
			++result;
			result->str = 0;
			*cp++ = cuchar;
			*cp++ = 0;
		}
	}
}

struct _keyword_table
{
	int code;
	srxfixup_const_char_ptr_t name;
} keyword_table[] =
{
	{ 3, "IOP" },
	{ 4, "EE" },
	{ 5, "Define" },
	{ 6, "Segments_name" },
	{ 7, "Memory_segment" },
	{ 8, "remove" },
	{ 9, "Program_header_order" },
	{ 10, "Program_header_data" },
	{ 11, "Segment_data" },
	{ 12, "segment" },
	{ 13, "createinfo" },
	{ 14, "Section_header_table" },
	{ 15, "CreateSymbols" },
	{ -1, NULL }
};

static TokenTree * make_conf_vector(LowToken **lowtokens)
{
	TokenTree *conf_vector;
	LowToken v6;
	LowToken v7;
	int kt_;
	int sltp_;
	struct _keyword_table *kt;
	LowToken *sltp;
	LowToken *ltp;
	int entries;
	TokenTree *v14;

	entries = 0;
	ltp = *lowtokens;
	v14 = (TokenTree *)calloc(1u, sizeof(TokenTree));
	v14->tkcode = TC_NULL;
	while ( ltp->str && strcmp(ltp->str, "}") != 0 )
	{
		{
			TokenTree *realloc_tmp = (TokenTree *)realloc(v14, (entries + 2) * sizeof(TokenTree));
			if (realloc_tmp == NULL)
			{
				fprintf(stderr, "Failure to allocate token tree\n");
				exit(1);
			}
			v14 = realloc_tmp;
		}
		
		if ( !strcmp(ltp->str, "{") )
		{
			sltp = ltp++;
			v14[entries].tkcode = TC_VECTOR;
			conf_vector = make_conf_vector(&ltp);
			v14[entries].value.subtree = conf_vector;
			if ( !ltp->str || strcmp(ltp->str, "}") != 0 )
			{
				sltp_ = sltp->col;
				kt_ = sltp->line;
				fprintf(stderr, "make_conf_vector(): missing '}' line:%d col=%d\n", kt_, sltp_);
				exit(1);
			}
			++ltp;
		}
		else
		{
			if ( *ltp->str != '@'
				&& *ltp->str != '.'
				&& *ltp->str != '_'
				&& *ltp->str != '*'
				&& isalnum(*ltp->str) == 0 )
			{
				v7 = *ltp;
				fprintf(stderr, "make_conf_vector(): unexcepted data '%s' line:%d col=%d\n", v7.str, v7.line, v7.col);
				exit(1);
			}
			if ( *ltp->str == '@' )
			{
				for ( kt = keyword_table; kt->code >= 0 && strcmp(kt->name, (const char *)ltp->str + 1) != 0; ++kt )
					;
				if ( kt->code < 0 )
				{
					v6 = *ltp;
					fprintf(stderr, "make_conf_vector(): unknown keyword '%s' line:%d col=%d\n", v6.str, v6.line, v6.col);
					exit(1);
				}
				v14[entries].tkcode = kt->code;
			}
			else
			{
				v14[entries].tkcode = TC_STRING;
			}
			v14[entries].value.subtree = (TokenTree *)ltp++;
		}
		v14[++entries].tkcode = TC_NULL;
	}
	*lowtokens = ltp;
	return v14;
}

static TokenTree * make_conf_tree(LowToken *lowtokens)
{
	LowToken v3;
	TokenTree *v4;

	v4 = (TokenTree *)calloc(1u, sizeof(TokenTree));
	v4->tkcode = TC_VECTOR;
	v4->value.subtree = make_conf_vector(&lowtokens);
	if ( lowtokens->str )
	{
		v3 = *lowtokens;
		fprintf(stderr, "make_conf_tree(): unexcepted data '%s' line:%d col=%d\n", v3.str, v3.line, v3.col);
		exit(1);
	}
	return v4;
}

static int  get_vector_len(TokenTree *ttp)
{
	int v2;

	v2 = 0;
	while ( ttp->tkcode )
	{
		++v2;
		++ttp;
	}
	return v2;
}

static int  get_stringvector_len(srxfixup_const_char_ptr_t *str)
{
	int v2;

	v2 = 0;
	while ( *str )
	{
		++v2;
		++str;
	}
	return v2;
}

static srxfixup_const_char_ptr_t * add_stringvector(srxfixup_const_char_ptr_t *str, srxfixup_const_char_ptr_t newstr)
{
	int stringvector_len;
	srxfixup_const_char_ptr_t *result;
	int nstr;

	stringvector_len = get_stringvector_len(str);
	nstr = stringvector_len + 1;
	result = (srxfixup_const_char_ptr_t *)realloc(str, (stringvector_len + 2) * sizeof(srxfixup_const_char_ptr_t));
	result[nstr - 1] = newstr;
	result[nstr] = 0;
	return result;
}

static int  setup_reserved_symbol_table(CreateSymbolConf *result, TokenTree *ttp, Srx_gen_table *conf)
{
	if ( !strcmp("GLOBAL", ttp[1].value.lowtoken->str) )
	{
		result->bind = 1;
	}
	else if ( !strcmp("LOCAL", ttp[1].value.lowtoken->str) )
	{
		result->bind = 0;
	}
	else
	{
		if ( strcmp("WEAK", ttp[1].value.lowtoken->str) != 0 )
		{
			fprintf(stderr, "Unsupported bind '%s' for '%s'\n", ttp[1].value.lowtoken->str, ttp->value.lowtoken->str);
			return 1;
		}
		result->bind = 2;
	}
	if ( strcmp("OBJECT", ttp[2].value.lowtoken->str) != 0 )
	{
		fprintf(stderr, "Unsupported type '%s' for '%s'\n", ttp[2].value.lowtoken->str, ttp->value.lowtoken->str);
		return 1;
	}
	result->type = 1;
	result->segment = lookup_segment(conf, ttp[3].value.lowtoken->str, 0);
	if ( !result->segment && ttp[3].value.lowtoken->str[0] == '.' )
	{
		result->sectname = ttp[3].value.lowtoken->str;
	}
	else
	{
		result->sectname = 0;
		if ( !result->segment )
		{
			fprintf(stderr, "Unknown segment '%s' for '%s'\n", ttp[3].value.lowtoken->str, ttp->value.lowtoken->str);
			return 1;
		}
	}
	if ( !strcmp("SHN_RADDR", ttp[4].value.lowtoken->str) )
	{
		result->shindex = 65311;
	}
	else
	{
		if ( strcmp("0", ttp[4].value.lowtoken->str) != 0 )
		{
			fprintf(stderr, "Unknown shindex '%s' for '%s'\n", ttp[4].value.lowtoken->str, ttp->value.lowtoken->str);
			return 1;
		}
		result->shindex = 0;
	}
	if ( !strcmp("start", ttp[5].value.lowtoken->str) )
	{
		result->seflag = 0;
	}
	else if ( !strcmp("end", ttp[5].value.lowtoken->str) )
	{
		result->seflag = 1;
	}
	else
	{
		if ( strcmp("gpbase", ttp[5].value.lowtoken->str) != 0 )
		{
			fprintf(stderr, "Unknown base '%s' for '%s'\n", ttp[5].value.lowtoken->str, ttp->value.lowtoken->str);
			return 1;
		}
		result->seflag = 2;
	}
	result->name = ttp->value.lowtoken->str;
	return 0;
}

static int  gen_define(TokenTree *ttp, Srx_gen_table *result)
{
	enum TokenCode v4;
	enum TokenCode v5;
	PheaderInfo *phrlist;
	SegConf *segp;
	SegConf *seglist;
	TokenTree *subarg;
	TokenTree *arg;
	int n;
	int nseg;
	int m;
	int j;
	int k;
	int i;
	int entries_1;
	int entries_2;
	int entries_3;
	int entries_4;

	while ( ttp->tkcode )
	{
		if ( ttp[1].tkcode != TC_VECTOR )
		{
			fprintf(stderr, "argument not found for '%s' line:%d col=%d\n", ttp->value.lowtoken->str, ttp->value.lowtoken->line, ttp->value.lowtoken->col);
			return 1;
		}
		arg = ttp[1].value.subtree;
		v4 = ttp->tkcode;
		if ( ttp->tkcode == TC_Memory_segment )
		{
			entries_4 = get_vector_len(arg);
			for ( i = 0; entries_4 > i; ++i )
			{
				segp = lookup_segment(result, arg[i].value.lowtoken->str, 1);
				if ( !segp )
					return 1;
				segp->bitid = 1 << (segp - result->segment_list);
			}
		}
		else if ( (unsigned int)v4 > TC_Memory_segment )
		{
			if ( v4 == TC_Program_header_order )
			{
				entries_2 = get_vector_len(arg);
				phrlist = (PheaderInfo *)calloc(entries_2 + 1, sizeof(PheaderInfo));
				result->program_header_order = phrlist;
				for ( j = 0; entries_2 > j; ++j )
				{
					v5 = arg[j].tkcode;
					if ( v5 == TC_STRING )
					{
						phrlist[j].sw = 1;
						phrlist[j].d.section_name = arg[j].value.lowtoken->str;
					}
					else if ( v5 == TC_VECTOR )
					{
						subarg = arg[j].value.subtree;
						nseg = get_vector_len(subarg);
						phrlist[j].sw = 2;
						phrlist[j].d.segment_list = (SegConf **)calloc(nseg + 1, sizeof(SegConf *));
						for ( n = 0; nseg > n; ++n )
						{
							phrlist[j].d.segment_list[n] = lookup_segment(
																		 result,
																		 subarg[n].value.lowtoken->str,
																		 1);
							if ( !phrlist[j].d.segment_list[n] )
								return 1;
						}
					}
				}
			}
			else
			{
				if ( v4 != TC_CreateSymbols )
				{
					fprintf(stderr, "unexcepted data '%s' line:%d col=%d\n", ttp->value.lowtoken->str, ttp->value.lowtoken->line, ttp->value.lowtoken->col);
					return 1;
				}
				entries_3 = get_vector_len(arg);
				result->create_symbols = (CreateSymbolConf *)calloc(entries_3 + 1, sizeof(CreateSymbolConf));
				for ( k = 0; entries_3 > k; ++k )
				{
					if ( arg[k].tkcode != TC_VECTOR || get_vector_len(arg[k].value.subtree) != 6 )
					{
						fprintf(stderr, "unexcepted data in @CreateSymbols\n");
						return 1;
					}
					if ( setup_reserved_symbol_table(&result->create_symbols[k], arg[k].value.subtree, result) )
						return 1;
				}
			}
		}
		else
		{
			if ( v4 != TC_Segments_name )
			{
				fprintf(stderr, "unexcepted data '%s' line:%d col=%d\n", ttp->value.lowtoken->str, ttp->value.lowtoken->line, ttp->value.lowtoken->col);
				return 1;
			}
			entries_1 = get_vector_len(arg);
			seglist = (SegConf *)calloc(entries_1 + 1, sizeof(SegConf));
			result->segment_list = seglist;
			for ( m = 0; entries_1 > m; ++m )
			{
				seglist[m].name = arg[m].value.lowtoken->str;
				seglist[m].sect_name_patterns = (srxfixup_const_char_ptr_t *)calloc(1u, sizeof(srxfixup_const_char_ptr_t));
			}
		}
		ttp += 2;
	}
	return 0;
}

static void  get_section_type_flag(TokenTree *ttp, int *rtype, int *rflag)
{
	int flag;
	int type;

	type = 0;
	flag = 0;
	while ( ttp->tkcode )
	{
		const char *info;

		info = ttp->value.lowtoken->str;
		if ( !strcmp(info, "PROGBITS") )
		{
			type = 1;
		}
		else if ( !strcmp(info, "NOBITS") )
		{
			type = 8;
		}
		else if ( !strcmp(info, "ALLOC") )
		{
			LOBYTE(flag) = flag | 2;
		}
		else if ( !strcmp(info, "EXECINSTR") )
		{
			LOBYTE(flag) = flag | 4;
		}
		else if ( !strcmp(info, "WRITE") )
		{
			LOBYTE(flag) = flag | 1;
		}
		++ttp;
	}
	*rtype = type;
	*rflag = flag;
}

static elf_section * make_empty_section(srxfixup_const_char_ptr_t name, int type, int flag)
{
	elf_section *result;

	result = (elf_section *)calloc(1u, sizeof(elf_section));
	result->name = strdup(name);
	result->shr.sh_type = type;
	result->shr.sh_flags = flag;
	result->shr.sh_size = 0;
	result->shr.sh_addralign = 4;
	result->shr.sh_entsize = 0;
	return result;
}

static Srx_gen_table * make_srx_gen_table(TokenTree *tokentree)
{
	SegConf *seg_1;
	SegConf *seg_2;
	SegConf *seg_3;
	TokenTree *nttp;
	TokenTree *ttp2_1;
	TokenTree *ttp2_2;
	TokenTree *ttp1;
	TokenTree *ttp;
	Srx_gen_table *result;
	int sectflag;
	int secttype;
	unsigned int bitid;
	int nsect;
	srxfixup_const_char_ptr_t *strp;
	const char *str;
	char *str2;

	result = (Srx_gen_table *)calloc(1u, sizeof(Srx_gen_table));
	if ( tokentree->tkcode != TC_VECTOR )
	{
		fprintf(stderr, "Internal error:make_srx_gen_table();\n");
		free(result);
		return 0;
	}
	ttp = tokentree->value.subtree;
	nsect = 0;
	result->section_table_order = (srxfixup_const_char_ptr_t *)calloc(1u, sizeof(srxfixup_const_char_ptr_t));
	result->file_layout_order = (srxfixup_const_char_ptr_t *)calloc(1u, sizeof(srxfixup_const_char_ptr_t));
	result->removesection_list = (srxfixup_const_char_ptr_t *)calloc(1u, sizeof(srxfixup_const_char_ptr_t));
	result->section_list = (SectConf *)calloc(1u, sizeof(SectConf));
	while ( ttp->tkcode )
	{
		if ( ttp[1].tkcode == TC_VECTOR )
		{
			ttp1 = ttp[1].value.subtree;
			nttp = ttp + 2;
		}
		else
		{
			ttp1 = 0;
			nttp = ttp + 1;
		}
		switch ( ttp->tkcode )
		{
			case TC_STRING:
				str = ttp->value.lowtoken->str;
				result->section_table_order = add_stringvector(result->section_table_order, str);
				if ( !ttp1 )
				{
					result->file_layout_order = add_stringvector(result->file_layout_order, str);
					ttp = nttp;
					break;
				}
				if ( ttp1->tkcode == TC_remove )
				{
					result->removesection_list = add_stringvector(result->removesection_list, str);
					ttp = nttp;
					break;
				}
				if ( ttp1->tkcode == TC_segment && ttp1[1].tkcode == TC_VECTOR )
				{
					bitid = 0;
					for ( ttp2_1 = ttp1[1].value.subtree; ttp2_1->tkcode; ++ttp2_1 )
					{
						seg_1 = lookup_segment(result, ttp2_1->value.lowtoken->str, 1);
						if ( !seg_1 )
							return 0;
						seg_1->sect_name_patterns = add_stringvector(seg_1->sect_name_patterns, str);
						bitid |= seg_1->bitid;
					}
					if ( bitid )
					{
						result->section_list[nsect].sect_name_pattern = str;
						result->section_list[nsect++].flag = bitid;
						result->section_list = (SectConf *)realloc(result->section_list, (nsect + 1) * sizeof(SectConf));
						result->section_list[nsect].sect_name_pattern = 0;
						result->section_list[nsect].flag = 0;
						result->section_list[nsect].secttype = 0;
						result->section_list[nsect].sectflag = 0;
					}
					if ( ttp1[2].tkcode != TC_createinfo || ttp1[3].tkcode != TC_VECTOR )
					{
						ttp = nttp;
						break;
					}
					get_section_type_flag(ttp1[3].value.subtree, &secttype, &sectflag);
					if ( secttype && sectflag )
					{
						if ( bitid )
						{
							result->section_list[nsect - 1].secttype = secttype;
							result->section_list[nsect - 1].sectflag = sectflag;
						}
						seg_2 = NULL;
						for ( ttp2_2 = ttp1[1].value.subtree; ; ++ttp2_2 )
						{
							if ( ttp2_2->tkcode == TC_NULL )
							{
								ttp = nttp;
								break;
							}
							seg_2 = lookup_segment(result, ttp2_2->value.lowtoken->str, 1);
							if ( !seg_2 )
								break;
							if ( !seg_2->empty_section )
								seg_2->empty_section = make_empty_section(str, secttype, sectflag);
						}
						if ( !seg_2 )
						{
							return 0;
						}
					}
					else
					{
						fprintf(stderr, "Illegal @createinfo line:%d col=%d\n", ttp1->value.lowtoken->line, ttp1->value.lowtoken->col);
						return 0;
					}
				}
				else
				{
					fprintf(stderr, "unexcepted data '%s' line:%d col=%d\n", ttp1->value.lowtoken->str, ttp1->value.lowtoken->line, ttp1->value.lowtoken->col);
					return 0;
				}
			case TC_IOP:
				result->target = 1;
				ttp = nttp;
				break;
			case TC_EE:
				result->target = 2;
				ttp = nttp;
				break;
			case TC_Define:
				if ( !ttp1 )
				{
					fprintf(stderr, "argument not found for '%s' line:%d col=%d\n", ttp->value.lowtoken->str, ttp->value.lowtoken->line, ttp->value.lowtoken->col);
					return 0;
				}
				if ( !gen_define(ttp1, result) )
				{
					ttp = nttp;
					break;
				}
				return 0;
			case TC_Program_header_data:
				if ( !ttp1 || ttp1->tkcode != TC_STRING )
				{
					if ( ttp1 )
					{
						fprintf(stderr, "unexcepted data '%s' line:%d col=%d\n", ttp1->value.lowtoken->str, ttp1->value.lowtoken->line, ttp1->value.lowtoken->col);
					}
					else
					{
						fprintf(stderr, "%s missing '{ <n> }' line:%d col=%d\n", ttp->value.lowtoken->str, ttp->value.lowtoken->line, ttp->value.lowtoken->col);
					}
					return 0;
				}
				str2 = (char *)malloc(0x32u);
				sprintf(str2, "@Program_header_data %s", ttp1->value.lowtoken->str);
				result->file_layout_order = add_stringvector(result->file_layout_order, str2);
				ttp = nttp;
				break;
			case TC_Segment_data:
				while ( 2 )
				{
					if ( ttp1->tkcode == TC_NULL )
					{
						ttp = nttp;
						break;
					}
					seg_3 = lookup_segment(result, ttp1->value.lowtoken->str, 1);
					if ( seg_3 )
					{
						for ( strp = seg_3->sect_name_patterns; *strp; ++strp )
							result->file_layout_order = add_stringvector(result->file_layout_order, *strp);
						++ttp1;
						continue;
					}
					return 0;
				}
				break;
			case TC_Section_header_table:
				result->file_layout_order = add_stringvector(result->file_layout_order, "@Section_header_table");
				ttp = nttp;
				break;
			case TC_VECTOR:
				ttp = ttp->value.subtree;
			default:
				fprintf(stderr, "unexcepted data '%s' line:%d col=%d\n", ttp->value.lowtoken->str, ttp->value.lowtoken->line, ttp->value.lowtoken->col);
				return 0;
		}
	}
	if ( result->target == 1 || result->target == 2 )
		return result;
	fprintf(stderr, "@IOP or @EE not found error !\n");
	return 0;
}

static void  check_change_bit(int oldbit, int newbit, int *up, int *down)
{
	*up = ~oldbit & newbit & (newbit ^ oldbit);
	*down = ~newbit & oldbit & (newbit ^ oldbit);
}

static int  check_srx_gen_table(Srx_gen_table *tp)
{
	const char *name;
	const char *sect_name_pattern;
	SegConf *scnfp;
	SectConf *sctp;
	int nsegment;
	int b;
	int i;
	int error;
	int defbitid;
	int downdelta;
	int updelta;
	int oldbitid;

	nsegment = 0;
	for ( 
		scnfp = tp->segment_list;
		scnfp->name;
		++scnfp )
		++nsegment;
	defbitid = 0;
	oldbitid = 0;
	error = 0;
	i = 0;
	for ( sctp = tp->section_list; sctp->sect_name_pattern; ++sctp )
	{
		check_change_bit(oldbitid, sctp->flag, &updelta, &downdelta);
		if ( (defbitid & updelta) != 0 )
		{
			++error;
			for ( b = 0; nsegment > b; ++b )
			{
				if ( (sctp->flag & (1 << b)) != 0 )
				{
					sect_name_pattern = sctp->sect_name_pattern;
					name = tp->segment_list[b].name;
					fprintf(stderr, "Segment '%s' restart by section `%s`\n", name, sect_name_pattern);
					break;
				}
			}
		}
		oldbitid = sctp->flag;
		check_change_bit(oldbitid, sctp[1].flag, &updelta, &downdelta);
		defbitid |= downdelta;
		++i;
	}
	return error;
}















































