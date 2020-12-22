#ifndef VM_MMAP_H
#define VM_MMAP_H

#include <stddef.h>

/* 每个进程同一时间可以映射的最多文件数*/
#define MAX_MMAP_FILES 128

struct mmap
{
  /* 文件映射开始地址*/
  void *upage;
  /* 被映射的文件 */
  struct file *file;
  /* 文件映射使用页数 */
  size_t num_pages;
};

int mmap (int fd, void *addr);
void munmap (int md);

#endif /* vm/mmap.h */
