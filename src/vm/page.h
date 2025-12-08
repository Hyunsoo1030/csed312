#ifndef VM_PAGE_H
#define VM_PAGE_H

#define VM_BIN 0
#define VM_FILE 1
#define VM_ANON 2

#include <hash.h>
#include "userprog/syscall.h"
#include "threads/palloc.h"
#include "filesys/off_t.h"

struct vm_entry 
{
	uint8_t type; // VM_BIN, VM_FILE, VM_ANON
	void *vaddr;
	bool is_writable;
	bool is_loaded;
	struct file* file; 
	size_t offset;
	size_t bytes_to_read;
	size_t zero_bytes;
    struct hash_elem elem;
    struct list_elem mmap_elem;
    size_t swap_slot;
};

struct mmap_file {
  mapid_t mapid;        
  struct file* file;     
  struct list_elem elem; 
  struct list mapping_pages;  
};

void vm_init (struct hash *vm);

struct vm_entry *vm_entry_find (void *vaddr);

bool vm_entry_insert (struct hash *vm, struct vm_entry *vm_entry);
bool vm_entry_delete (struct hash *vm, struct vm_entry *vm_entry);


void vm_destroy_action(struct hash_elem *e, void *aux);
void vm_destroy (struct hash *vm);
bool read_file_to_page (void* kaddr, struct vm_entry *fte);
struct vm_entry *vm_entry_create ( uint8_t type, void *vaddr, bool is_writable, bool is_loaded, struct file* file, size_t offset, size_t bytes_to_read, size_t zero_bytes);
#endif