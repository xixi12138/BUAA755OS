#include "vm/swap.h"
#include "devices/block.h"
#include "threads/palloc.h"
#include "threads/vaddr.h"
#include "threads/synch.h"
#include <bitmap.h>
#include <stdio.h>
#define SECTOR_IN_PAGE 8 /* PGSIZE/BLOCK_SECTOR_SIZE */

struct lock swap_lock;
struct bitmap *swap_bitmap;

/* Initialize swap space. */
void
swap_init (void)
{
  struct block *swap;
  swap = block_get_role (BLOCK_SWAP);
 
 /* block_size return # of block_sector. */
  swap_bitmap = bitmap_create (block_size (swap) / SECTOR_IN_PAGE);
  if (swap_bitmap == NULL)
    return;
  lock_init (&swap_lock);
}

/* Move data from swap slot to physical address. */
void
swap_in (size_t used_index, void *kaddr)
{
  lock_acquire (&swap_lock);
  if(used_index == BITMAP_ERROR)
  {
    lock_release (&swap_lock);
    return;
  }

  /* If swap slot is empty. */
  if (bitmap_test (swap_bitmap, used_index) == false)
  {
    lock_release (&swap_lock);
    return;
  }

  struct block *swap;
  swap = block_get_role (BLOCK_SWAP);

  int i;
  for (i = 0; i < SECTOR_IN_PAGE; i++)
    block_read (swap, used_index * SECTOR_IN_PAGE + i, kaddr + BLOCK_SECTOR_SIZE* i);

  /* Set this slow is empty now. */
  bitmap_set(swap_bitmap, used_index, false);
  lock_release (&swap_lock);
}

/* Move data from physical address to swap slot. */
size_t 
swap_out (void *kaddr)
{
  lock_acquire (&swap_lock);
  struct block *swap;
  size_t index;
  swap = block_get_role (BLOCK_SWAP);

  /* Find empty slot and change flag to full. */
  index = bitmap_scan_and_flip (swap_bitmap, 0, 1, false);
  if(index == BITMAP_ERROR)
  {
    lock_release(&swap_lock);
    return BITMAP_ERROR;
  }

  int i;
  for (i = 0; i < SECTOR_IN_PAGE; i++)
    block_write (swap, index * SECTOR_IN_PAGE + i, kaddr + BLOCK_SECTOR_SIZE * i);
  lock_release (&swap_lock);
  return index;
}
