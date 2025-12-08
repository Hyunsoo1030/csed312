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
struct list_elem *eviction_ptr; 

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
    list_push_back(&frame_table, &frame->table_elem);
}

// Remove a frame from the frame table
void frame_list_remove(struct frame *frame)
{	
	// If the element being removed is pointed to by eviction_ptr, move eviction_ptr to the next element
	if (eviction_ptr != &frame->table_elem)
		list_remove(&frame->table_elem);
	else if (eviction_ptr == &frame->table_elem)
		eviction_ptr = list_remove(eviction_ptr);
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
		if ((frame->vm_entry->vaddr) == vaddr)
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
    frame = (struct frame *)malloc(sizeof(struct frame));
    if (!frame) return NULL;
    memset(frame, 0, sizeof(struct frame));

    frame->thread = thread_current();

	// allocate physical page
    frame->page_addr = palloc_get_page(flags);
    while (!(frame->page_addr)){
        evict_page_frame();
        frame->page_addr = palloc_get_page(flags); 
    }

	ASSERT(pg_ofs(frame->page_addr) == 0);
	frame->pinned = false;

	// add frame to frame table
	frame_list_add(frame);		

    return frame;
}


void release_frame(void *addr)
{
	struct frame *frame = find_frame_by_paddr(addr);
	if (frame) {
		// Update the vm_entry to reflect that the page is no longer loaded
		frame->vm_entry->is_loaded = false;

		// Clear the page from the page directory 
		pagedir_clear_page(frame->thread->pagedir, frame->vm_entry->vaddr);

		// Free the physical page and remove the frame from the frame table
		palloc_free_page(frame->page_addr);
		frame_list_remove(frame);
		free(frame);
	}
}

struct frame* choose_victim_frame()
{
	struct list_elem *e;
	struct frame *frame;
	
	while(true){ // infinite loop until a victim frame is found
		if (!eviction_ptr || (eviction_ptr == list_end(&frame_table))){ // initialize or wrap around
			if (!list_empty(&frame_table)){ // check if frame table is not empty
				eviction_ptr = list_begin(&frame_table);
				e = eviction_ptr;
			}
			else // frame table is empty
				return NULL;
		}
		else{ // move to next element
			eviction_ptr = list_next(eviction_ptr);
			if (eviction_ptr == list_end(&frame_table)) // wrap around
				continue;
			e = eviction_ptr;
		}
		
		frame = list_entry(e, struct frame, table_elem);
		if(!frame->pinned){
			if (!pagedir_is_accessed(frame->thread->pagedir, frame->vm_entry->vaddr))
				return frame;
			else
				pagedir_set_accessed(frame->thread->pagedir, frame->vm_entry->vaddr, false);
		}
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
	struct frame *frame;
	//lock_acquire(&frame_table_lock);
	frame = find_frame_by_paddr(kaddr);
	//if (frame)
	frame->pinned = true;
	//lock_release(&frame_table_lock);
}

// Unpin a frame to allow it to be evicted
void unpin_frame(void *kaddr)
{
	struct frame *frame;
	//lock_acquire(&frame_table_lock);
	frame = find_frame_by_paddr(kaddr);
	//if (frame)
	frame->pinned = false;
	//lock_release(&frame_table_lock);
}