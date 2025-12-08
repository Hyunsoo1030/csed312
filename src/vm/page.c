#include "vm/page.h"
#include "vm/frame.h"
#include <string.h>
#include "threads/vaddr.h"
#include "threads/thread.h"
#include "threads/malloc.h"
#include "filesys/file.h"
#include "vm/swap.h"

static unsigned vm_hash (const struct hash_elem *e, void *aux);
static bool vm_less (const struct hash_elem *a, const struct hash_elem *b, void *aux);

extern struct lock filesys_lock;
extern struct lock frame_table_lock;

// vm (hash table) initialization
void vm_init (struct hash *vm) //
{
	hash_init(vm, vm_hash, vm_less, NULL);
}

static unsigned vm_hash (const struct hash_elem *e, void *aux UNUSED)
{
	struct vm_entry *vm_entry = hash_entry(e, struct vm_entry, elem);
	return hash_int((int)vm_entry->vaddr);
}

static bool vm_less (const struct hash_elem *a, const struct hash_elem *b, void *aux UNUSED)
{
	return hash_entry(a, struct vm_entry, elem)->vaddr < hash_entry(b, struct vm_entry, elem)->vaddr;
}	

// vm entry
bool vm_entry_insert (struct hash *vm, struct vm_entry *vm_entry)
{	
	if (hash_insert(vm, &vm_entry->elem))
		return true;
	else 
		return false;
	
}

bool vm_entry_delete (struct hash *vm, struct vm_entry *vm_entry) // syscall munmap에서 호출
{
	lock_acquire(&frame_table_lock);
	if (hash_delete(vm, &vm_entry->elem)) {
		release_frame(pagedir_get_page(thread_current()->pagedir, vm_entry->vaddr));
		free(vm_entry);
		lock_release(&frame_table_lock);
		return true;
	}
	else{
		lock_release(&frame_table_lock);
		return false;
	}
}	

struct vm_entry *vm_entry_find (void *vaddr)
{
	struct hash *vm = &thread_current()->vm;
	struct vm_entry vm_entry;
	struct hash_elem *elem;
	vm_entry.vaddr = pg_round_down(vaddr);

	if ((elem = hash_find(vm, &vm_entry.elem)))
		return hash_entry(elem, struct vm_entry, elem);
	else 
		return NULL;
}

void vm_destroy_action(struct hash_elem *e, void *aux UNUSED)
{
	struct vm_entry *vm_entry = hash_entry(e, struct vm_entry, elem);
	lock_acquire(&frame_table_lock);
	if(vm_entry)
	{
		if(vm_entry->is_loaded)
		{
			release_frame(pagedir_get_page(thread_current()->pagedir, vm_entry->vaddr));
		}
		free(vm_entry);
	}	
	lock_release(&frame_table_lock);
	
}

void vm_destroy (struct hash *vm)
{ // destroy vm hash table
	hash_destroy(vm, vm_destroy_action);
}


bool read_file_to_page (void* addr, struct vm_entry *vm_entry)
{
	lock_acquire(&filesys_lock);
	int byte_read = file_read_at(vm_entry->file, addr, vm_entry->bytes_to_read, vm_entry->offset);
	lock_release(&filesys_lock);
	if (byte_read != (int)vm_entry->bytes_to_read)
		return false;
	memset(addr + vm_entry->bytes_to_read, 0, vm_entry->zero_bytes);
	return true;

}


struct vm_entry *vm_entry_create ( uint8_t type, void *vaddr, bool is_writable, bool is_loaded, struct file* file, size_t offset, size_t bytes_to_read, size_t zero_bytes)
{
	struct vm_entry* new_entry = (struct vm_entry*)malloc(sizeof(struct vm_entry));
	if (!new_entry) return NULL;


	memset(new_entry, 0, sizeof(struct vm_entry));

	new_entry->type = type;
	new_entry->vaddr = vaddr;
	new_entry->is_writable = is_writable;
	new_entry->is_loaded = is_loaded;
	new_entry->file = file;
	new_entry->offset = offset;
	new_entry->bytes_to_read = bytes_to_read;
	new_entry->zero_bytes = zero_bytes;

	return new_entry;
}