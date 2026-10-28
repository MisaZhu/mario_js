#include "mario.h"
#include <stdio.h>
#include <unistd.h>

static const char* inmstr_str(opr_code_t ins) {
	switch(ins) {
		case INSTR_NIL          : return "NIL";
		case INSTR_END          : return "END";
		case INSTR_OBJ          : return "OBJ";
		case INSTR_OBJ_END      : return "OBJE";
		case INSTR_MEMBER       : return "MEMBER";
		case INSTR_MEMBERN      : return "MEMBERN";
		case INSTR_POP          : return "POP";
		case INSTR_VAR          : return "VAR";
		case INSTR_SAFE_VAR     : return "SAFE_VAR";
		case INSTR_CONST        : return "CONST";
		case INSTR_INT          : return "INT";
		case INSTR_INT_S        : return "INTS";
		case INSTR_FLOAT        : return "FLOAT";
		case INSTR_INT64        : return "INT64";
		case INSTR_FLOAT64      : return "FLOAT64";
		case INSTR_STR          : return "STR";
		case INSTR_BIGINT       : return "BIGINT";
		case INSTR_ARRAY_AT     : return "ARRAT";
		case INSTR_ARRAY_AT_W   : return "ARRATW";
		case INSTR_ARRAY        : return "ARR";
		case INSTR_ARRAY_END    : return "ARRE";
		case INSTR_LOAD         : return "LOAD";
		case INSTR_LOADV        : return "LOADV";
		case INSTR_LOADW        : return "LOADW";
		case INSTR_LOAD_SAFE    : return "LOADS";
		case INSTR_LOADP        : return "LOADP";
		case INSTR_LOADPV       : return "LOADPV";
		case INSTR_FUNCLOC      : return "FUNCLOC";
		case INSTR_FUNCARGU     : return "FUNCARGU";
		case INSTR_STORE        : return "STORE";
		case INSTR_JMP          : return "JMP";
		case INSTR_NJMP         : return "NJMP";
		case INSTR_JMPB         : return "JMPB";
		case INSTR_NJMPB        : return "NJMPB";
		case INSTR_FUNC         : return "FUNC";
		case INSTR_FUNC_STC     : return "FUNC_STC";
		case INSTR_FUNC_GET     : return "FUNCGET";
		case INSTR_FUNC_SET     : return "FUNCSET";
		case INSTR_CLASS        : return "CLASS";
		case INSTR_CLASS_EXPR   : return "CLASSEXPR";
		case INSTR_CLASS_END    : return "CLASSE";
		case INSTR_EXTENDS      : return "EXTENDS";
		case INSTR_EXTENDS_V    : return "EXTENDSV";
		case INSTR_CALL         : return "CALL";
		case INSTR_CALLO        : return "CALLO";
		case INSTR_CALLX        : return "CALLX";
		case INSTR_NEWX          : return "NEWX";
		case INSTR_NEWX_SPREAD   : return "NEWX_SPREAD";
		case INSTR_CALLX_SPREAD : return "CALLX_SPREAD";
		case INSTR_TAG_RAW      : return "TAG_RAW";
		case INSTR_GETW         : return "GETW";
				case INSTR_WANCHOR      : return "WANCHOR";
				case INSTR_WTARGET      : return "WTARGET";
		case INSTR_NOT          : return "NOT";
		case INSTR_MULTI        : return "MULTI";
		case INSTR_DIV          : return "DIV";
		case INSTR_MOD          : return "MOD";
		case INSTR_PLUS         : return "PLUS";
		case INSTR_MINUS        : return "MINUS";
		case INSTR_NEG          : return "NEG";
		case INSTR_BNOT         : return "BNOT";
				case INSTR_BITANDEQ     : return "BITANDEQ";
				case INSTR_BITOREQ      : return "BITOREQ";
				case INSTR_BITXOREQ     : return "BITXOREQ";
				case INSTR_LSHIFTEQ     : return "LSHIFTEQ";
				case INSTR_RSHIFTEQ     : return "RSHIFTEQ";
		case INSTR_URSHIFTEQ    : return "URSHIFTEQ";
		case INSTR_SCOR         : return "SCOR";
		case INSTR_SCAND        : return "SCAND";
		case INSTR_PPLUS        : return "PPLUS";
		case INSTR_MMINUS       : return "MMINUS";
		case INSTR_PPLUS_PRE    : return "PPLUSP";
		case INSTR_MMINUS_PRE   : return "MMINUSP";
		case INSTR_LSHIFT       : return "LSHIFT";
		case INSTR_RSHIFT       : return "RSHIFT";
		case INSTR_URSHIFT      : return "URSHIFT";
		case INSTR_EQ           : return "EQ";
		case INSTR_NEQ          : return "NEQ";
		case INSTR_LEQ          : return "LEQ";
		case INSTR_GEQ          : return "GEQ";
		case INSTR_GRT          : return "GRT";
		case INSTR_LES          : return "LES";
		case INSTR_PLUSEQ       : return "PLUSEQ";
		case INSTR_MINUSEQ      : return "MINUSEQ";
		case INSTR_MULTIEQ      : return "MULTIEQ";
		case INSTR_DIVEQ        : return "DIVEQ";
		case INSTR_MODEQ        : return "MODEQ";
		case INSTR_AAND         : return "AAND";
		case INSTR_OOR          : return "OOR";
		case INSTR_OR           : return "OR";
		case INSTR_XOR          : return "XOR";
		case INSTR_AND          : return "AND";
		case INSTR_ASIGN        : return "ASIGN";
		case INSTR_BREAK        : return "BREAK";
		case INSTR_LABEL        : return "LABEL";
		case INSTR_LABEL_END    : return "LABELE";
		case INSTR_CONTINUE     : return "CONTINUE";
		case INSTR_RETURN       : return "RETURN";
		case INSTR_RETURNV      : return "RETURNV";
		case INSTR_TRUE         : return "TRUE";
		case INSTR_FALSE        : return "FALSE";
		case INSTR_NULL         : return "NULL";
		case INSTR_UNDEF        : return "UNDEF";
		case INSTR_NEW          : return "NEW";
		case INSTR_GET          : return "GET";
		case INSTR_BLOCK        : return "BLOCK";
		case INSTR_BLOCK_END    : return "BLOCKE";
		case INSTR_LOOP         : return "LOOP";
		case INSTR_LOOP_END     : return "LOOPE";
		case INSTR_TRY          : return "TRY";
		case INSTR_TRY_END      : return "TRYE";
		case INSTR_THROW        : return "THROW";
		case INSTR_CATCH        : return "CATCH";
		case INSTR_INSTOF       : return "INSTOF";
		case INSTR_TYPEOF       : return "TYPEOF";
		case INSTR_DELETE       : return "DELETE";
		case INSTR_DELETE_AT    : return "DELAT";
		case INSTR_DELETE_VAR   : return "DELVAR";
		case INSTR_IN           : return "IN";
		case INSTR_STRICT       : return "STRICT";
		case INSTR_INCLUDE      : return "INCLUDE";
		case INSTR_MODULE       : return "MODULE";
		case INSTR_EXPORT       : return "EXPORT";
		case INSTR_EXPORT_VALUE : return "EXPORTV";
		case INSTR_EXPORT_STAR  : return "EXPORTS";
		case INSTR_IMPORT_BIND  : return "IMPBIND";
		case INSTR_MODULE_V     : return "MODULEV";
		case INSTR_IMPORT_META  : return "IMPMETA";
		case INSTR_STATIC_BLK   : return "STATICBLK";
		case INSTR_GETLA        : return "GETLA";
		case INSTR_FIELDN       : return "FIELDN";
		case INSTR_STATICN      : return "STATICN";
		default                 : return "";
	}
}

