#ifndef VM_PAGE_H
#define VM_PAGE_H

#include <hash.h>

#define VM_BIN  0 /* Load data from binary file. */
#define VM_FILE 1 /* Load data from mapped file. */
#define VM_ANON 2 /* Load data from swap block.  */

/* Each Page's information. Located in virtual memory. */
struct vm_entry
{
  uint8_t type;  	 /* VM_BIN, VM_FILE, VM_ANON */
  void *vaddr; 	   	 /* Virtual Address */
  bool writable;

  bool is_loaded; 	/* Have physical address or not */
  struct file* file; 	/* Mapped file */
  bool pinned;    	/* Pinned file will be excepted from deletion. */

  struct list_elem mmap_elem; /* Connected with mmap_list in thread. */

  size_t offset;      	/* File's offset. */
  size_t read_bytes;  	/* How much can read.  */
  size_t zero_bytes;  	/* How much is empty?  */

  size_t swap_slot;  	/* Swap slot index. */

  struct hash_elem elem;/* Connected with hash table in thread.*/
};

/* Information of mapped file. */
struct mmap_file
{
  int mapid;    	/* Mapping id. */
  struct file* file;  	/* Mapped file. */
  struct list_elem elem;/* Connected with mmap_list in thread. */
  struct list vme_list; /* vm_entry list correspond to mmap_file. */
};

/* Physical Page for user. */
struct page
{
  void *kaddr; 		/* Physical page address. */
  struct vm_entry *vme; /* Mapped vm_entry in virtual memory. */
  struct thread *thread;/* Which thread use this page? */
  struct list_elem lru; /* Connected with lru_list in frame.c */
};

typedef int mapid_t;

void vm_init (struct hash *vm);
static unsigned vm_hash_func (const struct hash_elem *e, void *aux);
static bool vm_less_func (const struct hash_elem *a, const struct hash_elem *b);
bool insert_vme (struct hash *vm, struct vm_entry *vme);
bool delete_vme (struct hash *vm, struct vm_entry *vme);
struct vm_entry *find_vme (void *vaddr);
void vm_destroy (struct hash *vm);
void vm_hash_action_func (struct hash_elem *e, void *aux);

#endif
