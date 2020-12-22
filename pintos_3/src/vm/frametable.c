#include <stdio.h>
#include <stdbool.h>
#include <list.h>
#include <hash.h>
#include <debug.h>
#include <string.h>
#include "userprog/pagedir.h"
#include "vm/pageinfo.h"
#include "vm/frametable.h"
#include "vm/swap.h"
#include "threads/palloc.h"
#include "threads/malloc.h"
#include "threads/vaddr.h"
#include "threads/synch.h"
#include "filesys/file.h"
#include "filesys/inode.h"
#include "filesys/filesys.h"

struct file_info {
  struct file *file;
  off_t end_offset;
};

static inline off_t offset (off_t end_offset) {
  return end_offset > 0  ? (end_offset - 1) & ~PGMASK : 0;
}

static inline off_t size (off_t end_offset) {
  return end_offset - offset (end_offset);
}

/* 用户页表信息 */
struct page_info
{
  uint8_t type;
  uint8_t writable;
  uint32_t *pd;
  const void *upage; // 用户的virtual address
  bool swapped; // 如果为true，就说明page在交换空间内，能够被交换区读取
  struct frame *frame; // page关联的frame
  union
  {
    struct file_info file_info; // page 关联的文件信息
    block_sector_t swap_sector; // 所在的交换区
    const void *kpage; 
  } data;
  struct list_elem elem;
};

struct frame {
  void *kpage; // 帧页关联的kernal virtual page 
  struct list page_info_list; // 与这个frame关联的所有page
  unsigned short lock; //
  bool io;
  struct condition io_done;
  struct hash_elem hash_elem;
  struct list_elem list_elem;
};

static struct lock frame_lock;
static struct hash read_only_frames; 
static struct list frame_list; // 循环链表
static struct list_elem *clock_hand;

static void frame_init (struct frame *frame);
static struct frame *allocate_frame (void);
static bool load_frame (uint32_t *pd, const void *upage, bool write,
                        bool keep_locked);
static void map_page (struct page_info *page_info, struct frame *frame,
                      const void *upage);
static void wait_for_io_done (struct frame **frame);
static struct frame *lookup_read_only_frame (struct page_info *page_info);
static void *evict_frame (void); // 驱逐页帧
static void *get_frame_to_evict (void); //
static unsigned frame_hash (const struct hash_elem *e, void *aux UNUSED);
static bool frame_less (const struct hash_elem *a, const struct hash_elem *b,
                        void *aux UNUSED);

/* 创建页 */
struct page_info * pageinfo_create (void) {
  struct page_info *page_info;
  page_info = calloc (1, sizeof *page_info);
  return page_info;
}

/* 删除页 */
void pageinfo_destroy (struct page_info *page_info) {
  free (page_info);
}

/* 分配用户页 */
void pageinfo_set_upage (struct page_info *page_info, const void *upage) {
  page_info->upage = upage;
}

/* 分配用户页 */
void pageinfo_set_type (struct page_info *page_info, int type) {
  page_info->type = type;
}

void pageinfo_set_writable (struct page_info *page_info, int writable) {
  page_info->writable = writable;
}

void pageinfo_set_pagedir (struct page_info *page_info, uint32_t *pd) {
  page_info->pd = pd;
}

void pageinfo_set_fileinfo (struct page_info *page_info, struct file *file, off_t end_offset) {
  page_info->data.file_info.file = file;
  page_info->data.file_info.end_offset = end_offset;
}

void
pageinfo_set_kpage (struct page_info *page_info, const void *kpage) {
  page_info->data.kpage = kpage;
}

void
frametable_init (void)
{
  lock_init (&frame_lock);
  list_init (&frame_list);
  clock_hand = list_end (&frame_list);
  hash_init (&read_only_frames, frame_hash, frame_less, NULL);
}

bool
frametable_load_frame (uint32_t *pd, const void *upage, bool write)
{
  return load_frame (pd, upage, write, false);
}