static PC bc_get_inmstr_str(bytecode_t* bc, PC i, mstr_t* ret) {
	PC ins = bc->code_buf[i];
	opr_code_t instr = OP(ins);
	uint32_t offset = ins & OFF_MASK;

	char s[128+1];
	mstr_reset(ret);

	if(offset == OFF_MASK) {
		snprintf(s, 128, "%08d | 0x%08X ; %s", i, ins, inmstr_str(instr));	
		mstr_append(ret, s);
	}
	else {
		if(instr == INSTR_JMP ||
				instr == INSTR_NJMP ||
				instr == INSTR_NJMPB ||
				instr == INSTR_JMPB ||
				instr == INSTR_LOADP ||
				instr == INSTR_LOADPV ||
				instr == INSTR_INT_S) {
			snprintf(s, 128, "%08d | 0x%08X ; %s\t%d", i, ins, inmstr_str(instr), offset);	
			mstr_append(ret, s);
		}
		else {
			snprintf(s, 128, "%08d | 0x%08X ; %s\t\"", i, ins, inmstr_str(instr));	
			mstr_append(ret, s);
			mstr_append(ret, bc_getstr(bc, offset));
			mstr_add(ret, '"');
		}
	}
	
	if(instr == INSTR_INT) {
		ins = bc->code_buf[i+1];
		snprintf(s, 128, "\n%08d | 0x%08X ; %d", i+1, ins, ins);	
		mstr_append(ret, s);
		i++;
	}
	else if(instr == INSTR_FLOAT) {
		ins = bc->code_buf[i+1];
		float f;
		memcpy(&f, &ins, sizeof(PC));
		snprintf(s, 128, "\n%08d | 0x%08X ; %f", i+1, ins, f);	
		mstr_append(ret, s);
		i++;
	}	
	else if(instr == INSTR_INT64) {
		uint32_t words[2];
		words[0] = bc->code_buf[i+1];
		words[1] = bc->code_buf[i+2];
		int64_t ll;
		memcpy(&ll, words, sizeof(ll));
		snprintf(s, 128, "\n%08d | 0x%08X %08X ; %" PRId64, i+1, words[0], words[1], ll);
		mstr_append(ret, s);
		i += 2;
	}
	else if(instr == INSTR_FLOAT64) {
		uint32_t words[2];
		words[0] = bc->code_buf[i+1];
		words[1] = bc->code_buf[i+2];
		double dd;
		memcpy(&dd, words, sizeof(dd));
		snprintf(s, 128, "\n%08d | 0x%08X %08X ; %.17g", i+1, words[0], words[1], dd);
		mstr_append(ret, s);
		i += 2;
	}
	return i;
}

