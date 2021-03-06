#include "stdafx.h"
#include "Emu/IdManager.h"
#include "Emu/Memory/Memory.h"

#include "SPUThread.h"
#include "SPURecompiler.h"
#include "SPUASMJITRecompiler.h"

extern u64 get_system_time();

spu_recompiler_base::~spu_recompiler_base()
{
}

void spu_recompiler_base::enter(SPUThread& spu)
{
	if (spu.pc >= 0x40000 || spu.pc % 4)
	{
		fmt::throw_exception("Invalid PC: 0x%05x", spu.pc);
	}

	// Get SPU LS pointer
	const auto _ls = vm::ps3::_ptr<u32>(spu.offset);

	// Always validate (TODO)
	const auto func = spu.spu_db->analyse(_ls, spu.pc);

	// Reset callstack if necessary
	if (func->does_reset_stack && spu.recursion_level)
	{
		spu.state += cpu_flag::ret;
		return;
	}

	if (!func->compiled)
	{
		if (!spu.spu_rec)
		{
			spu.spu_rec = fxm::get_always<spu_recompiler>();
		}

		spu.spu_rec->compile(*func);

		if (!func->compiled) fmt::throw_exception("Compilation failed" HERE);
	}

	const u32 res = func->compiled(&spu, _ls);

	if (const auto exception = spu.pending_exception)
	{
		spu.pending_exception = nullptr;
		std::rethrow_exception(exception);
	}

	if (res & 0x1000000)
	{
		spu.halt();
	}

	if (res & 0x2000000)
	{
	}

	if (res & 0x4000000)
	{
		if (res & 0x8000000)
		{
			fmt::throw_exception("Invalid interrupt status set (0x%x)" HERE, res);
		}

		spu.set_interrupt_status(true);
	}
	else if (res & 0x8000000)
	{
		spu.set_interrupt_status(false);
	}

	spu.pc = res & 0x3fffc;

	if ((spu.ch_event_stat & SPU_EVENT_INTR_TEST) > SPU_EVENT_INTR_ENABLED)
	{
		spu.ch_event_stat &= ~SPU_EVENT_INTR_ENABLED;
		spu.srr0 = std::exchange(spu.pc, 0);
	}
}
