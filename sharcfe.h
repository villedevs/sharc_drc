// license:BSD-3-Clause
// copyright-holders:Ville Linde

/******************************************************************************

    Front-end for SHARC recompiler

******************************************************************************/

#pragma once

#include "sharc.h"
#include "cpu/drcfe.h"

#ifndef __SHARCFE_H__
#define __SHARCFE_H__

class sharc_frontend : public drc_frontend
{
public:
	sharc_frontend(adsp21062_device *sharc, UINT32 window_start, UINT32 window_end, UINT32 max_sequence);

	enum UREG_ACCESS
	{
		UREG_READ,
		UREG_WRITE
	};

protected:
	// required overrides
	virtual bool describe(opcode_desc &desc, const opcode_desc *prev) override;

private:
	bool describe_compute(opcode_desc &desc, UINT64 opcode);
	bool describe_ureg_access(opcode_desc &desc, int reg, UREG_ACCESS access);
	bool describe_shiftop_imm(opcode_desc &desc, int shiftop, int rn, int rx);
	void describe_if_condition(opcode_desc &desc, int condition);

	adsp21062_device *m_sharc;
};

#endif /* __SHARCFE_H__ */
