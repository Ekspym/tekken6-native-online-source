// OWNER-SCOPED MENU ROWS FIX
#pragma once
#include <cstdint>
#include <rex/ppc/context.h>
uint32_t T6RowsSlotMap(uint32_t address, uint32_t owner);
uint32_t T6RowsMap(uint32_t address, uint32_t bank, uint32_t owner);
uint32_t T6RowsAdvance(uint32_t address, int32_t step, uint32_t owner);
uint32_t T6RowsRelated(uint32_t address, uint32_t bank, uint32_t owner);
uint32_t T6RowsCapacity(uint32_t owner);
void T6RowsInitialize(PPCContext& ctx, uint8_t* base, uint32_t owner);
void T6RowsDestroy(PPCContext& ctx, uint8_t* base, uint32_t owner);

uint64_t T6RowsGeneration();
