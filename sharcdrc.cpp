// license:BSD-3-Clause
// copyright-holders:Ville Linde

/******************************************************************************

    SHARC UML recompiler core

******************************************************************************/

#include "emu.h"
#include "debugger.h"
#include "sharc.h"
#include "sharcfe.h"
#include "cpu/drcfe.h"
#include "cpu/drcuml.h"
#include "cpu/drcumlsh.h"

using namespace uml;

// map variables
#define MAPVAR_PC                       M0
#define MAPVAR_CYCLES                   M1

// exit codes 
#define EXECUTE_OUT_OF_CYCLES           0
#define EXECUTE_MISSING_CODE            1
#define EXECUTE_UNMAPPED_CODE           2
#define EXECUTE_RESET_CACHE             3


#define REG(reg)						m_regmap[reg]
#define DM_I(reg)						mem(&m_core->dag1.i[reg])
#define DM_M(reg)						mem(&m_core->dag1.m[reg])
#define DM_L(reg)						mem(&m_core->dag1.l[reg])
#define DM_B(reg)						mem(&m_core->dag1.b[reg])
#define PM_I(reg)						mem(&m_core->dag2.i[reg])
#define PM_M(reg)						mem(&m_core->dag2.m[reg])
#define PM_L(reg)						mem(&m_core->dag2.l[reg])
#define PM_B(reg)						mem(&m_core->dag2.b[reg])
#define ASTAT_AZ						mem(&m_core->astat_drc.az)
#define ASTAT_AV						mem(&m_core->astat_drc.av)
#define ASTAT_AN						mem(&m_core->astat_drc.an)
#define ASTAT_AC						mem(&m_core->astat_drc.ac)
#define ASTAT_AS						mem(&m_core->astat_drc.as)
#define ASTAT_AI						mem(&m_core->astat_drc.ai)
#define ASTAT_MN						mem(&m_core->astat_drc.mn)
#define ASTAT_MV						mem(&m_core->astat_drc.mv)
#define ASTAT_MU						mem(&m_core->astat_drc.mu)
#define ASTAT_MI						mem(&m_core->astat_drc.mi)
#define ASTAT_SV						mem(&m_core->astat_drc.sv)
#define ASTAT_SZ						mem(&m_core->astat_drc.sz)
#define ASTAT_SS						mem(&m_core->astat_drc.ss)
#define ASTAT_BTF						mem(&m_core->astat_drc.btf)
#define FLAG0							mem(&m_core->flag[0])
#define FLAG1							mem(&m_core->flag[1])
#define FLAG2							mem(&m_core->flag[2])
#define FLAG3							mem(&m_core->flag[3])
#define CURLCNTR						mem(&m_core->curlcntr)
#define LCNTR							mem(&m_core->lcntr)
#define PCSTK							mem(&m_core->pcstk)
#define PCSTKP							mem(&m_core->pcstkp)
#define STKY							mem(&m_core->stky)

//#define ASTAT_CALC_REQUIRED				desc->regreq[0] & 0x10000
#define AZ_CALC_REQUIRED				desc->regreq[0] & 0x00010000
#define AV_CALC_REQUIRED				desc->regreq[0] & 0x00020000
#define AN_CALC_REQUIRED				desc->regreq[0] & 0x00040000
#define AC_CALC_REQUIRED				desc->regreq[0] & 0x00080000
#define AS_CALC_REQUIRED				desc->regreq[0] & 0x00100000
#define AI_CALC_REQUIRED				desc->regreq[0] & 0x00200000
#define MN_CALC_REQUIRED				desc->regreq[0] & 0x00400000
#define MV_CALC_REQUIRED				desc->regreq[0] & 0x00800000
#define MU_CALC_REQUIRED				desc->regreq[0] & 0x01000000
#define MI_CALC_REQUIRED				desc->regreq[0] & 0x02000000
#define SV_CALC_REQUIRED				desc->regreq[0] & 0x04000000
#define SZ_CALC_REQUIRED				desc->regreq[0] & 0x08000000
#define SS_CALC_REQUIRED				desc->regreq[0] & 0x10000000
#define BTF_CALC_REQUIRED				desc->regreq[0] & 0x20000000
#define AF_CALC_REQUIRED				desc->regreq[0] & 0x40000000


#define IRAM_BLOCK0_START				0x20000
#define IRAM_BLOCK0_END					0x27fff
#define IRAM_BLOCK1_START				0x28000
#define IRAM_BLOCK1_END					0x3ffff
#define IRAM_SHORT_BLOCK0_START			0x40000
#define IRAM_SHORT_BLOCK0_END			0x4ffff
#define IRAM_SHORT_BLOCK1_START			0x50000
#define IRAM_SHORT_BLOCK1_END			0x7ffff
#define IOP_REGISTER_START				0x00000
#define IOP_REGISTER_END				0x000ff
#define IRAM_END						0x7ffff


inline void adsp21062_device::alloc_handle(drcuml_state *drcuml, code_handle **handleptr, const char *name)
{
	if (*handleptr == nullptr)
		*handleptr = drcuml->handle_alloc(name);
}



static void cfunc_unimplemented(void *param)
{
	adsp21062_device *sharc = (adsp21062_device *)param;
	sharc->sharc_cfunc_unimplemented();
}

static void cfunc_read_iop(void *param)
{
	adsp21062_device *sharc = (adsp21062_device *)param;
	sharc->sharc_cfunc_read_iop();
}

static void cfunc_write_iop(void *param)
{
	adsp21062_device *sharc = (adsp21062_device *)param;
	sharc->sharc_cfunc_write_iop();
}

static void cfunc_pcstack_overflow(void *param)
{
	adsp21062_device *sharc = (adsp21062_device *)param;
	sharc->sharc_cfunc_pcstack_overflow();
}

static void cfunc_pcstack_underflow(void *param)
{
	adsp21062_device *sharc = (adsp21062_device *)param;
	sharc->sharc_cfunc_pcstack_underflow();
}


void adsp21062_device::sharc_cfunc_unimplemented()
{
	UINT64 op = m_core->arg64;
	fatalerror("PC=%08X: Unimplemented op %04X%08X\n", m_core->pc, (UINT32)(op >> 32), (UINT32)(op));
}

void adsp21062_device::sharc_cfunc_read_iop()
{
	m_core->arg1 = sharc_iop_r(m_core->arg0);
}

void adsp21062_device::sharc_cfunc_write_iop()
{
	printf("sharc iop write %08X, %08X\n", m_core->arg1, m_core->arg0);
	sharc_iop_w(m_core->arg0, m_core->arg1);
}

void adsp21062_device::sharc_cfunc_pcstack_overflow()
{
	fatalerror("SHARC: PCStack overflow");
}

void adsp21062_device::sharc_cfunc_pcstack_underflow()
{
	fatalerror("SHARC: PCStack underflow");
}


bool adsp21062_device::if_condition_always_true(int condition)
{
	if (condition == 0x1f || condition == 0x1e)
		return true;
	else
		return false;
}

bool adsp21062_device::if_condition_astat(int condition)
{
	switch (condition)
	{
	case 0x00:  /* EQ */
	case 0x01:  /* LT */
	case 0x02:  /* LE */
	case 0x03:  /* AC */
	case 0x04:  /* AV */
	case 0x05:  /* MV */
	case 0x06:  /* MS */
	case 0x07:  /* SV */
	case 0x08:  /* SZ */
	case 0x0d:  /* TF */
	case 0x10:  /* NOT EQUAL */
	case 0x11:  /* GE */
	case 0x12:  /* GT */
	case 0x13:  /* NOT AC */
	case 0x14:  /* NOT AV */
	case 0x15:  /* NOT MV */
	case 0x16:  /* NOT MS */
	case 0x17:  /* NOT SV */
	case 0x18:  /* NOT SZ */
	case 0x1d:  /* NOT TF */
		return true;
		break;

	default:
		return false;
	}
}



/*-------------------------------------------------
load_fast_iregs - load any fast integer
registers
-------------------------------------------------*/

inline void adsp21062_device::load_fast_iregs(drcuml_block *block)
{
	int regnum;

	for (regnum = 0; regnum < ARRAY_LENGTH(m_regmap); regnum++)
	{
		if (m_regmap[regnum].is_int_register())
		{
			UML_MOV(block, ireg(m_regmap[regnum].ireg() - REG_I0), mem(&m_core->r[regnum]));
		}
	}
}


/*-------------------------------------------------
save_fast_iregs - save any fast integer
registers
-------------------------------------------------*/

void adsp21062_device::save_fast_iregs(drcuml_block *block)
{
	int regnum;

	for (regnum = 0; regnum < ARRAY_LENGTH(m_regmap); regnum++)
	{
		if (m_regmap[regnum].is_int_register())
		{
			UML_MOV(block, mem(&m_core->r[regnum]), ireg(m_regmap[regnum].ireg() - REG_I0));
		}
	}
}

