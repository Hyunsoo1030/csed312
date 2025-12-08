#ifndef VM_FRAME_H
#define VM_FRAME_H

#include <list.h>
#include "vm/page.h"
#include "threads/vaddr.h"
#include "threads/synch.h"
#include "threads/thread.h"
#include "userprog/pagedir.h"

// Frame structure
struct frame
{
	void *page_addr; // kernel virtual address
	struct vm_entry *vm_entry; // associated vm_entry
	struct thread *thread; // owner thread
	struct list_elem table_elem; // frame table element
	bool pinned; // pin status
};

extern struct list frame_table; // global frame table
extern struct lock frame_table_lock; // lock for frame table
extern struct list_elem *eviction_ptr; // clock pointer for eviction

// Initialize frame table and lock
void frame_table_init(void);
void frame_list_add(struct frame *frame);
void frame_list_remove(struct frame *frame);

// Allocate a frame and add it to the frame table
struct frame* allocate_frame(enum palloc_flags flags);
struct frame* find_frame_by_paddr(void* addr);

// Find frame by virtual address of its vm_entry
struct frame* find_frame_by_vaddr(void* vaddr);
void release_frame(void *addr);

// Evict a page frame using the eviction policy
void evict_page_frame(void);
struct frame* choose_victim_frame(void);

// Pin and unpin frames to control eviction
void pin_frame(void *kaddr);
void unpin_frame(void *kaddr);

#endif