/* TEMP taobao diag: disassemble a window of instructions around `center`.
 * Decoding starts at pc 0 so instruction boundaries stay correct, but only the
 * last `radius` lines before (and after) the center are kept, which makes it
 * usable on a multi-hundred-KB bundle where bc_dump() would print megabytes. */
void bc_dump_window(bytecode_t* bc, PC center, PC radius) {
	if(bc == NULL || bc->code_buf == NULL || radius == 0)
		return;
	uint32_t cap = radius * 2 + 4;
	char** ring = (char**)calloc(cap, sizeof(char*));
	uint32_t* ring_pc = (uint32_t*)calloc(cap, sizeof(uint32_t));
	if(ring == NULL || ring_pc == NULL) { free(ring); free(ring_pc); return; }

	mstr_t* s = mstr_new("");
	PC i = 0, head = 0, count = 0;
	while(i < bc->cindex) {
		PC start = i;
		mstr_reset(s);
		i = bc_get_inmstr_str(bc, i, s);
		i++;
		if(start + radius >= center && start <= center + radius) {
			uint32_t slot = head % cap;
			free(ring[slot]);
			ring[slot] = strdup(s->cstr);
			ring_pc[slot] = start;
			head++; count++;
		} else if(start > center + radius) {
			break;
		}
	}
	uint32_t n = count < cap ? count : cap;
	uint32_t k;
	for(k = 0; k < n; ++k) {
		uint32_t slot = (head - n + k) % cap;
		fprintf(stderr, "[bcwin]%s%s\n", ring[slot] ? ring[slot] : "",
				ring_pc[slot] == center ? "   <<< PC" : (ring_pc[slot] < center ? "  ." : ""));
	}
	for(k = 0; k < cap; ++k) free(ring[k]);
	free(ring); free(ring_pc);
	mstr_free(s);
}

/* Streaming variant of bc_dump: writes each line straight to `f`. bc_dump()
 * accumulates everything in one mstr_t, whose max/len are 16-bit bit-fields -
 * a big bundle's disassembly (multi-MB) silently wrapped and corrupted the
 * whole output. One mstr per line keeps every intermediate far below 64KB
 * (only a single >64KB string-table entry can truncate, and only its own
 * line). */
void bc_dump_file(bytecode_t* bc, FILE* f) {
	if(bc == NULL || f == NULL)
		return;
	PC i;
	PC sz = bc->mstr_table.size;
	fprintf(f, "mstr_index| value\n---------------------------------------\n");
	for(i = 0; i < sz; ++i)
		fprintf(f, "0x%06X | %s\n", (unsigned)i, (const char*)bc->mstr_table.items[i]);
	fprintf(f, "\npc_index | opr_code   ; instruction\n---------------------------------------\n");
	mstr_t* s = mstr_new("");
	i = 0;
	while(i < bc->cindex) {
		i = bc_get_inmstr_str(bc, i, s);
		fprintf(f, "%s\n", s->cstr);
		i++;
	}
	mstr_free(s);
	fprintf(f, "---------------------------------------\n");
	fflush(f);
}

mstr_t* bc_dump(bytecode_t* bc) {
	mstr_t* ret = mstr_new("");
    if(ret == NULL)
        return NULL;

	PC i;
	char index[64];   /* big bundles carry source-text strings: the formatted
	                   * index plus a multi-100KB table entry must not overrun
	                   * a 32-byte scratch (it corrupted whole dumps). */
	PC sz = bc->mstr_table.size;

	mstr_append(ret, "mstr_index| value\n");
	mstr_append(ret, "---------------------------------------\n");
	for(i=0; i<sz; ++i) {
		snprintf(index, sizeof(index), "0x%06X | ", (unsigned)i);
		mstr_append(ret, index);
		mstr_append(ret, (const char*)bc->mstr_table.items[i]);
		mstr_append(ret, "\n");
	}
	mstr_append(ret, "\npc_index | opr_code   ; instruction\n");
	mstr_append(ret, "---------------------------------------\n");

	mstr_t* s = mstr_new("");
	i = 0;
	while(i < bc->cindex) {
		i = bc_get_inmstr_str(bc, i, s);
		mstr_append(ret, s->cstr);
		mstr_append(ret, "\n");
		i++;
	}
	mstr_free(s);
	mstr_append(ret, "---------------------------------------\n");
	return ret;
}