void adsp21062_device::static_generate_memory_accessor(MEM_ACCESSOR_TYPE type, const char *name, code_handle *&handleptr)
{
	// I0 = read/write data
	// I1 = address
	// I2 is trashed

	void* block0 = &m_internal_ram_block0[0];
	void* block0_1 = &m_internal_ram_block0[1];
	void* block0_2 = &m_internal_ram_block0[2];
	void* block1 = &m_internal_ram_block1[0];
	void* block1_1 = &m_internal_ram_block1[1];
	void* block1_2 = &m_internal_ram_block1[2];

	code_label label = 1;

	drcuml_block *block = m_drcuml->begin_block(1024);

	// add a global entry for this
	alloc_handle(m_drcuml.get(), &handleptr, name);
	UML_HANDLE(block, *handleptr);                                                          // handle  *handleptr

	switch (type)
	{
		case MEM_ACCESSOR_PM_READ48:
			UML_CMP(block, I1, IRAM_BLOCK0_START);						// cmp     i1,IRAM_BLOCK0_START
			UML_JMPc(block, COND_B, label);								// jb      label1
			UML_CMP(block, I1, IRAM_BLOCK0_END);						// cmp     i1,IRAM_BLOCK0_END
			UML_JMPc(block, COND_A, label);								// ja      label1
			
			// 0x20000 ... 0x27fff
			UML_AND(block, I1, I1, 0x7fff);								// and     i1,i1,0x7fff
			UML_MULS(block, I1, I1, I1, 3);								// muls    i1,3
			UML_DLOAD(block, I0, block0, I1, SIZE_WORD, SCALE_x2);		// dload   i0,[block0],i1,word,scale_x2
			UML_DSHL(block, I0, I0, 32);								// dshl    i0,i0,32
			UML_DLOAD(block, I2, block0_1, I1, SIZE_WORD, SCALE_x2);	// dload   i2,[block0_1],i1,word,scale_x2
			UML_DSHL(block, I2, I2, 16);								// dshl    i2,i2,16
			UML_DOR(block, I0, I0, I2);									// dor     i0,i0,i2
			UML_DLOAD(block, I2, block0_2, I1, SIZE_WORD, SCALE_x2);	// dload   i2,[block0_2],i1,word,scale_x2
			UML_DOR(block, I0, I0, I2);									// dor     i0,i0,i2
			UML_RET(block);												// ret

			UML_LABEL(block, label++);									// label1:
			UML_CMP(block, I1, IRAM_BLOCK1_START);						// cmp     i1,IRAM_BLOCK1_START
			UML_JMPc(block, COND_B, label);								// jb      label2
			UML_CMP(block, I1, IRAM_BLOCK1_END);						// cmp     i1,IRAM_BLOCK1_END
			UML_JMPc(block, COND_A, label);								// ja      label2

			// 0x28000  ... 0x3ffff
			UML_AND(block, I1, I1, 0x7fff);								// and     i1,i1,0x7fff (block 1 is mirrored in 0x28000...2ffff, 0x30000...0x37fff and 0x38000...3ffff)
			UML_MULS(block, I1, I1, I1, 3);								// muls    i1,3
			UML_DLOAD(block, I0, block1, I1, SIZE_WORD, SCALE_x2);		// dload   i0,[block1],i1,word,scale_x2
			UML_DSHL(block, I0, I0, 32);								// dshl    i0,i0,32
			UML_DLOAD(block, I2, block1_1, I1, SIZE_WORD, SCALE_x2);	// dload   i2,[block1_1],i1,word,scale_x2
			UML_DSHL(block, I2, I2, 16);								// dshl    i2,i2,16
			UML_DOR(block, I0, I0, I2);									// dor     i0,i0,i2
			UML_DLOAD(block, I2, block1_2, I1, SIZE_WORD, SCALE_x2);	// dload   i2,[block1_2],i1,word,scale_x2
			UML_DOR(block, I0, I0, I2);									// dor     i0,i0,i2
			UML_RET(block);												// ret

			UML_LABEL(block, label++);									// label2:
			break;

		case MEM_ACCESSOR_PM_WRITE48:
			UML_CMP(block, I1, IRAM_BLOCK0_START);						// cmp     i1,IRAM_BLOCK0_START
			UML_JMPc(block, COND_B, label);								// jb      label1
			UML_CMP(block, I1, IRAM_BLOCK0_END);						// cmp     i1,IRAM_BLOCK0_END
			UML_JMPc(block, COND_A, label);								// ja      label1

			// 0x20000 ... 0x27fff
			UML_AND(block, I1, I1, 0x7fff);								// and     i1,i1,0x7fff
			UML_MULS(block, I1, I1, I1, 3);								// muls    i1,3
			UML_DSTORE(block, block0_2, I1, I0, SIZE_WORD, SCALE_x2);	// dstore  [block0_2],i1,i0,word,scale_x2
			UML_DSHR(block, I0, I0, 16);								// dshr    i0,i0,16
			UML_DSTORE(block, block0_1, I1, I0, SIZE_WORD, SCALE_x2);	// dstore  [block0_1],i1,i0,word,scale_x2
			UML_DSHR(block, I0, I0, 16);								// dshr    i0,i0,16
			UML_DSTORE(block, block0, I1, I0, SIZE_WORD, SCALE_x2);		// dstore  [block0],i1,i0,word,scale_x2
			UML_RET(block);												// ret

			UML_LABEL(block, label++);									// label1:
			UML_CMP(block, I1, IRAM_BLOCK1_START);						// cmp     i1,IRAM_BLOCK1_START
			UML_JMPc(block, COND_B, label);								// jb      label2
			UML_CMP(block, I1, IRAM_BLOCK1_END);						// cmp     i1,IRAM_BLOCK1_END
			UML_JMPc(block, COND_A, label);								// ja      label2

			// 0x28000  ... 0x3ffff
			UML_AND(block, I1, I1, 0x7fff);								// and     i1,i1,0x7fff (block 1 is mirrored in 0x28000...2ffff, 0x30000...0x37fff and 0x38000...3ffff)
			UML_MULS(block, I1, I1, I1, 3);								// muls    i1,3
			UML_DSTORE(block, block1_2, I1, I0, SIZE_WORD, SCALE_x2);	// dstore  [block1_2],i1,i0,word,scale_x2
			UML_DSHR(block, I0, I0, 16);								// dshr    i0,i0,16
			UML_DSTORE(block, block1_1, I1, I0, SIZE_WORD, SCALE_x2);	// dstore  [block1_1],i1,i0,word,scale_x2
			UML_DSHR(block, I0, I0, 16);								// dshr    i0,i0,16
			UML_DSTORE(block, block1, I1, I0, SIZE_WORD, SCALE_x2);		// dstore  [block1],i1,i0,word,scale_x2		
			UML_RET(block);												// ret

			UML_LABEL(block, label++);									// label2:
			break;

		case MEM_ACCESSOR_PM_READ32:
			UML_CMP(block, I1, IRAM_BLOCK0_START);						// cmp     i1,IRAM_BLOCK0_START
			UML_JMPc(block, COND_B, label);								// jb      label1
			UML_CMP(block, I1, IRAM_BLOCK0_END);						// cmp     i1,IRAM_BLOCK0_END
			UML_JMPc(block, COND_A, label);								// ja      label1
			
			// 0x20000 ... 0x27fff
			UML_AND(block, I1, I1, 0x7fff);								// and     i1,i1,0x7fff
			UML_MULS(block, I1, I1, I1, 3);								// muls    i1,3
			UML_LOAD(block, I0, block0, I1, SIZE_WORD, SCALE_x2);		// load    i0,[block0],i1,word,scale_x2
			UML_SHL(block, I0, I0, 16);									// shl     i0,i0,16
			UML_LOAD(block, I2, block0_1, I1, SIZE_WORD, SCALE_x2);		// load    i2,[block0_1],i1,word,scale_x2
			UML_OR(block, I0, I0, I2);									// or      i0,i0,i2
			UML_RET(block);												// ret

			UML_LABEL(block, label++);									// label1:
			UML_CMP(block, I1, IRAM_BLOCK1_START);						// cmp     i1,IRAM_BLOCK1_START
			UML_JMPc(block, COND_B, label);								// jb      label2
			UML_CMP(block, I1, IRAM_BLOCK1_END);						// cmp     i1,IRAM_BLOCK1_END
			UML_JMPc(block, COND_A, label);								// ja      label2

			// 0x28000  ... 0x3ffff
			UML_AND(block, I1, I1, 0x7fff);								// and     i1,i1,0x7fff (block 1 is mirrored in 0x28000...2ffff, 0x30000...0x37fff and 0x38000...3ffff)
			UML_MULS(block, I1, I1, I1, 3);								// muls    i1,3
			UML_LOAD(block, I0, block1, I1, SIZE_WORD, SCALE_x2);		// load    i0,[block1],i1,word,scale_x2
			UML_SHL(block, I0, I0, 16);									// shl     i0,i0,16
			UML_LOAD(block, I2, block1_1, I1, SIZE_WORD, SCALE_x2);		// load    i2,[block1_1],i1,word,scale_x2			
			UML_OR(block, I0, I0, I2);									// or      i0,i0,i2
			UML_RET(block);												// ret

			UML_LABEL(block, label++);									// label2:
			break;

		case MEM_ACCESSOR_PM_WRITE32:
			UML_CMP(block, I1, IRAM_BLOCK0_START);						// cmp     i1,IRAM_BLOCK0_START
			UML_JMPc(block, COND_B, label);								// jb      label1
			UML_CMP(block, I1, IRAM_BLOCK0_END);						// cmp     i1,IRAM_BLOCK0_END
			UML_JMPc(block, COND_A, label);								// ja      label1

			// 0x20000 ... 0x27fff
			UML_AND(block, I1, I1, 0x7fff);								// and     i1,i1,0x7fff
			UML_MULS(block, I1, I1, I1, 3);								// muls    i1,3
			UML_STORE(block, block0_1, I1, I0, SIZE_WORD, SCALE_x2);	// store   [block0_1],i1,i0,word,scale_x2
			UML_SHR(block, I0, I0, 16);									// shr     i0,i0,16
			UML_STORE(block, block0, I1, I0, SIZE_WORD, SCALE_x2);		// store   [block0],i1,i0,word,scale_x2
			UML_RET(block);												// ret

			UML_LABEL(block, label++);									// label1:
			UML_CMP(block, I1, IRAM_BLOCK1_START);						// cmp     i1,IRAM_BLOCK1_START
			UML_JMPc(block, COND_B, label);								// jb      label2
			UML_CMP(block, I1, IRAM_BLOCK1_END);						// cmp     i1,IRAM_BLOCK1_END
			UML_JMPc(block, COND_A, label);								// ja      label2

			// 0x28000  ... 0x3ffff
			UML_AND(block, I1, I1, 0x7fff);								// and     i1,i1,0x7fff (block 1 is mirrored in 0x28000...2ffff, 0x30000...0x37fff and 0x38000...3ffff)
			UML_MULS(block, I1, I1, I1, 3);								// muls    i1,3
			UML_STORE(block, block1_1, I1, I0, SIZE_WORD, SCALE_x2);	// store   [block1_1],i1,i0,word,scale_x2
			UML_SHR(block, I0, I0, 16);									// shr     i0,i0,16
			UML_STORE(block, block1, I1, I0, SIZE_WORD, SCALE_x2);		// store   [block1],i1,i0,word,scale_x2
			UML_RET(block);												// ret

			UML_LABEL(block, label++);									// label2:
			break;

		case MEM_ACCESSOR_DM_READ32:
			UML_CMP(block, I1, IRAM_END);								// cmp     i1,IRAM_END
			UML_JMPc(block, COND_BE, label);							// jbe     label1
			// 0x80000 ...
			UML_SHL(block, I1, I1, 2);									// shl     i1,i1,2
			UML_READ(block, I0, I1, SIZE_DWORD, SPACE_DATA);			// read    i0,i1,dword,SPACE_DATA
			UML_RET(block);

			UML_LABEL(block, label++);									// label1:
			UML_CMP(block, I1, IRAM_BLOCK0_START);						// cmp     i1,IRAM_BLOCK0_START
			UML_JMPc(block, COND_B, label);								// jb      label2
			UML_CMP(block, I1, IRAM_BLOCK0_END);						// cmp     i1,IRAM_BLOCK0_END
			UML_JMPc(block, COND_A, label);								// ja      label2
			// 0x20000 ... 0x27fff
			UML_AND(block, I1, I1, 0x7fff);								// and     i1,i1,0x7fff
			UML_LOAD(block, I0, block0, I1, SIZE_WORD, SCALE_x4);		// load    i0,[block0],i1,word,scale_x4
			UML_SHL(block, I0, I0, 16);									// shl     i0,i0,16
			UML_LOAD(block, I2, block0_1, I1, SIZE_WORD, SCALE_x4);		// load    i2,[block0_1],i1,word,scale_x4
			UML_OR(block, I0, I0, I2);									// or      i0,i0,i2
			UML_RET(block);

			UML_LABEL(block, label++);									// label2:
			UML_CMP(block, I1, IRAM_BLOCK1_START);						// cmp     i1,IRAM_BLOCK1_START
			UML_JMPc(block, COND_B, label);								// jb      label3
			UML_CMP(block, I1, IRAM_BLOCK1_END);						// cmp     i1,IRAM_BLOCK1_END
			UML_JMPc(block, COND_A, label);								// ja      label3
			// 0x28000 ... 0x3ffff
			UML_AND(block, I1, I1, 0x7fff);								// and     i1,i1,0x7fff
			UML_LOAD(block, I0, block1, I1, SIZE_WORD, SCALE_x4);		// load    i0,[block1],i1,word,scale_x4
			UML_SHL(block, I0, I0, 16);									// shl     i0,i0,16
			UML_LOAD(block, I2, block1_1, I1, SIZE_WORD, SCALE_x4);		// load    i2,[block1_1],i1,word,scale_x4
			UML_OR(block, I0, I0, I2);									// or      i0,i0,i2
			UML_RET(block);

			UML_LABEL(block, label++);									// Label3:
			UML_CMP(block, I1, IOP_REGISTER_END);						// cmp     i1,IOP_REGISTER_END
			UML_JMPc(block, COND_A, label);								// ja      label4
			// IOP registers
			UML_MOV(block, mem(&m_core->arg0), I1);						// mov     [m_core->arg0],i1
			UML_CALLC(block, cfunc_read_iop, this);						// callc   cfunc_read_iop
			UML_MOV(block, I0, mem(&m_core->arg1));						// mov     i0,[m_core->arg1]
			UML_RET(block);

			UML_LABEL(block, label++);									// label4:
			UML_CMP(block, I1, IRAM_SHORT_BLOCK0_START);				// cmp     i1,IRAM_SHORT_BLOCK0_START
			UML_JMPc(block, COND_B, label+1);							// jb      label6
			UML_CMP(block, I1, IRAM_SHORT_BLOCK0_END);					// cmp     i1,IRAM_SHORT_BLOCK0_END
			UML_JMPc(block, COND_A, label+1);							// ja      label6
			// 0x40000 ... 0x4ffff
			UML_AND(block, I1, I1, 0xffff);								// and     i1,i1,0xffff
			UML_XOR(block, I1, I1, 1);									// xor     i1,i1,1
			UML_TEST(block, mem(&m_core->mode1), 0x4000);				// test    [m_core->mode1],0x4000
			UML_JMPc(block, COND_Z, label);								// jz      label5
			UML_LOADS(block, I0, block0, I1, SIZE_WORD, SCALE_x2);		// loads   i0,[block0],i1,word,scale_x2
			UML_RET(block);
			UML_LABEL(block, label++);									// label5:
			UML_LOAD(block, I0, block0, I1, SIZE_WORD, SCALE_x2);		// load    i0,[block0],i1,word,scale_x2
			UML_RET(block);

			UML_LABEL(block, label++);									// label6:
			UML_CMP(block, I1, IRAM_SHORT_BLOCK1_START);				// cmp     i1,IRAM_SHORT_BLOCK1_START
			UML_JMPc(block, COND_B, label+1);							// jb      label8
			UML_CMP(block, I1, IRAM_SHORT_BLOCK1_END);					// cmp     i1,IRAM_SHORT_BLOCK1_END
			UML_JMPc(block, COND_A, label+1);							// ja      label8
			// 0x50000 ... 0x7ffff
			UML_AND(block, I1, I1, 0xffff);								// and     i1,i1,0xffff
			UML_XOR(block, I1, I1, 1);									// xor     i1,i1,1
			UML_TEST(block, mem(&m_core->mode1), 0x4000);				// test    [m_core->mode1],0x4000
			UML_JMPc(block, COND_Z, label);								// jz      label7
			UML_LOADS(block, I0, block1, I1, SIZE_WORD, SCALE_x2);		// loads   i0,[block1],i1,word,scale_x2
			UML_RET(block);
			UML_LABEL(block, label++);									// label7:
			UML_LOAD(block, I0, block1, I1, SIZE_WORD, SCALE_x2);		// load    i0,[block1],i1,word,scale_x2
			UML_RET(block);

			UML_LABEL(block, label++);									// label8:
			break;

		case MEM_ACCESSOR_DM_WRITE32:
			UML_CMP(block, I1, IRAM_END);								// cmp     i1,IRAM_END
			UML_JMPc(block, COND_BE, label);							// jbe     label1
			// 0x80000 ...
			UML_SHL(block, I1, I1, 2);									// shl     i1,i1,2
			UML_WRITE(block, I1, I0, SIZE_DWORD, SPACE_DATA);			// write   i1,i0,dword,SPACE_DATA
			UML_RET(block);

			UML_LABEL(block, label++);									// label1:
			UML_CMP(block, I1, IRAM_BLOCK0_START);						// cmp     i1,IRAM_BLOCK0_START
			UML_JMPc(block, COND_B, label);								// jb      label2
			UML_CMP(block, I1, IRAM_BLOCK0_END);						// cmp     i1,IRAM_BLOCK0_END
			UML_JMPc(block, COND_A, label);								// ja      label2
			// 0x20000 ... 0x27fff
			UML_AND(block, I1, I1, 0x7fff);								// and     i1,i1,0x7fff
			UML_STORE(block, block0_1, I1, I0, SIZE_WORD, SCALE_x4);	// store   [block0_1],i1,i0,word,scale_x4
			UML_SHR(block, I0, I0, 16);									// shr     i0,i0,16
			UML_STORE(block, block0, I1, I0, SIZE_WORD, SCALE_x4);		// store   [block0],i1,i0,word,scale_x4
			UML_RET(block);

			UML_LABEL(block, label++);									// label2:
			UML_CMP(block, I1, IRAM_BLOCK1_START);						// cmp     i1,IRAM_BLOCK1_START
			UML_JMPc(block, COND_B, label);								// jb      label3
			UML_CMP(block, I1, IRAM_BLOCK1_END);						// cmp     i1,IRAM_BLOCK1_END
			UML_JMPc(block, COND_A, label);								// ja      label3
			// 0x28000 ... 0x3ffff
			UML_AND(block, I1, I1, 0x7fff);								// and     i1,i1,0x7fff
			UML_STORE(block, block1_1, I1, I0, SIZE_WORD, SCALE_x4);	// store   [block1_1],i1,i0,word,scale_x4
			UML_SHR(block, I0, I0, 16);									// shr     i0,i0,16
			UML_STORE(block, block1, I1, I0, SIZE_WORD, SCALE_x4);		// store   [block1],i1,i0,word,scale_x4
			UML_RET(block);

			UML_LABEL(block, label++);									// Label3:
			UML_CMP(block, I1, IOP_REGISTER_END);						// cmp     i1,IOP_REGISTER_END
			UML_JMPc(block, COND_A, label);								// ja      label4
			// IOP registers
			UML_MOV(block, mem(&m_core->arg0), I1);						// mov     [m_core->arg0],i1
			UML_MOV(block, mem(&m_core->arg1), I0);						// mov     [m_core->arg1],i0
			UML_CALLC(block, cfunc_write_iop, this);					// callc   cfunc_write_iop
			UML_RET(block);

			UML_LABEL(block, label++);									// label4:
			UML_CMP(block, I1, IRAM_SHORT_BLOCK0_START);				// cmp     i1,IRAM_SHORT_BLOCK0_START
			UML_JMPc(block, COND_B, label);								// jb      label5
			UML_CMP(block, I1, IRAM_SHORT_BLOCK0_END);					// cmp     i1,IRAM_SHORT_BLOCK0_END
			UML_JMPc(block, COND_A, label);								// ja      label5
			// 0x40000 ... 0x4ffff
			UML_AND(block, I1, I1, 0xffff);								// and     i1,i1,0xffff
			UML_XOR(block, I1, I1, 1);									// xor     i1,i1,1
			UML_STORE(block, block0, I1, I0, SIZE_WORD, SCALE_x2);		// store   [block0],i1,i0,word,scale_x2
			UML_RET(block);

			UML_LABEL(block, label++);									// label5:
			UML_CMP(block, I1, IRAM_SHORT_BLOCK1_START);				// cmp     i1,IRAM_SHORT_BLOCK1_START
			UML_JMPc(block, COND_B, label);								// jb      label6
			UML_CMP(block, I1, IRAM_SHORT_BLOCK1_END);					// cmp     i1,IRAM_SHORT_BLOCK1_END
			UML_JMPc(block, COND_A, label);								// ja      label6
			// 0x50000 ... 0x7ffff
			UML_AND(block, I1, I1, 0xffff);								// and     i1,i1,0xffff
			UML_XOR(block, I1, I1, 1);									// xor     i1,i1,1
			UML_STORE(block, block1, I1, I0, SIZE_WORD, SCALE_x2);		// store   [block1],i1,i0,word,scale_x2
			UML_RET(block);

			UML_LABEL(block, label++);									// label6:
			break;
	}

	UML_RET(block);

	block->end();
}

