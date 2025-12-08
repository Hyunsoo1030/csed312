#ifndef VM_FRAME_H
#define VM_FRAME_H

#include <list.h>
#include "vm/page.h"
#include "threads/vaddr.h"
#include "threads/synch.h"
#include "threads/thread.h"
#include "userprog/pagedir.h"

struct frame
{
	void *page_addr; 
	struct vm_entry *vm_entry;
	struct thread *thread; 
	struct list_elem table_elem; 
	bool pinned; 
};

extern struct list frame_table; 
extern struct lock frame_table_lock; 
extern struct list_elem *eviction_ptr; 

void ft_init(void);
void frame_add(struct frame *frame);
void frame_remove(struct frame *frame);

struct frame* allocate_frame(enum palloc_flags flags);
struct frame* find_frame_by_paddr(void* addr);

struct frame* find_frame_by_vaddr(void* vaddr);
void free_frame(void *addr);

void evict_page_frame(void);
struct frame* chose_victim_frame(void);

void pin_frame(void *kaddr);
void unpin_frame(void *kaddr);

#endif