/* 在进程退出的时候调用 */
void
frametable_unload_frame (uint32_t *pd, const void *upage) {
  struct page_info *page_info, *p;
  struct file_info *file_info;
  void *kpage;
  struct frame *frame;
  off_t bytes_written;
  struct list_elem *e;

  ASSERT (is_user_vaddr (upage));
  page_info = pagedir_get_info (pd, upage);
  if (page_info == NULL)
    return;
  lock_acquire (&frame_lock);
  wait_for_io_done (&page_info->frame);
  if (page_info->frame != NULL)
    {
      frame = page_info->frame;
      page_info->frame = NULL;
      if (list_size (&frame->page_info_list) > 1)
        {
          for (e = list_begin (&frame->page_info_list);
               e != list_end (&frame->page_info_list); e = list_next (e))
            {
              p = list_entry (e, struct page_info, elem);
              if (page_info == p)
                {
                  list_remove (e);
                  break;
                }
            }
        }
      else
        {
          ASSERT (list_entry (list_begin (&frame->page_info_list),
                              struct page_info, elem) == page_info);
          if (page_info->type & PAGE_TYPE_FILE && page_info->writable == 0)
            hash_delete (&read_only_frames, &frame->hash_elem);
          if (clock_hand == &frame->list_elem)
            {
              clock_hand = list_next (clock_hand);
              if (clock_hand == list_end (&frame_list))
                clock_hand = list_begin (&frame_list);
            }
          list_remove (&page_info->elem);
          list_remove (&frame->list_elem);
        }
      pagedir_clear_page (page_info->pd, upage);
      lock_release (&frame_lock); // 在检脏数据前一定要将frame从数据结构中移除掉，不然其他进程可能会修改数据
      if (list_empty (&frame->page_info_list))
        {
          if (page_info->writable & WRITABLE_TO_FILE
              && pagedir_is_dirty (page_info->pd, upage))
            {
              ASSERT (page_info->writable != 0);
              file_info = &page_info->data.file_info;
              bytes_written = file_write_at (file_info->file,
                                             frame->kpage,
                                             size (file_info->end_offset),
                                             offset (file_info->end_offset));
              ASSERT (bytes_written == size (file_info->end_offset));
            }
          palloc_free_page (frame->kpage);
          ASSERT (frame->lock == 0);
          free (frame);
        }
    }
  else
    lock_release (&frame_lock);
  if (page_info->swapped)
    {
      swap_release (page_info->data.swap_sector);
      page_info->swapped = false;
    }
  else if (page_info->type & PAGE_TYPE_KERNEL)
    {
      kpage = (void *) page_info->data.kpage;
      ASSERT (kpage != NULL);
      palloc_free_page (kpage);
      page_info->data.kpage = NULL;
    }
  pagedir_set_info (page_info->pd, upage, NULL);
  free (page_info);
}

bool frametable_lock_frame(uint32_t *pd, const void *upage, bool write) 
{
  return load_frame (pd, upage, write, true);
}

void
frametable_unlock_frame(uint32_t *pd, const void *upage)
{
  struct page_info *page_info;
  
  ASSERT (is_user_vaddr (upage));
  page_info = pagedir_get_info (pd, upage);
  if (page_info == NULL)
    return;
  ASSERT (page_info->frame != NULL);
  lock_acquire (&frame_lock);
  page_info->frame->lock--;
  lock_release (&frame_lock);
}

static bool
load_frame (uint32_t *pd, const void *upage, bool write, bool keep_locked)
{
  struct page_info *page_info;
  struct file_info *file_info;
  struct frame *frame = NULL;
  void *kpage;
  off_t bytes_read;
  bool success = false;
  ASSERT (is_user_vaddr (upage));
  page_info = pagedir_get_info (pd, upage);
  if (page_info == NULL || (write && page_info->writable == 0))
    return false;
  lock_acquire (&frame_lock);
  wait_for_io_done (&page_info->frame);
  ASSERT (page_info->frame == NULL || keep_locked);
  if (page_info->frame != NULL)
    {
      if (keep_locked)
        page_info->frame->lock++;
      lock_release (&frame_lock);
      return true;
    }
  if (page_info->type & PAGE_TYPE_FILE
      && page_info->writable == 0)
    {
      frame = lookup_read_only_frame (page_info);
      if (frame != NULL)
        {
          map_page (page_info, frame, upage);
          frame->lock++;
          wait_for_io_done (&frame);
          frame->lock--;
          success = true;
        }
    }
  if (frame == NULL)
    {
      frame = allocate_frame ();
      if (frame != NULL)
        {
          map_page (page_info, frame, upage);
          if (page_info->swapped || page_info->type & PAGE_TYPE_FILE)
            {
              frame->io = true;
              frame->lock++;
              if (page_info->swapped)
                {
                  lock_release (&frame_lock);
                  swap_read (page_info->data.swap_sector, frame->kpage);
                  page_info->swapped = false;
                }
              else
                {
                  if (page_info->writable == 0)
                    {
                      hash_insert (&read_only_frames, &frame->hash_elem);
                    }
                  file_info = &page_info->data.file_info;
                  lock_release (&frame_lock);
                  bytes_read = file_read_at (file_info->file,
                                             frame->kpage,
                                             size (file_info->end_offset),
                                             offset (file_info->end_offset));
                  ASSERT (bytes_read == size (file_info->end_offset));
                }
              lock_acquire (&frame_lock);
              frame->lock--;
              frame->io = false;
              cond_broadcast (&frame->io_done, &frame_lock);
            }
          else if (page_info->type & PAGE_TYPE_KERNEL)
            {
              kpage = (void *) page_info->data.kpage;
              ASSERT (kpage != NULL);
              memcpy (frame->kpage, kpage, PGSIZE);
              palloc_free_page (kpage);
              page_info->data.kpage = NULL;
              page_info->type = PAGE_TYPE_ZERO;
            }
          success = true;
        }
    }
  if (success && keep_locked)
    frame->lock++;
  lock_release (&frame_lock);
  return success;
}

