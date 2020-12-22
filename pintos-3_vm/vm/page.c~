#include "threads/thread.h"
#include "threads/vaddr.h"
#include "threads/palloc.h"
#include "userprog/process.h"
#include "vm/page.h"
#include "vm/swap.h"
#include "devices/block.h"

static unsigned vm_hash_func (const struct hash_elem *e, void *aux UNUSED);
static bool vm_less_func (const struct hash_elem *a, const struct hash_elem *b);
void vm_hash_destroy_func (struct hash_elem *e, void *aux UNUSED);

/* Initialize hash table of thread. */
void
vm_init (struct hash *vm)
{
  hash_init (vm, vm_hash_func, vm_less_func, NULL);
}

/* Find hash value. */
static unsigned
vm_hash_func (const struct hash_elem *e, void *aux UNUSED)
{
  struct vm_entry *vme;
  vme = hash_entry (e, struct vm_entry, elem);
  return hash_int ((int)vme->vaddr);
}

/* Compare hash value. */
static bool
vm_less_func (const struct hash_elem *a, const struct hash_elem *b)
{
  struct vm_entry *va;
  struct vm_entry *vb;
  va = hash_entry (a, struct vm_entry, elem);
  vb = hash_entry (b, struct vm_entry, elem);
  return va->vaddr < vb->vaddr;
}

/* Insert vm_entry into hash table. 
   Return true when insertion complete. */
bool
insert_vme (struct hash *vm, struct vm_entry *vme)
{
  if (hash_insert (vm, &vme->elem) != NULL)
    return false;
  else
    return true;
}

/* Delete vm_entry in hash table. 
   Return true when deletion complete. */
bool
delete_vme (struct hash *vm, struct vm_entry *vme)
{
  void *kaddr;
  if (hash_delete (vm, &vme->elem) != NULL)
  {
    kaddr=pagedir_get_page (thread_current ()->pagedir, vme->vaddr);
    free_page (kaddr);
    pagedir_clear_page (thread_current ()->pagedir, vme->vaddr);
    free (vme);
    return true;
  }
  else
    return false;
}

/* Find vm_entry which contain certain virtual address.
   If there is no vme, return NULL. */
struct vm_entry *
find_vme (void *vaddr)
{
  struct vm_entry vme;
  vme.vaddr = pg_round_down (vaddr);

  struct hash_elem *e;
  e = hash_find (&thread_current ()->vm, &vme.elem);
  if (e != NULL)
    return hash_entry (e, struct vm_entry, elem);
  else
    return NULL;
}
/* Destroy Hash table. */
void
vm_destroy (struct hash *vm)
{
  hash_destroy (vm, vm_hash_destroy_func);
}

/* How to destroy Hash table? */
void
vm_hash_destroy_func (struct hash_elem *e, void *aux UNUSED)
{
  struct vm_entry *vme;
  vme = hash_entry (e, struct vm_entry, elem);
  void *kaddr;
  if(vme->is_loaded == true)
  {
    kaddr=pagedir_get_page(thread_current()->pagedir,vme->vaddr);
    free_page(kaddr);
    pagedir_clear_page(thread_current()->pagedir, vme->vaddr);
  }
  free (vme);
}

/* After allocate physical memory, load file to physical page from disk. */
bool
load_file (void *kaddr, struct vm_entry *vme)
{
  int size;
  file_seek(vme->file, vme->offset);
  size = file_read(vme->file, kaddr, vme->read_bytes);

  /* Fill rest space to 0 */
  memset (kaddr + size, 0, vme->zero_bytes);  
  return vme->read_bytes == size;
}
