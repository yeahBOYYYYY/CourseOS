
#define _GNU_SOURCE

#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <err.h>
#include <sys/mman.h>

#include "os.h"

/* 2^20 pages ought to be enough for anybody */
#define NPAGES	(1024*1024)

static char* pages[NPAGES];

uint64_t alloc_page_frame(void)
{
	static uint64_t nalloc;
	uint64_t ppn;
	void* va;

	if (nalloc == NPAGES)
		errx(1, "out of physical memory");

	/* OS memory management isn't really this simple */
	ppn = nalloc;
	nalloc++;

	va = mmap(NULL, 1 << 13, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
	if (va == MAP_FAILED)
		err(1, "mmap failed");

	pages[ppn] = va;
	return ppn + 0xbaaaaaad;
}

void* phys_to_virt(uint64_t phys_addr)
{
	uint64_t ppn = (phys_addr >> 13) - 0xbaaaaaad;
	uint64_t off = phys_addr & 0x1fff;
	char* va = NULL;

	if (ppn < NPAGES)
		va = pages[ppn] + off;

	return va;
}

int main(int argc, char **argv)
{
    uint64_t pt = alloc_page_frame();

	assert(page_table_query(pt, 0xcafecafeeee) == NO_MAPPING);
	assert(page_table_query(pt, 0xfffecafeeee) == NO_MAPPING);
	assert(page_table_query(pt, 0xcafecafeeff) == NO_MAPPING);
	page_table_update(pt, 0xcafecafeeee, 0xf00d);
	assert(page_table_query(pt, 0xcafecafeeee) == 0xf00d);
	assert(page_table_query(pt, 0xfffecafeeee) == NO_MAPPING);
	assert(page_table_query(pt, 0xcafecafeeff) == NO_MAPPING);
	page_table_update(pt, 0xcafecafeeee, NO_MAPPING);
	assert(page_table_query(pt, 0xcafecafeeee) == NO_MAPPING);
	assert(page_table_query(pt, 0xfffecafeeee) == NO_MAPPING);
	assert(page_table_query(pt, 0xcafecafeeff) == NO_MAPPING);


    pt = alloc_page_frame();

    // Basic test: single mapping, removal
    assert(page_table_query(pt, 0xcafecafeeee) == NO_MAPPING);
    page_table_update(pt, 0xcafecafeeee, 0xf00d);
    assert(page_table_query(pt, 0xcafecafeeee) == 0xf00d);
    page_table_update(pt, 0xcafecafeeee, NO_MAPPING);
    assert(page_table_query(pt, 0xcafecafeeee) == NO_MAPPING);

    // Mapping multiple distinct VPNs to different PPNs
    page_table_update(pt, 0x1, 0xdead);
    page_table_update(pt, 0x2, 0xbeef);
    page_table_update(pt, 0x3, 0x1337);
    assert(page_table_query(pt, 0x1) == 0xdead);
    assert(page_table_query(pt, 0x2) == 0xbeef);
    assert(page_table_query(pt, 0x3) == 0x1337);

    // Remap existing VPN to new PPN
    page_table_update(pt, 0x2, 0xc0de);
    assert(page_table_query(pt, 0x2) == 0xc0de);

    // Remove mapping and verify
    page_table_update(pt, 0x3, NO_MAPPING);
    assert(page_table_query(pt, 0x3) == NO_MAPPING);

    // Edge VPN: lowest possible (0)
    page_table_update(pt, 0x0, 0xabc);
    assert(page_table_query(pt, 0x0) == 0xabc);

    // Edge VPN: largest 63-bit address (only bottom 63 bits used)
    uint64_t max_vpn = (1ULL << 50) - 1;
    page_table_update(pt, max_vpn, 0x789);
    assert(page_table_query(pt, max_vpn) == 0x789);

    // Mapping collisions with shared prefixes
    uint64_t vpn_base = 0x123456789AB;
    for (uint64_t i = 0; i < 10; ++i) {
        page_table_update(pt, vpn_base + i, 0x1000 + i);
    }
    for (uint64_t i = 0; i < 10; ++i) {
        assert(page_table_query(pt, vpn_base + i) == 0x1000 + i);
    }

    // Create mappings differing only at the last level
    for (uint64_t i = 0; i < 1024; ++i) {
        page_table_update(pt, 0x555550000 + i, 0x2000 + i);
    }
    for (uint64_t i = 0; i < 1024; ++i) {
        assert(page_table_query(pt, 0x555550000 + i) == 0x2000 + i);
    }

    // Attempt to map, then unmap deeply nested entries
    uint64_t tricky_vpn = (0x1FFULL << 40) | (0x1FFULL << 30) | (0x1FFULL << 20) | (0x1FFULL << 10) | 0x1FF;
    page_table_update(pt, tricky_vpn, 0x7FFF);
    assert(page_table_query(pt, tricky_vpn) == 0x7FFF);
    page_table_update(pt, tricky_vpn, NO_MAPPING);
    assert(page_table_query(pt, tricky_vpn) == NO_MAPPING);

    // Make sure mapping near each other don’t leak
    page_table_update(pt, 0x11111111111, 0x1a2b);
    page_table_update(pt, 0x11111111222, 0x1a2c);
    page_table_update(pt, 0x11111113333, 0x1a2d);
    assert(page_table_query(pt, 0x11111111111) == 0x1a2b);
    assert(page_table_query(pt, 0x11111111222) == 0x1a2c);
    assert(page_table_query(pt, 0x11111113333) == 0x1a2d);

    // Mix of mapping and immediate unmapping
    page_table_update(pt, 0xABCDEF, 0x9876);
    assert(page_table_query(pt, 0xABCDEF) == 0x9876);
    page_table_update(pt, 0xABCDEF, NO_MAPPING);
    assert(page_table_query(pt, 0xABCDEF) == NO_MAPPING);

    // Reuse same VPN with multiple mappings
    page_table_update(pt, 0xF0F0F0, 0xAAAA);
    assert(page_table_query(pt, 0xF0F0F0) == 0xAAAA);
    page_table_update(pt, 0xF0F0F0, 0xBBBB);
    assert(page_table_query(pt, 0xF0F0F0) == 0xBBBB);
    page_table_update(pt, 0xF0F0F0, NO_MAPPING);
    assert(page_table_query(pt, 0xF0F0F0) == NO_MAPPING);

    printf("All tests passed!\n");
    return 0;
}