void adsp21062_device::static_generate_push_pc()
{
	// Push contents of I0 to PC stack
	// Trashes I1

	code_label label = 1;
	drcuml_block *block = m_drcuml->begin_block(32);

	// add a global entry for this
	alloc_handle(m_drcuml.get(), &m_push_pc, "push_pc");
	UML_HANDLE(block, *m_push_pc);											// handle  *m_push_pc

	UML_MOV(block, I1, PCSTKP);												// mov     i1,PCSTKP
	UML_ADD(block, I1, I1, 1);												// add     i1,i1,1
	UML_CMP(block, I1, 32);													// cmp     i1,32
	UML_JMPc(block, COND_L,label);											// jl      label1
	UML_CALLC(block, cfunc_pcstack_overflow, this);							// callc   cfunc_pcstack_overflow

	UML_LABEL(block, label++);												// label1:	
	UML_CMP(block, I1, 0);													// cmp     i1,0
	UML_JMPc(block, COND_E, label);											// je      label2
	UML_AND(block, STKY, STKY, ~0x400000);									// and     STKY,~0x400000
	UML_JMP(block, label + 1);												// jmp     label3
	UML_LABEL(block, label++);												// label2:
	UML_OR(block, STKY, STKY, 0x400000);									// or      STKY,0x400000

	UML_LABEL(block, label++);												// label3:
	UML_MOV(block, PCSTK, I0);												// mov     PCSTK,pc
	UML_STORE(block, &m_core->pcstack, I1, I0, SIZE_DWORD, SCALE_x4);		// store   [m_core->pcstack],i1,i0,dword,scale_x4
	UML_MOV(block, PCSTKP, I1);												// mov     PCSTKP,i1

	UML_RET(block);

	block->end();
}

void adsp21062_device::static_generate_pop_pc()
{
	// Pop PC stack into I0
	// Trashes I1

	code_label label = 1;
	drcuml_block *block = m_drcuml->begin_block(32);

	// add a global entry for this
	alloc_handle(m_drcuml.get(), &m_pop_pc, "pop_pc");
	UML_HANDLE(block, *m_pop_pc);											// handle  *m_pop_pc

	UML_MOV(block, I1, PCSTKP);												// mov     i0,PCSTKP
	UML_LOAD(block, I0, &m_core->pcstack, I1, SIZE_DWORD, SCALE_x4);		// load    i1,[m_core->pcstack],i0,dword,scale_x4
	UML_CMP(block, I1, 0);													// cmp     i1,0
	UML_JMPc(block, COND_NE, label);										// jne     label1
	UML_CALLC(block, cfunc_pcstack_underflow, this);						// callc   cfunc_pcstack_underflow

	UML_LABEL(block, label++);												// label1:
	UML_SUB(block, I1, I1, 1);												// sub     i1,i1,1
	UML_CMP(block, I1, 0);													// cmp     i1,0
	UML_JMPc(block, COND_E, label);											// je      label2
	UML_AND(block, STKY, STKY, ~0x400000);									// and     STKY,~0x400000
	UML_JMP(block, label + 1);												// jmp     label3
	UML_LABEL(block, label++);												// label2:
	UML_OR(block, STKY, STKY, 0x400000);									// or      STKY,0x400000

	UML_LABEL(block, label++);												// label3:
	UML_MOV(block, PCSTKP, I1);												// mov     PCSTKP,i1
	UML_MOV(block, PCSTK, I0);												// mov     PCSTK,i0

	UML_RET(block);

	block->end();
}

void adsp21062_device::static_generate_push_loop()
{
	// I0 = loop target
	// I1 = counter
	// I2 = type/condition?

	code_label label = 1;
	drcuml_block *block = m_drcuml->begin_block(32);

	// add a global entry for this
	alloc_handle(m_drcuml.get(), &m_push_loop, "push_loop");
	UML_HANDLE(block, *m_push_loop);										// handle  *m_push_loop

	UML_RET(block);

	block->end();
}

void adsp21062_device::static_generate_pop_loop()
{
	// TODO
	// Trashes I1

	code_label label = 1;
	drcuml_block *block = m_drcuml->begin_block(32);

	// add a global entry for this
	alloc_handle(m_drcuml.get(), &m_pop_loop, "pop_loop");
	UML_HANDLE(block, *m_pop_loop);											// handle  *m_pop_loop

	UML_RET(block);

	block->end();
}



void adsp21062_device::execute_run_drc()
{
	drcuml_state *drcuml = m_drcuml.get();
	int execute_result;

	/* reset the cache if dirty */
	if (m_cache_dirty)
		flush_cache();

	m_cache_dirty = false;

	/* execute */
	do
	{
		execute_result = drcuml->execute(*m_entry);

		/* if we need to recompile, do it */
		if (execute_result == EXECUTE_MISSING_CODE)
		{
			compile_block(m_core->pc);
		}
		else if (execute_result == EXECUTE_UNMAPPED_CODE)
		{
			fatalerror("Attempted to execute unmapped code at PC=%08X\n", m_core->pc);
		}
		else if (execute_result == EXECUTE_RESET_CACHE)
		{
			flush_cache();
		}
	} while (execute_result != EXECUTE_OUT_OF_CYCLES);
}


void adsp21062_device::compile_block(offs_t pc)
{
	compiler_state compiler = { 0 };

	const opcode_desc *seqhead, *seqlast;
	const opcode_desc *desclist;
	bool override = false;

	drcuml_block *block;

	desclist = m_drcfe->describe_code(pc);

	bool succeeded = false;
	while (!succeeded)
	{
		try
		{
			block = m_drcuml->begin_block(4096);

			for (seqhead = desclist; seqhead != nullptr; seqhead = seqlast->next())
			{
				const opcode_desc *curdesc;
				UINT32 nextpc;

				/* determine the last instruction in this sequence */
				for (seqlast = seqhead; seqlast != nullptr; seqlast = seqlast->next())
					if (seqlast->flags & OPFLAG_END_SEQUENCE)
						break;
				assert(seqlast != nullptr);

				/* if we don't have a hash for this mode/pc, or if we are overriding all, add one */
				if (override || m_drcuml->hash_exists(0, seqhead->pc))
					UML_HASH(block, 0, seqhead->pc);                                        // hash    mode,pc

																							/* if we already have a hash, and this is the first sequence, assume that we */
																							/* are recompiling due to being out of sync and allow future overrides */
				else if (seqhead == desclist)
				{
					override = true;
					UML_HASH(block, 0, seqhead->pc);                                        // hash    mode,pc
				}

				/* otherwise, redispatch to that fixed PC and skip the rest of the processing */
				else
				{
					UML_LABEL(block, seqhead->pc | 0x80000000);                             // label   seqhead->pc
					UML_HASHJMP(block, 0, seqhead->pc, *m_nocode);							// hashjmp <0>,seqhead->pc,nocode
					continue;
				}

				/* label this instruction, if it may be jumped to locally */
				if (seqhead->flags & OPFLAG_IS_BRANCH_TARGET)
					UML_LABEL(block, seqhead->pc | 0x80000000);                             // label   seqhead->pc

				/* iterate over instructions in the sequence and compile them */
				for (curdesc = seqhead; curdesc != seqlast->next(); curdesc = curdesc->next())
					generate_sequence_instruction(block, &compiler, curdesc);

				/* if we need to return to the start, do it */
				if (seqlast->flags & OPFLAG_RETURN_TO_START)
					nextpc = pc;
				/* otherwise we just go to the next instruction */
				else
					nextpc = seqlast->pc + (seqlast->skipslots + 1);

				/* count off cycles and go there */
				generate_update_cycles(block, &compiler, nextpc, TRUE);						// <subtract cycles>

				if (seqlast->next() == nullptr || seqlast->next()->pc != nextpc)
					UML_HASHJMP(block, 0, nextpc, *m_nocode);								// hashjmp <mode>,nextpc,nocode
			}

			block->end();
			succeeded = true;
		}
		catch (drcuml_block::abort_compilation &)
		{
			flush_cache();
		}
	}
}


void adsp21062_device::flush_cache()
{
	/* empty the transient cache contents */
	m_drcuml->reset();

	try
	{
		// generate the entry point and out-of-cycles handlers
		static_generate_entry_point();
		static_generate_nocode_handler();
		static_generate_out_of_cycles();

		// append exception handlers for various types
//		static_generate_exception(EXCEPTION_RESET,     TRUE,  "exception_reset");

		// generate memory accessors
		static_generate_memory_accessor(MEM_ACCESSOR_PM_READ48, "pm_read48", m_pm_read48);
		static_generate_memory_accessor(MEM_ACCESSOR_PM_WRITE48, "pm_write48", m_pm_write48);
		static_generate_memory_accessor(MEM_ACCESSOR_PM_READ32, "pm_read32", m_pm_read32);
		static_generate_memory_accessor(MEM_ACCESSOR_PM_WRITE32, "pm_write32", m_pm_write32);
		static_generate_memory_accessor(MEM_ACCESSOR_DM_READ32, "dm_read32", m_dm_read32);
		static_generate_memory_accessor(MEM_ACCESSOR_DM_WRITE32, "dm_write32", m_dm_write32);

		// generate utility functions
		static_generate_push_pc();
		static_generate_pop_pc();
		static_generate_push_loop();
		static_generate_pop_loop();
	}
	catch (drcuml_block::abort_compilation &)
	{
		fatalerror("Error generating SHARC static handlers\n");
	}
}


