#include <stdio.h>
#include "vm/mmap.h"
#include "filesys/filesys.h"
#include "filesys/file.h"
#include "threads/vaddr.h"
#include "userprog/process.h"
#include "userprog/pagedir.h"
#include "vm/frametable.h"
#include "vm/growstack.h"
#include "vm/pageinfo.h"

static int
allocate_md (void *upage, struct file *file, size_t num_pages);

/* 从虚拟地址空间 VADDR.映射文件到用户地址空间 */
int mmap (int fd, void *vaddr)
{
  struct thread *cur = thread_current ();
  struct file *file = fd_get_file (fd);
  struct page_info *page_info;
  int md;
  off_t length;
  off_t ofs = 0;
  void *upage;
  size_t num_pages;
  size_t i;

  if (vaddr == 0 || pg_ofs (vaddr) != 0 || fd == STDIN_FILENO
      || fd == STDOUT_FILENO || file == NULL)
    return -1;
  length = file_length (file);
  if (length == 0)
    return -1;
  num_pages = ((size_t) pg_round_up ((const void *) length)) / PGSIZE;
  
  for (upage = vaddr, i = 0; i < num_pages; i++, upage += PGSIZE)
    if (pagedir_get_info (cur->pagedir, upage) != NULL
        || is_stack_access (upage))
      return -1;
  file = file_reopen (file);
  if (file == NULL)
    return -1;
  md = allocate_md (vaddr, file, num_pages);
  if (md == -1)
    {
      file_close (file);
      return -1;
    }
  /* 映射文件 */
  for (upage = vaddr, i = 0; i < num_pages; i++, upage += PGSIZE)
    {
      page_info = pageinfo_create ();
      if (page_info == NULL)
          break;
      if (length > PGSIZE)
        {
          ofs += PGSIZE;
          length -= PGSIZE;
        }
      else
        ofs += length;
      pageinfo_set_pagedir (page_info, cur->pagedir);
      pageinfo_set_upage (page_info, upage);
      pageinfo_set_type (page_info, PAGE_TYPE_FILE);
      pageinfo_set_fileinfo (page_info, file, ofs);
      pageinfo_set_writable (page_info, WRITABLE_TO_FILE);
      pagedir_set_info (cur->pagedir, upage, page_info);
    }
  if (i < num_pages)
    {
      num_pages = i;
      for (upage = vaddr, i = 0; i < num_pages; i++, upage += PGSIZE)
        {
          /* 帧表释放页表与页信息 */
          frametable_unload_frame (cur->pagedir, upage);
        }
      file_close (file);
      return -1;
    }
  return md;
}
/* 释放映射文件*/
void munmap (int md)
{
  struct thread *cur = thread_current ();
  struct mmap *mmap;
  void *upage;
  size_t i;
  
  if (md >= 0 && md < MAX_MMAP_FILES)
    {
      mmap = &cur->mfiles[md];
      if (mmap->file != NULL)
        {
          for (upage = mmap->upage, i = 0; i < mmap->num_pages; i++, upage += PGSIZE)
            frametable_unload_frame (cur->pagedir, upage);
          file_close (mmap->file);
          mmap->file = NULL;
        }
    }
}

static int
allocate_md (void *upage, struct file *file, size_t num_pages)
{
  struct thread *cur = thread_current ();
  int md = -1;

  /* 分配第一个可用的映射描述符*/
  for (md = 0; md < MAX_MMAP_FILES; md++)
    if (cur->mfiles[md].file == NULL)
      break;
  if (md < MAX_MMAP_FILES)
    {
      cur->mfiles[md].upage = upage;
      cur->mfiles[md].file = file;
      cur->mfiles[md].num_pages = num_pages;
    }

  return md;
}