static void
frame_init (struct frame *frame)
{
  list_init (&frame->page_info_list);
  cond_init (&frame->io_done);
}

static struct frame *
allocate_frame (void)
{
  struct frame *frame;
  void *kpage;
  
  kpage = palloc_get_page (PAL_USER | PAL_ZERO);
  if (kpage != NULL)
    {
      frame = calloc (1, sizeof *frame);
      if (frame != NULL)
        {
          frame_init (frame);
          frame->kpage = kpage;
          if (!list_empty (&frame_list))
            list_insert (clock_hand, &frame->list_elem);
          else
            {
              list_push_front (&frame_list, &frame->list_elem);
              clock_hand = list_begin (&frame_list);
            }
        }
      else
        palloc_free_page (kpage);
    }
  else
    frame = evict_frame ();
  return frame;
}

static void
map_page (struct page_info *page_info, struct frame *frame, const void *upage) 
{
  page_info->frame = frame;
  list_push_back (&frame->page_info_list, &page_info->elem);
  pagedir_set_page (page_info->pd, upage, frame->kpage, page_info->writable != 0);
  pagedir_set_dirty (page_info->pd, upage, false);
  pagedir_set_accessed (page_info->pd, upage, true);
}

static void
wait_for_io_done (struct frame **frame)
{
  while (*frame != NULL && (*frame)->io)
    cond_wait (&(*frame)->io_done, &frame_lock);
}

static void *
evict_frame (void)
{
  struct frame *frame;
  struct page_info *page_info;
  struct file_info *file_info;
  off_t bytes_written;
  block_sector_t swap_sector;
  struct list_elem *e;
  bool dirty = false;

  frame = get_frame_to_evict();
  for (e = list_begin (&frame->page_info_list);
       e != list_end (&frame->page_info_list); e = list_next (e))
    {
      page_info = list_entry (e, struct page_info, elem);
      dirty = dirty || pagedir_is_dirty (page_info->pd, page_info->upage);
      pagedir_clear_page (page_info->pd, page_info->upage);
    }
  if (dirty || page_info->writable & WRITABLE_TO_SWAP)
    {
      ASSERT (page_info->writable != 0);
      frame->io = true;
      frame->lock++;
      if (page_info->writable & WRITABLE_TO_FILE)
        {
          file_info = &page_info->data.file_info;
          lock_release (&frame_lock);
          bytes_written = file_write_at (file_info->file,
                                         frame->kpage,
                                         size (file_info->end_offset),
                                         offset (file_info->end_offset));
          ASSERT (bytes_written == size (file_info->end_offset));          
        }
      else
        {
          lock_release (&frame_lock);
          swap_sector = swap_write (frame->kpage);
        }
      lock_acquire (&frame_lock);
      frame->lock--;
      frame->io = false;
      cond_broadcast (&frame->io_done, &frame_lock);
    }
  else if (page_info->type & PAGE_TYPE_FILE && page_info->writable == 0)
    {
      ASSERT (hash_find (&read_only_frames, &frame->hash_elem) != NULL);
      hash_delete (&read_only_frames, &frame->hash_elem);
    }
  for (e = list_begin (&frame->page_info_list);
       e != list_end (&frame->page_info_list); )
    {
      page_info = list_entry (list_front (&frame->page_info_list),
                              struct page_info, elem);
      page_info->frame = NULL;
      if (page_info->writable & WRITABLE_TO_SWAP)
        {
          page_info->swapped = true;
          page_info->data.swap_sector = swap_sector;
        }
      e = list_remove (e);
    }
  memset (frame->kpage, 0, PGSIZE);
  return frame;
}