void adsp21062_device::static_generate_entry_point()
{
	//code_label skip = 1;
	drcuml_block *block;

	/* begin generating */
	block = m_drcuml->begin_block(20);

	/* forward references */
	alloc_handle(m_drcuml.get(), &m_nocode, "nocode");

	alloc_handle(m_drcuml.get(), &m_entry, "entry");
	UML_HANDLE(block, *m_entry);															// handle  entry

	load_fast_iregs(block);																	// <load fastregs>

	/* check for interrupts */
	UML_MOV(block, I0, 1);

	/* generate a hash jump via the current mode and PC */
	UML_HASHJMP(block, 0, mem(&m_core->pc), *m_nocode);   // hashjmp <mode>,<pc>,nocode

	block->end();
}


void adsp21062_device::static_generate_nocode_handler()
{
	drcuml_block *block;

	/* begin generating */
	block = m_drcuml->begin_block(10);

	/* generate a hash jump via the current mode and PC */
	alloc_handle(m_drcuml.get(), &m_nocode, "nocode");
	UML_HANDLE(block, *m_nocode);															// handle  nocode
	UML_GETEXP(block, I0);																	// getexp  i0
	UML_MOV(block, mem(&m_core->pc), I0);													// mov     [pc],i0
	save_fast_iregs(block);																	// <save fastregs>
	UML_EXIT(block, EXECUTE_MISSING_CODE);													// exit    EXECUTE_MISSING_CODE

	block->end();
}

void adsp21062_device::static_generate_out_of_cycles()
{
	drcuml_block *block;

	/* begin generating */
	block = m_drcuml->begin_block(10);

	/* generate a hash jump via the current mode and PC */
	alloc_handle(m_drcuml.get(), &m_out_of_cycles, "out_of_cycles");
	UML_HANDLE(block, *m_out_of_cycles);													// handle  out_of_cycles
	UML_GETEXP(block, I0);																	// getexp  i0
	UML_MOV(block, mem(&m_core->pc), I0);													// mov     <pc>,i0
	save_fast_iregs(block);																	// <save fastregs>
	UML_EXIT(block, EXECUTE_OUT_OF_CYCLES);													// exit    EXECUTE_OUT_OF_CYCLES

	block->end();
}


void adsp21062_device::generate_sequence_instruction(drcuml_block *block, compiler_state *compiler, const opcode_desc *desc)
{
	/* add an entry for the log */
//	if (m_drcuml->logging() && !(desc->flags & OPFLAG_VIRTUAL_NOOP))
//		log_add_disasm_comment(block, desc->pc, desc->opptr.l[0]);

	/* set the PC map variable */
	UML_MAPVAR(block, MAPVAR_PC, desc->pc);                                                 // mapvar  PC,desc->pc

																							/* accumulate total cycles */
	compiler->cycles += desc->cycles;

	// insert delayed MODE1 operation if needed
	if (compiler->mode1_delay.counter > 0)
	{
		compiler->mode1_delay.counter--;
		if (compiler->mode1_delay.counter <= 0)
		{
			switch (compiler->mode1_delay.mode)
			{
				case MODE1_WRITE_IMM:
					generate_write_mode1_imm(block, compiler, desc, compiler->mode1_delay.data);
					break;
				case MODE1_WRITE_REG:
					fatalerror("MODE1 delay REG");
					break;
				case MODE1_SET:
					fatalerror("MODE1 delay SET");
					break;
				case MODE1_CLEAR:
					fatalerror("MODE1 delay CLEAR");
					break;
			}
		}
	}


	/* update the icount map variable */
	UML_MAPVAR(block, MAPVAR_CYCLES, compiler->cycles);                                     // mapvar  CYCLES,compiler->cycles

	/* if we are debugging, call the debugger */
	if ((machine().debug_flags & DEBUG_FLAG_ENABLED) != 0)
	{
		UML_MOV(block, mem(&m_core->pc), desc->pc);											// mov     [pc],desc->pc
		save_fast_iregs(block);																// <save fastregs>
		UML_DEBUG(block, desc->pc);															// debug   desc->pc
	}

	/* if we hit an unmapped address, fatal error */
	if (desc->flags & OPFLAG_COMPILER_UNMAPPED)
	{
		UML_MOV(block, mem(&m_core->pc), desc->pc);											// mov     [pc],desc->pc
		save_fast_iregs(block);																// <save fastregs>
		UML_EXIT(block, EXECUTE_UNMAPPED_CODE);												// exit    EXECUTE_UNMAPPED_CODE
	}

	/* if this is an invalid opcode, generate the exception now */
//	if (desc->flags & OPFLAG_INVALID_OPCODE)
//		UML_EXH(block, *m_exception[EXCEPTION_PROGRAM], 0x80000);							// exh    exception_program,0x80000

																		   /* otherwise, unless this is a virtual no-op, it's a regular instruction */
	else if (!(desc->flags & OPFLAG_VIRTUAL_NOOP))
	{
		/* compile the instruction */
		if (!generate_opcode(block, compiler, desc))
		{
			UML_MOV(block, mem(&m_core->pc), desc->pc);										// mov     [pc],desc->pc			
			UML_DMOV(block, mem(&m_core->arg64), desc->opptr.q[0]);							// dmov    [arg64],*desc->opptr.q
			UML_CALLC(block, cfunc_unimplemented, this);									// callc   cfunc_unimplemented,ppc
		}
	}
}

void adsp21062_device::generate_update_cycles(drcuml_block *block, compiler_state *compiler, uml::parameter param, int allow_exception)
{
	/* check full interrupts if pending */
	// TODO TODO TODO TODO TODO TODO
	/*
	if (compiler->checkints)
	{
		code_label skip;

		compiler->checkints = FALSE;
		UML_TEST(block, mem(&m_core->irq_pending), ~0);                                    // test    [irq_pending],0
		UML_JMPc(block, COND_Z, skip = compiler->labelnum++);                                   // jmp     skip,Z
		UML_TEST(block, MSR32, MSR_EE);                                             // test    [msr],MSR_EE
		UML_JMPc(block, COND_Z, skip);                                                      // jmp     skip,Z
		UML_MOV(block, I0, param);                                      // mov     i0,nextpc
		UML_MOV(block, I1, compiler->cycles);                                       // mov     i1,cycles
		UML_CALLH(block, *m_exception_norecover[EXCEPTION_EI]);                    // callh   interrupt_norecover
		UML_LABEL(block, skip);                                                         // skip:
	}
	*/

	/* account for cycles */
	if (compiler->cycles > 0)
	{
		UML_SUB(block, mem(&m_core->icount), mem(&m_core->icount), MAPVAR_CYCLES);                // sub     icount,icount,cycles
		UML_MAPVAR(block, MAPVAR_CYCLES, 0);                                                // mapvar  cycles,0
		if (allow_exception)
			UML_EXHc(block, COND_S, *m_out_of_cycles, param);      // exh     out_of_cycles,nextpc
	}
	compiler->cycles = 0;
}

void adsp21062_device::generate_write_mode1_imm(drcuml_block *block, compiler_state *compiler, const opcode_desc *desc, UINT32 data)
{
	// TODO: swap effects

	UML_MOV(block, mem(&m_core->mode1), data);
}

void adsp21062_device::generate_call(drcuml_block *block, compiler_state *compiler, const opcode_desc *desc, bool delayslot)
{
	compiler_state compiler_temp = *compiler;

	if (desc->targetpc == BRANCH_TARGET_DYNAMIC)
	{
		fatalerror("generate_call: dynamic branch at %08X", desc->pc);
	}

	// compile delay slots if needed
	if (delayslot)
	{
		generate_sequence_instruction(block, &compiler_temp, desc->delay.first());
		generate_sequence_instruction(block, &compiler_temp, desc->delay.last());
	}

	UML_MOV(block, I0, desc->pc + 3);
	UML_CALLH(block, *m_push_pc);

	// update cycles and hash jump
	if (desc->targetpc != BRANCH_TARGET_DYNAMIC)
	{
		generate_update_cycles(block, &compiler_temp, desc->targetpc, TRUE);
		if (desc->flags & OPFLAG_INTRABLOCK_BRANCH)
			UML_JMP(block, desc->targetpc | 0x80000000);								// jmp      targetpc | 0x80000000
		else
			UML_HASHJMP(block, 0, desc->targetpc, *m_nocode);							// hashjmp  0,targetpc,nocode
	}
	else
	{
		//	generate_update_cycles(block, &compiler_temp, mem(&m_core->jmpdest), TRUE);
		//	UML_HASHJMP(block, m_core->mode, mem(&m_core->jmpdest), *m_nocode);				// hashjmp  0,<reg>,nocode
	}

	// update compiler label
	compiler->labelnum = compiler_temp.labelnum;

	/* reset the mapvar to the current cycles and account for skipped slots */
	compiler->cycles += desc->skipslots;
	UML_MAPVAR(block, MAPVAR_CYCLES, compiler->cycles);									// mapvar  CYCLES,compiler->cycles
}

void adsp21062_device::generate_jump(drcuml_block *block, compiler_state *compiler, const opcode_desc *desc, bool delayslot, bool loopabort, bool clearint)
{
	compiler_state compiler_temp = *compiler;

	if (desc->targetpc == BRANCH_TARGET_DYNAMIC)
	{
		fatalerror("generate_jump: dynamic branch at %08X", desc->pc);
	}

	// compile delay slots if needed
	if (delayslot)
	{
		generate_sequence_instruction(block, &compiler_temp, desc->delay.first());
		generate_sequence_instruction(block, &compiler_temp, desc->delay.last());
	}

	// update cycles and hash jump
	if (desc->targetpc != BRANCH_TARGET_DYNAMIC)
	{
		generate_update_cycles(block, &compiler_temp, desc->targetpc, TRUE);
		if (desc->flags & OPFLAG_INTRABLOCK_BRANCH)
			UML_JMP(block, desc->targetpc | 0x80000000);								// jmp      targetpc | 0x80000000
		else
			UML_HASHJMP(block, 0, desc->targetpc, *m_nocode);							// hashjmp  0,targetpc,nocode
	}
	else
	{
	//	generate_update_cycles(block, &compiler_temp, mem(&m_core->jmpdest), TRUE);
	//	UML_HASHJMP(block, m_core->mode, mem(&m_core->jmpdest), *m_nocode);				// hashjmp  0,<reg>,nocode
	}

	// update compiler label
	compiler->labelnum = compiler_temp.labelnum;

	/* reset the mapvar to the current cycles and account for skipped slots */
	compiler->cycles += desc->skipslots;
	UML_MAPVAR(block, MAPVAR_CYCLES, compiler->cycles);									// mapvar  CYCLES,compiler->cycles
}

/*-------------------------------------------------
generate_write_ureg - UREG is read into I0
-------------------------------------------------*/

void adsp21062_device::generate_read_ureg(drcuml_block *block, compiler_state *compiler, const opcode_desc *desc, int ureg)
{
	// UREG is read into I0

	switch (ureg)
	{
		// REG 0-15
		case 0x00: case 0x01: case 0x02: case 0x03: case 0x04: case 0x05: case 0x06: case 0x07:
		case 0x08: case 0x09: case 0x0a: case 0x0b: case 0x0c: case 0x0d: case 0x0e: case 0x0f:
			UML_MOV(block, I0, REG(ureg & 0xf));
			break;
		// I0-7
		case 0x10: case 0x11: case 0x12: case 0x13: case 0x14: case 0x15: case 0x16: case 0x17:
			UML_MOV(block, I0, mem(&m_core->dag1.i[ureg & 7]));
			break;
		// I8-15
		case 0x18: case 0x19: case 0x1a: case 0x1b: case 0x1c: case 0x1d: case 0x1e: case 0x1f:
			UML_MOV(block, I0, mem(&m_core->dag2.i[ureg & 7]));
			break;
		// M0-7
		case 0x20: case 0x21: case 0x22: case 0x23: case 0x24: case 0x25: case 0x26: case 0x27:
			UML_MOV(block, I0, mem(&m_core->dag1.m[ureg & 7]));
			break;
		// M8-15
		case 0x28: case 0x29: case 0x2a: case 0x2b: case 0x2c: case 0x2d: case 0x2e: case 0x2f:
			UML_MOV(block, I0, mem(&m_core->dag2.m[ureg & 7]));
			break;
		// L0-7
		case 0x30: case 0x31: case 0x32: case 0x33: case 0x34: case 0x35: case 0x36: case 0x37:
			UML_MOV(block, I0, mem(&m_core->dag1.l[ureg & 7]));
			break;
		// L8-15
		case 0x38: case 0x39: case 0x3a: case 0x3b: case 0x3c: case 0x3d: case 0x3e: case 0x3f:
			UML_MOV(block, I0, mem(&m_core->dag2.l[ureg & 7]));
			break;
		// B0-7
		case 0x40: case 0x41: case 0x42: case 0x43: case 0x44: case 0x45: case 0x46: case 0x47:
			UML_MOV(block, I0, mem(&m_core->dag1.b[ureg & 7]));
			break;
		// B8-15
		case 0x48: case 0x49: case 0x4a: case 0x4b: case 0x4c: case 0x4d: case 0x4e: case 0x4f:
			UML_MOV(block, I0, mem(&m_core->dag2.b[ureg & 7]));
			break;

		case 0x64:		// PCSTK
			fatalerror("generate_read_ureg: PCSTK");
			break;
		case 0x70:		// USTAT1
			UML_MOV(block, I0, mem(&m_core->ustat1));
			break;
		case 0x71:		// USTAT2
			UML_MOV(block, I0, mem(&m_core->ustat2));
			break;
		case 0x79:		// IRPTL
			UML_MOV(block, I0, mem(&m_core->irptl));
			break;
		case 0x7a:		// MODE2
			UML_MOV(block, I0, mem(&m_core->mode2));
			break;
		case 0x7b:		// MODE1
			UML_MOV(block, I0, mem(&m_core->mode1));
			break;
		case 0x7c:		// ASTAT
			// TODO: needs full construct from bits
			fatalerror("generate_read_ureg: ASTAT");
			break;
		case 0x7d:		// IMASK
			UML_MOV(block, I0, mem(&m_core->imask));
			break;
		case 0x7e:		// STKY
			UML_MOV(block, I0, mem(&m_core->stky));
			break;
		case 0xdb:		// PX
			UML_DMOV(block, I0, mem(&m_core->px));		// NOTE: this returns 64 bits
			break;
		case 0xdc:		// PX1
			UML_DMOV(block, I0, mem(&m_core->px));
			break;
		case 0xdd:		// PX2
			UML_DMOV(block, I0, mem(&m_core->px));
			UML_DSHR(block, I0, I0, 32);
			break;

		default:
			fatalerror("generate_read_ureg %02X", ureg);
			break;
	}
}


