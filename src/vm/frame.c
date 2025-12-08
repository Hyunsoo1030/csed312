#include "vm/frame.h"
#include "vm/swap.h"
#include <list.h>
#include "threads/malloc.h"
#include "threads/synch.h"
#include <string.h>
#include "filesys/file.h"


extern struct lock filesys_lock;

struct list frame_table;
struct lock frame_table_lock;
struct list_elem *eviction_ptr; // clock pointer for eviction

// Initialize frame table and frame lock
void frame_table_init(void)
{
    list_init(&frame_table);
	lock_init(&frame_table_lock);
	eviction_ptr = NULL;
}
// Add a frame to the frame table
void frame_list_add(struct frame *frame)
{
	ASSERT (frame != NULL);
  	list_push_back (&frame_table, &frame->table_elem);
}

// Remove a frame from the global frame table.
//   If eviction_ptr points to this element, move it forward.
void frame_list_remove(struct frame *frame)
{	
	ASSERT (frame != NULL);

	if (eviction_ptr == &frame->table_elem)
		eviction_ptr = list_remove (eviction_ptr);
	else
		list_remove (&frame->table_elem);
}

// Find a frame by its kernel address
struct frame* find_frame_by_paddr(void* addr)
{
    struct list_elem *e;

	for (e = list_begin(&frame_table); e != list_end(&frame_table); e = list_next(e)) {
		struct frame *frame = list_entry(e, struct frame, table_elem);
		if ((frame->page_addr) == addr)
			return frame;
	}
	return NULL;
}
// Find a frame by the virtual address of its associated vm_entry
struct frame* find_frame_by_vaddr(void* vaddr)
{
    struct list_elem *e;
	for (e = list_begin(&frame_table); e != list_end(&frame_table); e = list_next(e))
	{
		struct frame *frame = list_entry(e, struct frame, table_elem);
		if (frame->vm_entry != NULL && frame->vm_entry->vaddr == vaddr)
			return frame;
	}
	return NULL;
}

// Allocates a physical page and creates a frame for it
struct frame* allocate_frame(enum palloc_flags flags)
{
    struct frame *frame; 

	ASSERT(flags & PAL_USER); // user memory only

	// allocate frame structure
    frame = malloc (sizeof *frame);
	if (frame == NULL)
		return NULL;
  	memset (frame, 0, sizeof *frame);

    frame->thread = thread_current();

	// Keep trying until we get a physical page, evicting if needed.
	while (frame->page_addr == NULL){
		evict_page_frame ();
		frame->page_addr = palloc_get_page (flags);
	}

	ASSERT (pg_ofs (frame->page_addr) == 0);
	frame->pinned = false;
	frame->vm_entry = NULL;

	frame_list_add (frame);
	return frame;
}

// Releases a frame and frees its resources
void release_frame(void *addr)
{
	struct frame *frame = find_frame_by_paddr(addr);
	if (frame == NULL)
    	return;

	if (frame->vm_entry != NULL){
      frame->vm_entry->is_loaded = false;
      pagedir_clear_page (frame->thread->pagedir, frame->vm_entry->vaddr);
    }

	palloc_free_page (frame->page_addr);
	frame_list_remove (frame);
	free (frame);
}

// Choose a victim frame for eviction using the clock algorithm
struct frame* choose_victim_frame()
{
	struct frame *frame;

	if (list_empty (&frame_table))
		return NULL;

	for (;;)
		{
		if (eviction_ptr == NULL || eviction_ptr == list_end (&frame_table))
			{
			eviction_ptr = list_begin (&frame_table);
			}
		else
			{
			eviction_ptr = list_next (eviction_ptr);
			if (eviction_ptr == list_end (&frame_table))
				continue;           /* wrap around */
			}

		frame = list_entry (eviction_ptr, struct frame, table_elem);

		if (frame->pinned)
			continue;

		if (!pagedir_is_accessed (frame->thread->pagedir,
									frame->vm_entry->vaddr))
			return frame;

		/* Second chance: clear accessed bit and move on. */
		pagedir_set_accessed (frame->thread->pagedir,
								frame->vm_entry->vaddr, false);
		}
}

// Evict a frame to free up space
void evict_page_frame()
{
	// 1. choose victim frame
  	struct frame *frame = choose_victim_frame();
	//if (!frame) return; // no frame to evict

	// check if the frame is pinned (should not happen)
  	bool is_modified = pagedir_is_dirty(frame->thread->pagedir, frame->vm_entry->vaddr);
	
	// 2. swap out or write back based on vm_entry type
	switch(frame->vm_entry->type)
	{
		case VM_FILE:
			if(is_modified) // write back to file if modified
			{	
				lock_acquire(&filesys_lock);
				file_write_at(frame->vm_entry->file, frame->page_addr, frame->vm_entry->bytes_to_read, frame->vm_entry->offset);
				lock_release(&filesys_lock);
			}
			break;

		case VM_BIN:
			if(is_modified) // write to swap if modified. if not, can be reloaded from executable
			{	
				frame->vm_entry->swap_slot = swap_out(frame->page_addr);
				frame->vm_entry->type = VM_ANON;
			}
			break;

		case VM_ANON:
		    // Anonymous page: always swap out
			frame->vm_entry->swap_slot = swap_out(frame->page_addr);
			break;
	}
	
	// 3. clean up frame and vm_entry
	pagedir_clear_page(frame->thread->pagedir, frame->vm_entry->vaddr);
	palloc_free_page(frame->page_addr);
	
	frame_list_remove(frame);
	frame->vm_entry->is_loaded = false;
	free(frame);
	//release_frame(frame->page_addr);
}

// Pin a frame to prevent it from being evicted
void pin_frame(void *kaddr)
{
	struct frame *frame = find_frame_by_paddr (kaddr);
	if (frame != NULL)
		frame->pinned = true;
}

// Unpin a frame to allow it to be evicted
void unpin_frame(void *kaddr)
{
	struct frame *frame = find_frame_by_paddr (kaddr);
	if (frame != NULL)
		frame->pinned = false;
}