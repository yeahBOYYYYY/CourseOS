#include "os.h"

// Constants defining page table architecture
#define PAGE_SIZE_BITS      13
#define PAGE_TABLE_LEVELS   5
#define VPN_BITS_USED       50
#define BITS_PER_LEVEL      10      // (VPN_BITS_USED / PAGE_TABLE_LEVELS)
#define ENTRIES_PER_LEVEL   1024    // (1UL << BITS_PER_LEVEL)
#define LEVEL_MASK          0x3ff   // (ENTRIES_PER_LEVEL - 1)
#define PTE_VALID_BIT       0
#define PTE_UNUSED_BITS     12
#define PTE_FRAME_SHIFT     13      // PAGE_SIZE_BITS

/**
 * Splits a virtual page number (VPN) into its level indices for a multi-level page table.
 *
 * @param vpn The virtual page number to split.
 * @param indices Output array of size PAGE_TABLE_LEVELS to hold the index for each level.
 */
static void split_vpn(uint64_t vpn, uint64_t indices[PAGE_TABLE_LEVELS]) {
    for (int i = PAGE_TABLE_LEVELS - 1; i >= 0; --i) {
        indices[i] = vpn & LEVEL_MASK;
        vpn >>= BITS_PER_LEVEL;
    }
}

/**
 * Updates a page table by either inserting or removing a mapping from a virtual page number (VPN)
 * to a physical page number (PPN).
 *
 * @param pt The physical page number of the root of the page table.
 * @param vpn The virtual page number whose mapping is to be updated.
 * @param ppn The physical page number to map to. If equal to NO_MAPPING, the mapping is removed.
 */
void page_table_update(uint64_t pt, uint64_t vpn, uint64_t ppn) {
    uint64_t indices[PAGE_TABLE_LEVELS];
    split_vpn(vpn, indices);

    uint64_t *table = (uint64_t *) phys_to_virt(pt << PAGE_SIZE_BITS);
    for (int level = 0; level < PAGE_TABLE_LEVELS - 1; ++level) {
        uint64_t entry = table[indices[level]];
        if (!(entry & 1)) {
            if (ppn == NO_MAPPING) {
                return;
            }
            uint64_t new_pt = alloc_page_frame();
            table[indices[level]] = (new_pt << PTE_FRAME_SHIFT) | 1;
            table = (uint64_t *) phys_to_virt(new_pt << PAGE_SIZE_BITS);
        } else {
            uint64_t next_pt = entry >> PTE_FRAME_SHIFT;
            table = (uint64_t *) phys_to_virt(next_pt << PAGE_SIZE_BITS);
        }
    }

    if (ppn == NO_MAPPING) {
        table[indices[PAGE_TABLE_LEVELS - 1]] = 0;
    } else {
        table[indices[PAGE_TABLE_LEVELS - 1]] = (ppn << PTE_FRAME_SHIFT) | 1;
    }
}

/**
 * Queries a page table to find the physical page number mapped to a virtual page number (VPN).
 *
 * @param pt The physical page number of the root of the page table.
 * @param vpn The virtual page number to query.
 * @return The physical page number mapped to the VPN, or NO_MAPPING if no mapping exists.
 */
uint64_t page_table_query(uint64_t pt, uint64_t vpn) {
    uint64_t indices[PAGE_TABLE_LEVELS];
    split_vpn(vpn, indices);

    uint64_t *table = (uint64_t *) phys_to_virt(pt << PAGE_SIZE_BITS);
    for (int level = 0; level < PAGE_TABLE_LEVELS - 1; ++level) {
        uint64_t entry = table[indices[level]];
        if (!(entry & 1)) {
            return NO_MAPPING;
        }
        uint64_t next_pt = entry >> PTE_FRAME_SHIFT;
        table = (uint64_t *) phys_to_virt(next_pt << PAGE_SIZE_BITS);
    }

    uint64_t final_entry = table[indices[PAGE_TABLE_LEVELS - 1]];
    if (!(final_entry & 1)) {
        return NO_MAPPING;
    }
    return final_entry >> PTE_FRAME_SHIFT;
}