/*-------------------------------------------------
	generate_write_ureg - contents of register I0 or 32-bit immediate data are written into UREG
-------------------------------------------------*/

void adsp21062_device::generate_write_ureg(drcuml_block *block, compiler_state *compiler, const opcode_desc *desc, int ureg, bool imm, UINT32 data)
{
	switch (ureg)
	{
		// REG 0-15
		case 0x00: case 0x01: case 0x02: case 0x03: case 0x04: case 0x05: case 0x06: case 0x07:
		case 0x08: case 0x09: case 0x0a: case 0x0b: case 0x0c: case 0x0d: case 0x0e: case 0x0f:
			UML_MOV(block, REG(ureg & 0xf), imm ? data : I0);
			break;
		// I0-7
		case 0x10: case 0x11: case 0x12: case 0x13: case 0x14: case 0x15: case 0x16: case 0x17:
			UML_MOV(block, DM_I(ureg & 7), imm ? data : I0);
			break;
		// I8-15
		case 0x18: case 0x19: case 0x1a: case 0x1b: case 0x1c: case 0x1d: case 0x1e: case 0x1f:
			UML_MOV(block, PM_I(ureg & 7), imm ? data : I0);
			break;
		// M0-7
		case 0x20: case 0x21: case 0x22: case 0x23: case 0x24: case 0x25: case 0x26: case 0x27:
			UML_MOV(block, DM_M(ureg & 7), imm ? data : I0);
			break;
		// M8-15
		case 0x28: case 0x29: case 0x2a: case 0x2b: case 0x2c: case 0x2d: case 0x2e: case 0x2f:
			UML_MOV(block, PM_M(ureg & 7), imm ? data : I0);
			break;
		// L0-7
		case 0x30: case 0x31: case 0x32: case 0x33: case 0x34: case 0x35: case 0x36: case 0x37:
			UML_MOV(block, DM_L(ureg & 7), imm ? data : I0);
			break;
		// L8-15
		case 0x38: case 0x39: case 0x3a: case 0x3b: case 0x3c: case 0x3d: case 0x3e: case 0x3f:
			UML_MOV(block, PM_L(ureg & 7), imm ? data : I0);
			break;
		// B0-7
		case 0x40: case 0x41: case 0x42: case 0x43: case 0x44: case 0x45: case 0x46: case 0x47:
			// Note: loading B also loads the same value in I
			UML_MOV(block, DM_B(ureg & 7), imm ? data : I0);
			UML_MOV(block, DM_I(ureg & 7), imm ? data : I0);
			break;
		// B8-15
		case 0x48: case 0x49: case 0x4a: case 0x4b: case 0x4c: case 0x4d: case 0x4e: case 0x4f:
			// Note: loading B also loads the same value in I
			UML_MOV(block, PM_B(ureg & 7), imm ? data : I0);
			UML_MOV(block, PM_B(ureg & 7), imm ? data : I0);
			break;

		case 0x64:		// PCSTK
			fatalerror("generate_write_ureg: PCSTK");
			break;
		case 0x70:		// USTAT1
			UML_MOV(block, mem(&m_core->ustat1), imm ? data : I0);
			break;
		case 0x71:		// USTAT2
			UML_MOV(block, mem(&m_core->ustat2), imm ? data : I0);
			break;
		case 0x79:		// IRPTL
			UML_MOV(block, mem(&m_core->irptl), imm ? data : I0);
			break;
		case 0x7a:		// MODE2
			UML_MOV(block, mem(&m_core->mode2), imm ? data : I0);
			break;
		case 0x7b:		// MODE1
			// MODE1 needs to be written delayed
			if (imm)
			{
				compiler->mode1_delay.counter = 2;
				compiler->mode1_delay.data = data;
				compiler->mode1_delay.mode = MODE1_WRITE_IMM;
			}
			else
			{
				compiler->mode1_delay.counter = 2;
				compiler->mode1_delay.mode = MODE1_WRITE_REG;
				UML_MOV(block, mem(&m_core->mode1_delay_data), I0);
			}
			break;
		case 0x7c:		// ASTAT
			// TODO: needs bit break up
			fatalerror("generate_write_ureg: ASTAT");
			break;
		case 0x7d:		// IMASK
			UML_MOV(block, mem(&m_core->imask), imm ? data : I0);
			break;
		case 0x7e:		// STKY
			UML_MOV(block, mem(&m_core->stky), imm ? data : I0);
			break;
		case 0xdc:		// PX1
			if (imm)
			{
				UML_DAND(block, mem(&m_core->px), mem(&m_core->px), ~0xffffffff);
				UML_DOR(block, mem(&m_core->px), mem(&m_core->px), (UINT64)(data));
			}
			else
			{
				UML_DAND(block, mem(&m_core->px), mem(&m_core->px), ~0xffffffff);
				UML_DXOR(block, I1, I1, I1);
				UML_MOV(block, I1, I0);
				UML_DOR(block, mem(&m_core->px), mem(&m_core->px), I1);
			}
			break;
		case 0xdd:		// PX2
			if (imm)
			{
				UML_DAND(block, mem(&m_core->px), mem(&m_core->px), 0xffffffff);
				UML_DOR(block, mem(&m_core->px), mem(&m_core->px), (UINT64)(data) << 32);
			}
			else
			{
				UML_DAND(block, mem(&m_core->px), mem(&m_core->px), 0xffffffff);				
				UML_DSHL(block, I0, I0, 32);
				UML_DOR(block, mem(&m_core->px), mem(&m_core->px), I0);
			}
			break;

		default:
			fatalerror("generate_write_ureg %02X", ureg);
			break;
	}
}

