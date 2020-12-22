#ifndef SWAP_H
#define SWAP_H

#include <stddef.h>

void swap_init ();
void swap_in (size_t used_index, void *kaddr);
size_t swap_out (void *kaddr);

#endif
