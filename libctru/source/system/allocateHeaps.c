#include <3ds/types.h>
#include <3ds/svc.h>
#include <3ds/env.h>
#include <3ds/os.h>

extern char* fake_heap_start;
extern char* fake_heap_end;

u32 __ctru_heap;
u32 __ctru_heap_size;
u32 __ctru_linear_heap;
u32 __ctru_linear_heap_size;

void __attribute__((weak)) __system_allocateHeaps(void) {
	u32 tmp=0;

	// Distribute available memory into halves, aligning to page size.
	//u32 size = (osGetMemRegionFree(MEMREGION_SYSTEM) / 2) & 0xFFFFF000;
	__ctru_heap_size = 0x800000;//size;
	__ctru_linear_heap_size = 0x800000; // 

	//*(u32*)0x00100998 = size;


	// Allocate the application heap
	__ctru_heap = 0x08000000;
	svcControlMemory(&tmp, __ctru_heap, 0x0, __ctru_heap_size, MEMOP_ALLOC, MEMPERM_READ | MEMPERM_WRITE);

	// Allocate the linear heap
	//__ctru_linear_heap = 0x18000000;
	Result res = svcControlMemory(&__ctru_linear_heap, 0x0, 0x0, __ctru_linear_heap_size, MEMOP_ALLOC_LINEAR, MEMPERM_READ | MEMPERM_WRITE);
	if(res < 0) *(u32*)0x00100069 = res;
	if(__ctru_linear_heap < 0x08000000) *(u32*)0x00100070 = __ctru_linear_heap;

	// Set up newlib heap
	fake_heap_start = (char*)__ctru_heap;
	fake_heap_end = fake_heap_start + __ctru_heap_size;

}