int adsp21062_device::generate_opcode(drcuml_block *block, compiler_state *compiler, const opcode_desc *desc)
{
	UINT64 opcode = desc->opptr.q[0];

	switch ((opcode >> 45) & 7)
	{
		case 0:				// subops
		{
			UINT32 subop = (opcode >> 40) & 0x1f;
			switch (subop)
			{
				case 0x00:			// NOP / idle						|000|00000|
					if (opcode & U64(0x008000000000))
					{
						// IDLE
						UML_MOV(block, mem(&m_core->idle), 1);
						UML_MOV(block, mem(&m_core->icount), 0);
						return TRUE;
					}
					else
					{
						// NOP
						return TRUE;
					}
					break;

				case 0x01:			// compute								|000|00001|				
				{
					int cond = (opcode >> 33) & 0x1f;

					bool has_condition = !if_condition_always_true(cond);
					int skip_label = 0;

					if (has_condition)
					{
						skip_label = compiler->labelnum++;
						generate_if_condition(block, compiler, desc, cond, skip_label);
					}
					if (generate_compute(block, compiler, desc) == FALSE)
						return FALSE;
					if (has_condition)
						UML_LABEL(block, skip_label);
					return TRUE;
				}

				case 0x02:			// immediate shift						|000|00010|
				{
					int shiftop = (opcode >> 16) & 0x3f;
					int rn = (opcode >> 4) & 0xf;
					int rx = (opcode & 0xf);
					int cond = (opcode >> 33) & 0x1f;
					int data = ((opcode >> 8) & 0xff) | ((opcode >> 19) & 0xf00);

					bool has_condition = !if_condition_always_true(cond);
					int skip_label = 0;

					if (has_condition)
					{
						skip_label = compiler->labelnum++;
						generate_if_condition(block, compiler, desc, cond, skip_label);
					}
					
					if (generate_shift_imm(block, compiler, desc, data, shiftop, rn, rx) == FALSE)
						return FALSE;
					
					if (has_condition)					
						UML_LABEL(block, skip_label);			
					return TRUE;
				}

				case 0x04:			// compute / modify						|000|00100|
				{
					return FALSE;
				}

				case 0x06:			// direct jump|call						|000|00110|
				{
					int b = (opcode >> 39) & 0x1;
					int j = (opcode >> 26) & 0x1;
					int la = (opcode >> 38) & 0x1;
					int ci = (opcode >> 24) & 0x1;
					int cond = (opcode >> 33) & 0x1f;
					UINT32 address = opcode & 0xffffff;

					bool has_condition = !if_condition_always_true(cond);
					int skip_label = 0;

					if (has_condition)
					{
						skip_label = compiler->labelnum++;
						generate_if_condition(block, compiler, desc, cond, skip_label);
					}
					if (b) // call
					{
						generate_call(block, compiler, desc, j != 0);						
					}
					else // jump
					{
						generate_jump(block, compiler, desc, j != 0, la != 0, ci != 0);
					}
					if (has_condition)
						UML_LABEL(block, skip_label);
					return TRUE;
				}

				case 0x07:			// direct jump|call						|000|00111|
				{
					return FALSE;
				}

				case 0x08:			// indirect jump|call / compute			|000|01000|
				{
					return FALSE;
				}

				case 0x09:			// indirect jump|call / compute			|000|01001|
				{
					int la = (opcode >> 38) & 0x1;
					int ci = (opcode >> 24) & 0x1;
					int b = (opcode >> 39) & 0x1;
					int j = (opcode >> 26) & 0x1;
					int e = (opcode >> 25) & 0x1;
					int cond = (opcode >> 33) & 0x1f;
					int compute = opcode & 0x7fffff;

					if (e)
					{
						// IF ... ELSE

						int label_if = compiler->labelnum++;
						int label_else = compiler->labelnum++;
						generate_if_condition(block, compiler, desc, cond, label_if);
						UML_JMP(block, label_else);

						UML_LABEL(block, label_if);
						if (b) // call
						{
							generate_call(block, compiler, desc, j != 0);
						}
						else // jump
						{
							generate_jump(block, compiler, desc, j != 0, la != 0, ci != 0);
						}
						
						UML_LABEL(block, label_else);
						if (generate_compute(block, compiler, desc) == FALSE)
							return FALSE;
					}
					else
					{
						// IF
						bool has_condition = !if_condition_always_true(cond);
						int skip_label = 0;

						if (has_condition)
						{
							skip_label = compiler->labelnum++;
							generate_if_condition(block, compiler, desc, cond, skip_label);
						}
						if (generate_compute(block, compiler, desc) == FALSE)
							return FALSE;

						if (b) // call
						{
							generate_call(block, compiler, desc, j != 0);
						}
						else // jump
						{
							generate_jump(block, compiler, desc, j != 0, la != 0, ci != 0);
						}

						if (has_condition)
							UML_LABEL(block, skip_label);
					}

					return TRUE;
				}

				case 0x0a:			// return from subroutine / compute		|000|01010|
				{
					return FALSE;
				}

				case 0x0b:			// return from interrupt / compute		|000|01011|
				{
					return FALSE;
				}

				case 0x0c:			// do until counter expired				|000|01100|
				{
					UINT16 data = (UINT16)(opcode >> 24);
					int offset = SIGN_EXTEND24(opcode & 0xffffff);
					UINT32 address = desc->pc + offset;

					UML_MOV(block, LCNTR, data);
					if (data > 0)
					{
						// TODO: insert loop check at target address

						// push pc
						UML_MOV(block, I0, desc->pc + 1);
						UML_CALLH(block, *m_push_pc);

						// push loop						
						UML_MOV(block, I0, address);
						UML_MOV(block, I1, data);
						// TODO: I2 = type/condition?
						UML_CALLH(block, *m_push_loop);
					}
					return TRUE;
				}

				case 0x0d:			// do until counter expired				|000|01101|
				{
					return FALSE;
				}

				case 0x0e:			// do until								|000|01110|
				{
					return FALSE;
				}

				case 0x0f:			// immediate data -> ureg				|000|01111|
				{
					int ureg = (opcode >> 32) & 0xff;
					UINT32 data = (UINT32)opcode;

					generate_write_ureg(block, compiler, desc, ureg, true, data);
					return TRUE;
				}

				case 0x10:			// ureg <-> DM|PM (direct)				|000|100|G|D|
				case 0x11:
				case 0x12:
				case 0x13:
				{
					int ureg = (opcode >> 32) & 0xff;
					UINT32 address = (UINT32)(opcode);
					int d = (opcode >> 40) & 1;
					int g = (opcode >> 41) & 1;

					UML_MOV(block, I1, address);
					if (d)
					{
						generate_read_ureg(block, compiler, desc, ureg);

						// write
						if (g)
						{
							// PM
							if (ureg == 0xdb)	// PX is 48-bit
								UML_CALLH(block, *m_pm_write48);
							else
								UML_CALLH(block, *m_pm_write32);
						}
						else
						{
							// DM
							UML_CALLH(block, *m_dm_write32);
						}
					}
					else
					{
						// read
						if (g)
						{
							// PM
							if (ureg == 0xdb)	// PX is 48-bit
								UML_CALLH(block, *m_pm_read48);
							else
								UML_CALLH(block, *m_pm_read32);
						}
						else
						{
							// DM
							UML_CALLH(block, *m_dm_read32);
						}

						generate_write_ureg(block, compiler, desc, ureg, false, 0);
					}
					return TRUE;
				}

				case 0x14:			// system register bit manipulation		|000|10100|
				{
					int bop = (opcode >> 37) & 0x7;
					int sreg = (opcode >> 32) & 0xf;

					switch (bop)
					{
					case 0:		// SET
						break;
					case 1:		// CLEAR
						break;
					case 2:		// TOGGLE
						break;
					case 4:		// TEST
						break;
					case 5:		// XOR
						break;

					default:
						return FALSE;
					}
					return FALSE;
				}

				case 0x16:			// I register modify / bit-reverse		|000|10110|
				{
					if (opcode & U64(0x008000000000))	// bit reverse
					{
						return FALSE;
					}
					else			// modify
					{
						return FALSE;
					}
				}

				case 0x17:			// push|pop stacks / flush cache		|000|10111|					
					return FALSE;

				case 0x18:			// cjump								|000|11000|
					return FALSE;

				case 0x19:			// rframe								|000|11001|
					return FALSE;

				default:
					return FALSE;
			}
			break;
		}

		case 1:				// compute / dreg <-> DM / dreg <-> PM									|001|
		{
			return FALSE;
		}

		case 2:				// compute / ureg <-> DM|PM, register modify							|010|
		{
			return FALSE;
		}

		case 3:
		{
			if (opcode & U64(0x100000000000))	// compute / ureg <-> ureg							|011|1|
			{
				int src_ureg = (opcode >> 36) & 0xff;
				int dst_ureg = (opcode >> 23) & 0xff;
				int cond = (opcode >> 31) & 0x1f;
				int compute = opcode & 0x7fffff;

				bool has_condition = !if_condition_always_true(cond);
				bool src_is_dreg = (src_ureg >= 0 && src_ureg < 16);
				int skip_label = 0;

				if (has_condition)
				{
					skip_label = compiler->labelnum++;
					generate_if_condition(block, compiler, desc, cond, skip_label);
				}

				bool temp_ureg = false;
				// save UREG if compute writes to it
				if (compute != 0 && src_is_dreg && desc->regout[0] & (1 << (src_ureg & 0xf)))
				{
					UML_MOV(block, mem(&m_core->dreg_temp), REG(src_ureg & 0xf));
					temp_ureg = true;
				}

				if (generate_compute(block, compiler, desc) == FALSE)
					return FALSE;

				if (temp_ureg)
				{
					UML_MOV(block, I0, mem(&m_core->dreg_temp));
				}
				else
				{
					generate_read_ureg(block, compiler, desc, src_ureg);
				}
				generate_write_ureg(block, compiler, desc, dst_ureg, false, 0);

				if (has_condition)
					UML_LABEL(block, skip_label);

				return TRUE;
			}
			else								// compute / dreg <-> DM|PM, immediate modify		|011|0|
			{
				int cond = (opcode >> 33) & 0x1f;
				int u = (opcode >> 38) & 0x1;
				int d = (opcode >> 39) & 0x1;
				int g = (opcode >> 40) & 0x1;
				int dreg = (opcode >> 23) & 0xf;
				int i = (opcode >> 41) & 0x7;
				int mod = SIGN_EXTEND6((opcode >> 27) & 0x3f);
				int compute = opcode & 0x7fffff;

				bool has_condition = !if_condition_always_true(cond);
				int skip_label = 0;

				if (has_condition)
				{
					skip_label = compiler->labelnum++;
					generate_if_condition(block, compiler, desc, cond, skip_label);
				}
				if (d)		
				{
					// DREG -> DM|PM
					bool temp_dreg = false;

					// save dreg if compute writes to it
					if (compute != 0 && desc->regout[0] & (1 << dreg))
					{
						UML_MOV(block, mem(&m_core->dreg_temp), REG(dreg));
						temp_dreg = true;
					}
					// compute
					if (generate_compute(block, compiler, desc) == FALSE)
						return FALSE;

					// transfer
					UML_MOV(block, I1, (g) ? PM_I(i) : DM_I(i));			// mov    i1,dm|pm[i]
					if (u == 0)	// pre-modify without update
						UML_ADD(block, I1, I1, mod);						// add    i1,i1,mod
					if (temp_dreg)
						UML_MOV(block, I0, mem(&m_core->dreg_temp));		// mov    i0,[m_core->dreg_temp]
					else
						UML_MOV(block, I0, REG(dreg));						// mov    i0,reg[dreg]
					UML_CALLH(block, (g) ? *m_pm_write32 : *m_dm_write32);	// callh  dm|pm_write32
				}
				else
				{
					// DM|PM -> DREG

					// compute
					if (generate_compute(block, compiler, desc) == FALSE)
						return FALSE;

					// transfer
					UML_MOV(block, I1, (g) ? PM_I(i) : DM_I(i));			// mov    i1,dm|pm[i]
					if (u == 0)	// pre-modify without update
						UML_ADD(block, I1, I1, mod);						// add    i1,i1,mod
					UML_CALLH(block, (g) ? *m_pm_read32 : *m_dm_read32);	// callh  dm|pm_read32
					UML_MOV(block, REG(dreg), I0);							// mov    reg[dreg],i0
				}

				if (u != 0)		// post-modify with update
				{
					if (g)
						UML_ADD(block, PM_I(i), PM_I(i), mod);				// add    pm[i],mod
					else
						UML_ADD(block, DM_I(i), DM_I(i), mod);				// add    dm[i],mod					

					// TODO: update circular buffer
				}
				if (has_condition)
					UML_LABEL(block, skip_label);

				return TRUE;
			}
			break;
		}

		case 4:
		{
			if (opcode & U64(0x100000000000))	// immediate data -> DM|PM							|100|1|
			{
				return FALSE;
			}
			else								// immediate shift / dreg <-> DM|PM					|100|0|
			{
				int i = (opcode >> 41) & 0x7;
				int m = (opcode >> 38) & 0x7;
				int g = (opcode >> 32) & 0x1;
				int d = (opcode >> 31) & 0x1;
				int dreg = (opcode >> 23) & 0xf;
				int cond = (opcode >> 33) & 0x1f;
				int data = ((opcode >> 8) & 0xff) | ((opcode >> 19) & 0xf00);
				int shiftop = (opcode >> 16) & 0x3f;
				int rn = (opcode >> 4) & 0xf;
				int rx = (opcode & 0xf);

				bool has_condition = !if_condition_always_true(cond);
				int skip_label = 0;

				if (has_condition)
				{
					skip_label = compiler->labelnum++;
					generate_if_condition(block, compiler, desc, cond, skip_label);
				}
				if (d)
				{
					// DREG -> DM|PM
					bool temp_dreg = false;

					// save dreg if shiftop writes to it
					if (desc->regout[0] & (1 << dreg))
					{
						UML_MOV(block, mem(&m_core->dreg_temp), REG(dreg));
						temp_dreg = true;
					}
					// shiftop
					if (generate_shift_imm(block, compiler, desc, data, shiftop, rn, rx) == FALSE)
						return FALSE;

					// transfer
					UML_MOV(block, I1, (g) ? PM_I(i) : DM_I(i));			// mov    i1,dm|pm[i]
					if (temp_dreg)
						UML_MOV(block, I0, mem(&m_core->dreg_temp));		// mov    i0,[m_core->dreg_temp]
					else
						UML_MOV(block, I0, REG(dreg));						// mov    i0,reg[dreg]
					UML_CALLH(block, (g) ? *m_pm_write32 : *m_dm_write32);	// callh  dm|pm_write32
				}
				else
				{
					// DM|PM -> DREG

					// shiftop
					if (generate_shift_imm(block, compiler, desc, data, shiftop, rn, rx) == FALSE)
						return FALSE;

					// transfer
					UML_MOV(block, I1, (g) ? PM_I(i) : DM_I(i));			// mov    i1,dm|pm[i]
					UML_CALLH(block, (g) ? *m_pm_read32 : *m_dm_read32);	// callh  dm|pm_read32
					UML_MOV(block, REG(dreg), I0);							// mov    reg[dreg],i0
				}

				// update I
				if (g)
					UML_ADD(block, PM_I(i), PM_I(i), PM_M(m));
				else
					UML_ADD(block, DM_I(i), DM_I(i), DM_M(m));

				// TODO: update circular buffer

				if (has_condition)
					UML_LABEL(block, skip_label);

				return TRUE;
			}
			break;
		}

		case 5:								// ureg <-> DM|PM (indirect)							|101|
		{
			return FALSE;
		}

		case 6:								// indirect jump / compute / dreg <-> DM				|110|
		{
			return FALSE;
		}

		case 7:								// indirect jump / compute / dreg <-> DM				|111|
		{
			return FALSE;
		}
	}

	return FALSE;
}

