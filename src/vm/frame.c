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

void ft_init(void)
{
    list_init(&frame_table);
	lock_init(&frame_table_lock);
	eviction_ptr = NULL;
}

void frame_add(struct frame *frame)
{
    list_push_back(&frame_table, &frame->table_elem);
}

void frame_remove(struct frame *frame)
{	
	if (eviction_ptr == &frame->table_elem) eviction_ptr = list_remove(eviction_ptr);
	else if (eviction_ptr != &frame->table_elem) list_remove(&frame->table_elem);
}

struct frame* allocate_frame(enum palloc_flags flags)
{
    struct frame *frame = NULL; 
    
    ASSERT(flags & PAL_USER); 

    frame = (struct frame *)malloc(sizeof(struct frame));
    if (frame == NULL) {
        return NULL;
    }
    memset(frame, 0, sizeof(struct frame));
    
    frame->thread = thread_current();
    frame->pinned = false; 

    frame->page_addr = palloc_get_page(flags);
    
    while (frame->page_addr == NULL) {
        evict_page_frame();
        
        frame->page_addr = palloc_get_page(flags);
    }
    
    ASSERT(pg_ofs(frame->page_addr) == 0); 
    
    frame_add(frame);       

    return frame;
}

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

void free_frame(void *addr)
{
	struct frame *frame = find_frame_by_paddr(addr);
	if (frame) 
	{
		frame->vm_entry->is_loaded = false;

		pagedir_clear_page(frame->thread->pagedir, frame->vm_entry->vaddr);

		palloc_free_page(frame->page_addr);
		frame_remove(frame);
		free(frame);
	}
}

void pin_frame(void *kaddr)
{
	struct frame *frame;
	frame = find_frame_by_paddr(kaddr);
	frame->pinned = true;
}

void unpin_frame(void *kaddr)
{
	struct frame *frame;
	frame = find_frame_by_paddr(kaddr);
	frame->pinned = false;
}

struct frame* chose_victim_frame()
{
    struct list_elem *e;
    struct frame *frame;
    
    if (list_empty(&frame_table)) {
        return NULL;
    }
    
    while (true)
    { 
        if (eviction_ptr == NULL || eviction_ptr == list_end(&frame_table)) { 
            eviction_ptr = list_begin(&frame_table);
        }
        
        e = eviction_ptr;
        frame = list_entry(e, struct frame, table_elem);

        eviction_ptr = list_next(eviction_ptr);

        if (!frame->pinned)
        {
            if (!pagedir_is_accessed(frame->thread->pagedir, frame->vm_entry->vaddr)) {
                return frame; 
            }
            else {
                pagedir_set_accessed(frame->thread->pagedir, frame->vm_entry->vaddr, false); 
            }
        }
    }
}

void evict_page_frame()
{
    struct frame *frame = chose_victim_frame();
    
    bool is_modified = pagedir_is_dirty(frame->thread->pagedir, frame->vm_entry->vaddr);
    
    if (frame->vm_entry->type == VM_FILE) 
    {
        if (is_modified) 
        {   
            lock_acquire(&filesys_lock);
            file_write_at(frame->vm_entry->file, frame->page_addr, frame->vm_entry->bytes_to_read, frame->vm_entry->offset);
            lock_release(&filesys_lock);
        }
    }
    else if (frame->vm_entry->type == VM_BIN)
    {
        if (is_modified) 
        {   
            frame->vm_entry->swap_slot = swap_out(frame->page_addr);
            frame->vm_entry->type = VM_ANON;
        }
    }
    else if (frame->vm_entry->type == VM_ANON)
    {
        frame->vm_entry->swap_slot = swap_out(frame->page_addr);
    }
    
    pagedir_clear_page(frame->thread->pagedir, frame->vm_entry->vaddr);
    
    palloc_free_page(frame->page_addr);
    
    frame_remove(frame);
    frame->vm_entry->is_loaded = false;
    free(frame);
}