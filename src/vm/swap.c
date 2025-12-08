#include "vm/swap.h"
#include <bitmap.h>
#include "threads/synch.h"
#include "devices/block.h"
#include "threads/vaddr.h"
#include "threads/interrupt.h"

#define SECTOR_NUM_PER_PAGE (PGSIZE/BLOCK_SECTOR_SIZE)

static struct lock lock_swap;
static struct bitmap *swap_bitmap;
static struct block *swap_block;

void swap_init()
{
    swap_block = block_get_role(BLOCK_SWAP);
    
    size_t swap_bitmap_size = block_size(swap_block) / SECTOR_NUM_PER_PAGE;
    swap_bitmap = bitmap_create(swap_bitmap_size);
    
    lock_init(&lock_swap);
}

bool swap_in(size_t slot_index, void *kaddr)
{
    int start_sector;
    int i;
    
    start_sector = SECTOR_NUM_PER_PAGE * slot_index;

    lock_acquire(&lock_swap); 
    
    for (i = 0; i < SECTOR_NUM_PER_PAGE; i++)
    {   
        block_read(swap_block, start_sector + i, kaddr + i * BLOCK_SECTOR_SIZE);
    }
    
    bitmap_set(swap_bitmap, slot_index, false);

    lock_release(&lock_swap); 

    return true;
}

size_t swap_out(void* kaddr)
{
    size_t slot_index;
    int start_sector;
    int i;

    lock_acquire(&lock_swap);

    slot_index = bitmap_scan_and_flip(swap_bitmap, 0, 1, false);

    if (slot_index == BITMAP_ERROR)
    {
        lock_release(&lock_swap);
        NOT_REACHED(); 
        return BITMAP_ERROR;
    }

    start_sector = SECTOR_NUM_PER_PAGE * slot_index;
    
    for (i = 0; i < SECTOR_NUM_PER_PAGE; i++)
    {
        block_write(swap_block, start_sector + i, kaddr + i * BLOCK_SECTOR_SIZE);
    }
    
    lock_release(&lock_swap);
    
    return slot_index;
}