int adsp21062_device::generate_compute(drcuml_block *block, compiler_state *compiler, const opcode_desc *desc)
{
	UINT64 opcode = desc->opptr.q[0];
	int rs = (opcode >> 12) & 0xf;
	int rn = (opcode >> 8) & 0xf;
	int ra = rn;
	int rx = (opcode >> 4) & 0xf;
	int ry = (opcode >> 0) & 0xf;

	if (opcode & 0x400000)		// multi-function operation
	{
		UINT32 multiop = (opcode >> 16) & 0x3f;
		int fm = rs;
		int fa = rn;
		int fxm = (opcode >> 6) & 0x3;          // registers 0 - 3
		int fym = ((opcode >> 4) & 0x3) + 4;    // registers 4 - 7
		int fxa = ((opcode >> 2) & 0x3) + 8;    // registers 8 - 11
		int fya = (opcode & 0x3) + 12;          // registers 12 - 15

		switch (multiop)
		{
			case 0x07:			// Ra = Rx + Ry,   Rs = Rx - Ry
			case 0x0f:			// Fa = Fx + Fy,   Fs = Fx - Fy
				return FALSE;

			case 0x04:			// Rm = R3-0 * R7-4 (SSFR),   Ra = R11-8 + R15-12
			case 0x05:			// Rm = R3-0 * R7-4 (SSFR),   Ra = R11-8 - R15-12
			case 0x06:			// Rm = R3-0 * R7-4 (SSFR),   Ra = (R11-8 + R15-12) / 2
				return FALSE;

			case 0x08:			// MRF = MRF + R3-0 * R7-4 (SSF),   Ra = R11-8 + R15-12
			case 0x09:			// MRF = MRF + R3-0 * R7-4 (SSF),   Ra = R11-8 - R15-12
			case 0x0a:			// MRF = MRF + R3-0 * R7-4 (SSF),   Ra = (R11-8 + R15-12) / 2
				break;

			case 0x0c:			// Rm = MRF + R3-0 * R7-4 (SSFR),   Ra = R11-8 + R15-12
			case 0x0d:			// Rm = MRF + R3-0 * R7-4 (SSFR),   Ra = R11-8 - R15-12
			case 0x0e:			// Rm = MRF + R3-0 * R7-4 (SSFR),   Ra = (R11-8 + R15-12) / 2
				return FALSE;

			case 0x10:			// MRF = MRF - R3-0 * R7-4 (SSF),   Ra = R11-8 + R15-12
			case 0x11:			// MRF = MRF - R3-0 * R7-4 (SSF),   Ra = R11-8 - R15-12
			case 0x12:			// MRF = MRF - R3-0 * R7-4 (SSF),   Ra = (R11-8 + R15-12) / 2
				return FALSE;

			case 0x14:			// Rm = MRF - R3-0 * R7-4 (SSFR),   Ra = R11-8 + R15-12
			case 0x15:			// Rm = MRF - R3-0 * R7-4 (SSFR),   Ra = R11-8 - R15-12
			case 0x16:			// Rm = MRF - R3-0 * R7-4 (SSFR),   Ra = (R11-8 + R15-12) / 2
				return FALSE;

			case 0x18:			// Fm = F3-0 * F7-4,   Fa = F11-8 + F15-12
			case 0x19:			// Fm = F3-0 * F7-4,   Fa = F11-8 - F15-12
			case 0x1a:			// Fm = F3-0 * F7-4,   Fa = FLOAT F11-8 BY R15-12
			case 0x1b:			// Fm = F3-0 * F7-4,   Fa = FIX F11-8 BY R15-12
			case 0x1c:			// Fm = F3-0 * F7-4,   Fa = (F11-8 + F15-12) / 2
			case 0x1e:			// Fm = F3-0 * F7-4,   Fa = MAX(F11-8, F15-12)
			case 0x1f:			// Fm = F3-0 * F7-4,   Fa = MIN(F11-8, F15-12)
				return FALSE;

			case 0x1d:			// Fm = F3-0 * F7-4,   Fa = ABS F11-8
				return FALSE;

			case 0x20: case 0x21: case 0x22: case 0x23: case 0x24: case 0x25: case 0x26: case 0x27:
			case 0x28: case 0x29: case 0x2a: case 0x2b: case 0x2c: case 0x2d: case 0x2e: case 0x2f:
				// Rm = R3-0 * R7-4 (SSFR),   Ra = R11-8 + R15-12,   Rs = R11-8 - R15-12
				return FALSE;

			case 0x30: case 0x31: case 0x32: case 0x33: case 0x34: case 0x35: case 0x36: case 0x37:
			case 0x38: case 0x39: case 0x3a: case 0x3b: case 0x3c: case 0x3d: case 0x3e: case 0x3f:
				// Fm = F3-0 * F7-4,   Fa = F11-8 + F15-12,   Fs = F11-8 - F15-12
				return FALSE;

			default:
				return FALSE;
		}
	}
	else							// single-function operation
	{
		UINT32 operation = (opcode >> 12) & 0xff;

		switch ((opcode >> 20) & 3)
		{
			case 0:				// ALU operations
			{
				switch (operation)
				{
					case 0x01:		// Rn = Rx + Ry
					case 0x02:		// Rn = Rx - Ry
					case 0x09:		// Rn = (Rx + Ry) / 2
					case 0x61:		// Rn = MIN(Rx, Ry)
					case 0x62:		// Rn = MAX(Rx, Ry)
					case 0x63:		// Rn = CLIP Rx BY Ry
					case 0x81:		// Fn = Fx + Fy
					case 0x82:		// Fn = Fx - Fy
					case 0x91:		// Fn = ABS(Fx + Fy)
					case 0x92:		// Fn = ABS(Fx - Fy)
					case 0x89:		// Fn = (Fx + Fy) / 2
					case 0xbd:		// Fn = SCALB Fx BY Ry
					case 0xd9:		// Rn = FIX Fx BY Ry
					case 0xdd:		// Rn = TRUNC Fx BY Ry
					case 0xda:		// Fn = FLOAT Rx BY Ry
					case 0xe1:		// Fn = MIN(Fx, Fy)
					case 0xe2:		// Fn = MAX(Fx, Fy)
					case 0xe3:		// Fn = CLIP Fx BY Fy
					case 0xe0:		// Fn = Fx COPYSIGN Fy
					case 0x05:		// Rn = Rx + Ry + CI
					case 0x06:		// Rn = Rx - Ry + CI - 1
					case 0x25:		// Rn = Rx + CI
					case 0x26:		// Rn = Rx + CI - 1
					case 0x8a:		// COMP(Fx, Fy)
					case 0x29:		// Rn = Rx + 1
					case 0x2a:		// Rn = Rx - 1
					case 0x22:		// Rn = -Rx
					case 0x30:		// Rn = ABS Rx
					case 0x21:		// Rn = PASS Rx
					case 0x43:		// Rn = NOT Rx
					case 0xb0:		// Fn = ABS(Fx)
					case 0xa1:		// Fn = PASS Fx
					case 0xa5:		// Fn = RND Fx
					case 0xad:		// Rn = MANT Fx
					case 0xc1:		// Rn = LOGB Fx
					case 0xc9:		// Rn = FIX Fx
					case 0xcd:		// Rn = TRUNC Fx
					case 0xca:		// Fn = FLOAT Rx
					case 0xc4:		// Fn = RECIPS Fx
					case 0xc5:		// Fn = RSQRTS Fx
						return FALSE;

					case 0x0a:		// COMP(Rx, Ry)
						UML_CMP(block, REG(rx), REG(ry));
						if (AZ_CALC_REQUIRED) UML_SETc(block, COND_Z, ASTAT_AZ);
						if (AN_CALC_REQUIRED) UML_SETc(block, COND_L, ASTAT_AN);
						if (AV_CALC_REQUIRED) UML_MOV(block, ASTAT_AV, 0);
						if (AC_CALC_REQUIRED) UML_MOV(block, ASTAT_AC, 0);
						if (AS_CALC_REQUIRED) UML_MOV(block, ASTAT_AS, 0);
						if (AI_CALC_REQUIRED) UML_MOV(block, ASTAT_AI, 0);
						return TRUE;

					case 0x40:		// Rn = Rx AND Ry
						UML_AND(block, REG(rn), REG(rx), REG(ry));
						if (AZ_CALC_REQUIRED) UML_SETc(block, COND_Z, ASTAT_AZ);
						if (AN_CALC_REQUIRED) UML_SETc(block, COND_S, ASTAT_AN);
						if (AV_CALC_REQUIRED) UML_MOV(block, ASTAT_AV, 0);
						if (AC_CALC_REQUIRED) UML_MOV(block, ASTAT_AC, 0);
						if (AS_CALC_REQUIRED) UML_MOV(block, ASTAT_AS, 0);
						if (AI_CALC_REQUIRED) UML_MOV(block, ASTAT_AI, 0);
						return TRUE;

					case 0x41:		// Rn = Rx OR Ry
						UML_OR(block, REG(rn), REG(rx), REG(ry));
						if (AZ_CALC_REQUIRED) UML_SETc(block, COND_Z, ASTAT_AZ);
						if (AN_CALC_REQUIRED) UML_SETc(block, COND_S, ASTAT_AN);
						if (AV_CALC_REQUIRED) UML_MOV(block, ASTAT_AV, 0);
						if (AC_CALC_REQUIRED) UML_MOV(block, ASTAT_AC, 0);
						if (AS_CALC_REQUIRED) UML_MOV(block, ASTAT_AS, 0);
						if (AI_CALC_REQUIRED) UML_MOV(block, ASTAT_AI, 0);
						return TRUE;

					case 0x42:		// Rn = Rx XOR Ry
						UML_XOR(block, REG(rn), REG(rx), REG(ry));
						if (AZ_CALC_REQUIRED) UML_SETc(block, COND_Z, ASTAT_AZ);
						if (AN_CALC_REQUIRED) UML_SETc(block, COND_S, ASTAT_AN);
						if (AV_CALC_REQUIRED) UML_MOV(block, ASTAT_AV, 0);
						if (AC_CALC_REQUIRED) UML_MOV(block, ASTAT_AC, 0);
						if (AS_CALC_REQUIRED) UML_MOV(block, ASTAT_AS, 0);
						if (AI_CALC_REQUIRED) UML_MOV(block, ASTAT_AI, 0);
						return TRUE;

					default:
						return FALSE;
				}
				break;
			}

			case 1:				// multiplier operations
			{
				switch (operation)
				{
					case 0x40:		// Rn = Rx * Ry (UUI)
					case 0x48:		// Rn = Rx * Ry (UUF)
					case 0x49:		// Rn = Rx * Ry (UUFR)
					case 0x50:		// Rn = Rx * Ry (SUI)
					case 0x58:		// Rn = Rx * Ry (SUF)
					case 0x59:		// Rn = Rx * Ry (SUFR)
					case 0x60:		// Rn = Rx * Ry (USI)
					case 0x68:		// Rn = Rx * Ry (USF)
					case 0x69:		// Rn = Rx * Ry (USFR)
					case 0x70:		// Rn = Rx * Ry (SSI)
					case 0x78:		// Rn = Rx * Ry (SSF)
					case 0x79:		// Rn = Rx * Ry (SSFR)
					case 0x44:		// MRF = Rx * Ry (UUI)
					case 0x4c:		// MRF = Rx * Ry (UUF)
					case 0x4d:		// MRF = Rx * Ry (UUFR)
					case 0x54:		// MRF = Rx * Ry (SUI)
					case 0x5c:		// MRF = Rx * Ry (SUF)
					case 0x5d:		// MRF = Rx * Ry (SUFR)
					case 0x64:		// MRF = Rx * Ry (USI)
					case 0x6c:		// MRF = Rx * Ry (USF)
					case 0x6d:		// MRF = Rx * Ry (USFR)
					case 0x74:		// MRF = Rx * Ry (SSI)
					case 0x7c:		// MRF = Rx * Ry (SSF)
					case 0x7d:		// MRF = Rx * Ry (SSFR)
					case 0x46:		// MRB = Rx * Ry (UUI)
					case 0x4e:		// MRB = Rx * Ry (UUF)
					case 0x4f:		// MRB = Rx * Ry (UUFR)
					case 0x56:		// MRB = Rx * Ry (SUI)
					case 0x5e:		// MRB = Rx * Ry (SUF)
					case 0x5f:		// MRB = Rx * Ry (SUFR)
					case 0x66:		// MRB = Rx * Ry (USI)
					case 0x6e:		// MRB = Rx * Ry (USF)
					case 0x6f:		// MRB = Rx * Ry (USFR)
					case 0x76:		// MRB = Rx * Ry (SSI)
					case 0x7e:		// MRB = Rx * Ry (SSF)
					case 0x7f:		// MRB = Rx * Ry (SSFR)
					case 0x80:		// Rn = MRF + Rx * Ry (UUI)
					case 0x88:		// Rn = MRF + Rx * Ry (UUF)
					case 0x89:		// Rn = MRF + Rx * Ry (UUFR)
					case 0x90:		// Rn = MRF + Rx * Ry (SUI)
					case 0x98:		// Rn = MRF + Rx * Ry (SUF)
					case 0x99:		// Rn = MRF + Rx * Ry (SUFR)
					case 0xa0:		// Rn = MRF + Rx * Ry (USI)
					case 0xa8:		// Rn = MRF + Rx * Ry (USF)
					case 0xa9:		// Rn = MRF + Rx * Ry (USFR)
					case 0xb0:		// Rn = MRF + Rx * Ry (SSI)
					case 0xb8:		// Rn = MRF + Rx * Ry (SSF)
					case 0xb9:		// Rn = MRF + Rx * Ry (SSFR)
					case 0x82:		// Rn = MRB + Rx * Ry (UUI)
					case 0x8a:		// Rn = MRB + Rx * Ry (UUF)
					case 0x8b:		// Rn = MRB + Rx * Ry (UUFR)
					case 0x92:		// Rn = MRB + Rx * Ry (SUI)
					case 0x9a:		// Rn = MRB + Rx * Ry (SUF)
					case 0x9b:		// Rn = MRB + Rx * Ry (SUFR)
					case 0xa2:		// Rn = MRB + Rx * Ry (USI)
					case 0xaa:		// Rn = MRB + Rx * Ry (USF)
					case 0xab:		// Rn = MRB + Rx * Ry (USFR)
					case 0xb2:		// Rn = MRB + Rx * Ry (SSI)
					case 0xba:		// Rn = MRB + Rx * Ry (SSF)
					case 0xbb:		// Rn = MRB + Rx * Ry (SSFR)
					case 0x84:		// MRF = MRF + Rx * Ry (UUI)
					case 0x8c:		// MRF = MRF + Rx * Ry (UUF)
					case 0x8d:		// MRF = MRF + Rx * Ry (UUFR)
					case 0x94:		// MRF = MRF + Rx * Ry (SUI)
					case 0x9c:		// MRF = MRF + Rx * Ry (SUF)
					case 0x9d:		// MRF = MRF + Rx * Ry (SUFR)
					case 0xa4:		// MRF = MRF + Rx * Ry (USI)
					case 0xac:		// MRF = MRF + Rx * Ry (USF)
					case 0xad:		// MRF = MRF + Rx * Ry (USFR)
					case 0xb4:		// MRF = MRF + Rx * Ry (SSI)
					case 0xbc:		// MRF = MRF + Rx * Ry (SSF)
					case 0xbd:		// MRF = MRF + Rx * Ry (SSFR)
					case 0x86:		// MRB = MRB + Rx * Ry (UUI)
					case 0x8e:		// MRB = MRB + Rx * Ry (UUF)
					case 0x8f:		// MRB = MRB + Rx * Ry (UUFR)
					case 0x96:		// MRB = MRB + Rx * Ry (SUI)
					case 0x9e:		// MRB = MRB + Rx * Ry (SUF)
					case 0x9f:		// MRB = MRB + Rx * Ry (SUFR)
					case 0xa6:		// MRB = MRB + Rx * Ry (USI)
					case 0xae:		// MRB = MRB + Rx * Ry (USF)
					case 0xaf:		// MRB = MRB + Rx * Ry (USFR)
					case 0xb6:		// MRB = MRB + Rx * Ry (SSI)
					case 0xbe:		// MRB = MRB + Rx * Ry (SSF)
					case 0xbf:		// MRB = MRB + Rx * Ry (SSFR)
					case 0xc0:		// Rn = MRF - Rx * Ry (UUI)
					case 0xc8:		// Rn = MRF - Rx * Ry (UUF)
					case 0xc9:		// Rn = MRF - Rx * Ry (UUFR)
					case 0xd0:		// Rn = MRF - Rx * Ry (SUI)
					case 0xd8:		// Rn = MRF - Rx * Ry (SUF)
					case 0xd9:		// Rn = MRF - Rx * Ry (SUFR)
					case 0xe0:		// Rn = MRF - Rx * Ry (USI)
					case 0xe8:		// Rn = MRF - Rx * Ry (USF)
					case 0xe9:		// Rn = MRF - Rx * Ry (USFR)
					case 0xf0:		// Rn = MRF - Rx * Ry (SSI)
					case 0xf8:		// Rn = MRF - Rx * Ry (SSF)
					case 0xf9:		// Rn = MRF - Rx * Ry (SSFR)
					case 0xc2:		// Rn = MRB - Rx * Ry (UUI)
					case 0xca:		// Rn = MRB - Rx * Ry (UUF)
					case 0xcb:		// Rn = MRB - Rx * Ry (UUFR)
					case 0xd2:		// Rn = MRB - Rx * Ry (SUI)
					case 0xda:		// Rn = MRB - Rx * Ry (SUF)
					case 0xdb:		// Rn = MRB - Rx * Ry (SUFR)
					case 0xe2:		// Rn = MRB - Rx * Ry (USI)
					case 0xea:		// Rn = MRB - Rx * Ry (USF)
					case 0xeb:		// Rn = MRB - Rx * Ry (USFR)
					case 0xf2:		// Rn = MRB - Rx * Ry (SSI)
					case 0xfa:		// Rn = MRB - Rx * Ry (SSF)
					case 0xfb:		// Rn = MRB - Rx * Ry (SSFR)
					case 0xc4:		// MRF = MRF - Rx * Ry (UUI)
					case 0xcc:		// MRF = MRF - Rx * Ry (UUF)
					case 0xcd:		// MRF = MRF - Rx * Ry (UUFR)
					case 0xd4:		// MRF = MRF - Rx * Ry (SUI)
					case 0xdc:		// MRF = MRF - Rx * Ry (SUF)
					case 0xdd:		// MRF = MRF - Rx * Ry (SUFR)
					case 0xe4:		// MRF = MRF - Rx * Ry (USI)
					case 0xec:		// MRF = MRF - Rx * Ry (USF)
					case 0xed:		// MRF = MRF - Rx * Ry (USFR)
					case 0xf4:		// MRF = MRF - Rx * Ry (SSI)
					case 0xfc:		// MRF = MRF - Rx * Ry (SSF)
					case 0xfd:		// MRF = MRF - Rx * Ry (SSFR)
					case 0xc6:		// MRB = MRB - Rx * Ry (UUI)
					case 0xce:		// MRB = MRB - Rx * Ry (UUF)
					case 0xcf:		// MRB = MRB - Rx * Ry (UUFR)
					case 0xd6:		// MRB = MRB - Rx * Ry (SUI)
					case 0xde:		// MRB = MRB - Rx * Ry (SUF)
					case 0xdf:		// MRB = MRB - Rx * Ry (SUFR)
					case 0xe6:		// MRB = MRB - Rx * Ry (USI)
					case 0xee:		// MRB = MRB - Rx * Ry (USF)
					case 0xef:		// MRB = MRB - Rx * Ry (USFR)
					case 0xf6:		// MRB = MRB - Rx * Ry (SSI)
					case 0xfe:		// MRB = MRB - Rx * Ry (SSF)
					case 0xff:		// MRB = MRB - Rx * Ry (SSFR)
					case 0x00:		// Rn = SAT MRF (UI)
					case 0x01:		// Rn = SAT MRF (SI)
					case 0x08:		// Rn = SAT MRF (UF)
					case 0x09:		// Rn = SAT MRF (SF)
					case 0x02:		// Rn = SAT MRB (UI)
					case 0x03:		// Rn = SAT MRB (SI)
					case 0x0a:		// Rn = SAT MRB (UF)
					case 0x0b:		// Rn = SAT MRB (SF)
					case 0x04:		// MRF = SAT MRF (UI)
					case 0x05:		// MRF = SAT MRF (SI)
					case 0x0c:		// MRF = SAT MRF (UF)
					case 0x0d:		// MRF = SAT MRF (SF)
					case 0x06:		// MRB = SAT MRB (UI)
					case 0x07:		// MRB = SAT MRB (SI)
					case 0x0e:		// MRB = SAT MRB (UF)
					case 0x0f:		// MRB = SAT MRB (SF)
					case 0x18:		// Rn = RND MRF (U)
					case 0x19:		// Rn = RND MRF (S)
					case 0x1a:		// Rn = RND MRB (U)
					case 0x1b:		// Rn = RND MRB (S)
					case 0x1c:		// MRF = RND MRF (U)
					case 0x1d:		// MRF = RND MRF (S)
					case 0x1e:		// MRB = RND MRB (U)
					case 0x1f:		// MRB = RND MRB (S)
					case 0x14:		// MRF = 0
					case 0x16:		// MRB = 0
					case 0x30:		// Fn = Fx * Fy
						return FALSE;

					default:
						return FALSE;
				}
				break;
			}

			case 2:				// shifter operations
			{
				switch (operation)
				{
					case 0x00:		// Rn = LSHIFT Rx BY Ry | <data8>
					case 0x04:		// Rn = ASHIFT Rx BY Ry | <data8>
					case 0x08:		// Rn = ROT Rx BY Ry | <data8>
					case 0xc4:		// Rn = BCLR Rx BY Ry | <data8>
					case 0xc0:		// Rn = BSET Rx BY Ry | <data8>
					case 0x44:		// Rn = FDEP Rx BY Ry | <bit6>:<len6>
					case 0x4c:		// Rn = FDEP Rx BY Ry | <bit6>:<len6> (SE)
					case 0x40:		// Rn = FEXT Rx BY Ry | <bit6>:<len6>
					case 0x48:		// Rn = FEXT Rx BY Ry | <bit6>:<len6> (SE)
					case 0x20:		// Rn = Rn OR LSHIFT Rx BY Ry | <data8>
					case 0x24:		// Rn = Rn OR ASHIFT Rx BY Ry | <data8>
					case 0x64:		// Rn = Rn OR FDEP Rx BY Ry | <bit6>:<len6>
					case 0x6c:		// Rn = Rn OR FDEP Rx BY Ry | <bit6>:<len6> (SE)
					case 0xcc:		// BTST Rx BY Ry | <data8>
					case 0x80:		// Rn = EXP Rx
					case 0x84:		// Rn = EXP Rx (EX)
					case 0x88:		// Rn = LEFTZ Rx
					case 0x8c:		// Rn = LEFTO Rx
					case 0x90:		// Rn = FPACK Fx
					case 0x94:		// Fn = FUNPACK Rx
						return FALSE;

					default:
						return FALSE;
				}
				break;
			}

			default:
				return FALSE;
		}
	}

	return TRUE;
}