static void *
get_frame_to_evict (void)
{
  struct frame *frame;
  struct frame *start;
  struct frame *found = NULL;
  struct page_info *page_info;
  struct list_elem *e;
  bool accessed;

  ASSERT (!list_empty (&frame_list));
  start = list_entry (clock_hand, struct frame, list_elem);
  frame = start;
  do
    {
      if (frame->lock == 0)
        {
          accessed = false;
          ASSERT (!list_empty (&frame->page_info_list));
          for (e = list_begin (&frame->page_info_list);
               e != list_end (&frame->page_info_list); e = list_next (e))
            {
              page_info = list_entry (e, struct page_info, elem);
              accessed = accessed || pagedir_is_accessed (page_info->pd,
                                                          page_info->upage);
              pagedir_set_accessed (page_info->pd, page_info->upage, false);
            }
          if (!accessed)
            found = frame;
        }
      clock_hand = list_next (clock_hand);
      if (clock_hand == list_end (&frame_list))
        clock_hand = list_begin (&frame_list);
      frame = list_entry (clock_hand, struct frame, list_elem);
    } while (!found && frame != start);
  if (found == NULL)
    {
      ASSERT (frame == start);
      if (frame->lock > 0)
        PANIC ("no frame available for eviction");
      found = frame;
      clock_hand = list_next (clock_hand);
      if (clock_hand == list_end (&frame_list))
        clock_hand = list_begin (&frame_list);
    }

  return found;
}

static struct frame *
lookup_read_only_frame (struct page_info *page_info)
{
  struct frame frame;
  struct hash_elem *e;

  list_init (&frame.page_info_list);
  list_push_back (&frame.page_info_list, &page_info->elem);
  e = hash_find (&read_only_frames, &frame.hash_elem);
  return e != NULL ? hash_entry (e, struct frame, hash_elem) : NULL;
}

static unsigned
frame_hash (const struct hash_elem *e, void *aux UNUSED)
{
  struct frame *frame = hash_entry (e, struct frame, hash_elem);
  struct page_info *page_info;
  block_sector_t sector;

  ASSERT (!list_empty (&frame->page_info_list));
  page_info = list_entry (list_front (&frame->page_info_list), // 取出第一个page
                          struct page_info, elem);
  ASSERT (page_info->type & PAGE_TYPE_FILE && page_info->writable == 0);
  sector = inode_get_inumber (file_get_inode (page_info->data.file_info.file));
  return hash_bytes (&sector, sizeof sector)
    ^ hash_bytes (&page_info->data.file_info.end_offset,
                  sizeof page_info->data.file_info.end_offset);
}

static bool
frame_less (const struct hash_elem *a_, const struct hash_elem *b_,
            void *aux UNUSED)
{
  struct frame *frame_a = hash_entry (a_, struct frame, hash_elem);
  struct frame *frame_b = hash_entry (b_, struct frame, hash_elem);
  struct page_info *page_info_a, *page_info_b;
  block_sector_t sector_a, sector_b;
  struct inode *inode_a, *inode_b;

  ASSERT (!list_empty (&frame_a->page_info_list));
  ASSERT (!list_empty (&frame_b->page_info_list));
  page_info_a = list_entry (list_front (&frame_a->page_info_list),
                            struct page_info, elem);
  page_info_b = list_entry (list_front (&frame_b->page_info_list),
                            struct page_info, elem);
  inode_a = file_get_inode (page_info_a->data.file_info.file);
  inode_b = file_get_inode (page_info_b->data.file_info.file);
  sector_a = inode_get_inumber (inode_a);
  sector_b = inode_get_inumber (inode_b);
  if (sector_a < sector_b)
    return true;
  else if (sector_a > sector_b)
    return false;
  else
    if (page_info_a->data.file_info.end_offset
        < page_info_b->data.file_info.end_offset)
      return true;
    else
      return false;
}