void adsp21062_device::generate_if_condition(drcuml_block *block, compiler_state *compiler, const opcode_desc *desc, int condition, int skip_label)
{
	switch (condition)
	{
		case 0x00:                                    /* EQ */
			UML_TEST(block, ASTAT_AZ, 1);
			UML_JMPc(block, COND_E, skip_label);
			break;
		case 0x01:                                    /* LT */
			UML_TEST(block, ASTAT_AZ, 1);
			UML_JMPc(block, COND_NE, skip_label);
			UML_TEST(block, ASTAT_AN, 1);
			UML_JMPc(block, COND_E, skip_label);
			break;
		case 0x02:                                    /* LE */
			fatalerror("generate_if_condition 0x02"); // TODO
			break;
		case 0x03:                                    /* AC */
			UML_TEST(block, ASTAT_AC, 1);
			UML_JMPc(block, COND_E, skip_label);
			break;
		case 0x04:                                    /* AV */
			UML_TEST(block, ASTAT_AV, 1);
			UML_JMPc(block, COND_E, skip_label);
			break;
		case 0x05:                                    /* MV */
			UML_TEST(block, ASTAT_MV, 1);
			UML_JMPc(block, COND_E, skip_label);
			break;
		case 0x06:                                    /* MS */
			UML_TEST(block, ASTAT_MN, 1);
			UML_JMPc(block, COND_E, skip_label);
			break;
		case 0x07:                                    /* SV */
			UML_TEST(block, ASTAT_SV, 1);
			UML_JMPc(block, COND_E, skip_label);
			break;
		case 0x08:                                    /* SZ */
			UML_TEST(block, ASTAT_SZ, 1);
			UML_JMPc(block, COND_E, skip_label);
			break;
		case 0x09:                                    /* FLAG0 */
			UML_CMP(block, FLAG0, 0);
			UML_JMPc(block, COND_E, skip_label);
			break;
		case 0x0a:                                    /* FLAG1 */
			UML_CMP(block, FLAG1, 0);
			UML_JMPc(block, COND_E, skip_label);
			break;
		case 0x0b:                                    /* FLAG2 */
			UML_CMP(block, FLAG2, 0);
			UML_JMPc(block, COND_E, skip_label);
			break;
		case 0x0c:                                    /* FLAG3 */
			UML_CMP(block, FLAG3, 0);
			UML_JMPc(block, COND_E, skip_label);
			break;
		case 0x0d:                                    /* TF */
			UML_TEST(block, ASTAT_BTF, 1);
			UML_JMPc(block, COND_E, skip_label);
		case 0x0e:                                    /* BM */
			UML_JMP(block, skip_label);
		case 0x0f:                                    /* NOT LCE */
			UML_CMP(block, CURLCNTR, 1);
			UML_JMPc(block, COND_E, skip_label);
			break;
		case 0x10:                                    /* NOT EQUAL */
			UML_TEST(block, ASTAT_AZ, 1);
			UML_JMPc(block, COND_NE, skip_label);
			break;
		case 0x11:                                    /* GE */
			fatalerror("generate_if_condition 0x11"); // TODO
			break;
		case 0x12:                                    /* GT */
			fatalerror("generate_if_condition 0x12"); // TODO
			break;
		case 0x13:                                    /* NOT AC */
			UML_TEST(block, ASTAT_AC, 1);
			UML_JMPc(block, COND_NE, skip_label);
			break;
		case 0x14:                                    /* NOT AV */
			UML_TEST(block, ASTAT_AV, 1);
			UML_JMPc(block, COND_NE, skip_label);
			break;
		case 0x15:                                    /* NOT MV */
			UML_TEST(block, ASTAT_MV, 1);
			UML_JMPc(block, COND_NE, skip_label);
			break;
		case 0x16:                                    /* NOT MS */
			UML_TEST(block, ASTAT_MN, 1);
			UML_JMPc(block, COND_NE, skip_label);
			break;
		case 0x17:                                    /* NOT SV */
			UML_TEST(block, ASTAT_SV, 1);
			UML_JMPc(block, COND_NE, skip_label);
			break;
		case 0x18:                                    /* NOT SZ */
			UML_TEST(block, ASTAT_SZ, 1);
			UML_JMPc(block, COND_NE, skip_label);
			break;
		case 0x19:                                    /* NOT FLAG0 */
			UML_CMP(block, FLAG0, 0);
			UML_JMPc(block, COND_NE, skip_label);
			break;
		case 0x1a:                                    /* NOT FLAG1 */
			UML_CMP(block, FLAG1, 0);
			UML_JMPc(block, COND_NE, skip_label);
			break;
		case 0x1b:                                    /* NOT FLAG2 */
			UML_CMP(block, FLAG2, 0);
			UML_JMPc(block, COND_NE, skip_label);
			break;
		case 0x1c:                                    /* NOT FLAG3 */
			UML_CMP(block, FLAG3, 0);
			UML_JMPc(block, COND_NE, skip_label);
			break;
		case 0x1d:                                    /* NOT TF */
			UML_TEST(block, ASTAT_BTF, 1);
			UML_JMPc(block, COND_NE, skip_label);
			break;
		case 0x1e:                                    /* NOT BM */
			fatalerror("generate_if_condition 0x1e"); // should not happen
			break;
		case 0x1f:                                    /* TRUE */
			fatalerror("generate_if_condition 0x1f"); // should not happen
			break;
	}
}

int adsp21062_device::generate_shift_imm(drcuml_block *block, compiler_state *compiler, const opcode_desc *desc, int data, int shiftop, int rn, int rx)
{
	INT8 shift = data & 0xff;

	switch (shiftop)
	{
		case 0x00:		// LSHIFT Rx BY <data8>
		case 0x01:		// ASHIFT Rx BY <data8>		
		case 0x10:		// FEXT Rx BY <data8>
		case 0x11:		// FDEP Rx BY <data8>
		case 0x12:		// FEXT Rx BY <bit6>:<len6> (SE)
		case 0x13:		// FDEP Rx BY <bit6>:<len6> (SE)
		case 0x30:		// BSET Rx BY <data8>
		case 0x31:		// BCLR Rx By <data8>		
		case 0x08:		// Rn = Rn OR LSHIFT Rx BY <data8>
		case 0x33:		// BTST Rx BY <data8>
			return FALSE;

		case 0x02:		// ROT Rx BY <data8>
			UML_ROL(block, REG(rn), REG(rx), (shift < 0) ? 31 - ((-shift) & 0x1f) : shift & 0x1f);			
			if (SZ_CALC_REQUIRED) UML_SETc(block, COND_Z, ASTAT_SZ);
			if (SV_CALC_REQUIRED) UML_MOV(block, ASTAT_SV, 0);
			if (SS_CALC_REQUIRED) UML_MOV(block, ASTAT_SS, 0);
			return TRUE;

		case 0x32:		// BTGL Rx BY <data8>
			UML_XOR(block, REG(rn), REG(rx), 1 << data);
			if (SZ_CALC_REQUIRED) UML_SETc(block, COND_Z, ASTAT_SZ);
			if (SV_CALC_REQUIRED && data > 31) UML_MOV(block, ASTAT_SV, 1);
			if (SS_CALC_REQUIRED) UML_MOV(block, ASTAT_SS, 0);
			return TRUE;

		default:
			return FALSE;
	}

	return TRUE;
}