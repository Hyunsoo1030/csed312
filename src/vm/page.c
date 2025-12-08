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

void vm_init (struct hash *vm) 
{
    hash_init(vm, vm_hash, vm_less, NULL);
}

static bool vm_less (const struct hash_elem *a, const struct hash_elem *b, void *aux UNUSED)
{
    struct vm_entry *vm_entry_a = hash_entry(a, struct vm_entry, elem);
    struct vm_entry *vm_entry_b = hash_entry(b, struct vm_entry, elem);
    
    return vm_entry_a->vaddr < vm_entry_b->vaddr;
} 

static unsigned vm_hash (const struct hash_elem *e, void *aux UNUSED)
{
    struct vm_entry *vm_entry = hash_entry(e, struct vm_entry, elem);
    return hash_int((int)vm_entry->vaddr);
}   

struct vm_entry *vm_entry_create ( uint8_t type, void *vaddr, bool is_writable, bool is_loaded, struct file* file, size_t offset, size_t bytes_to_read, size_t zero_bytes)
{
    struct vm_entry* new_entry = (struct vm_entry*)malloc(sizeof(struct vm_entry));
    
    if (new_entry == NULL) {
        return NULL;
    }

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

bool vm_entry_insert (struct hash *vm, struct vm_entry *vm_entry)
{   
    if (hash_insert(vm, &vm_entry->elem) == NULL) {
        return true;
    } else {
        return false; 
    }
}

bool vm_entry_delete (struct hash *vm, struct vm_entry *vme)
{
    bool success = false;
    struct hash_elem *deleted_elem;

    lock_acquire(&frame_table_lock);

    deleted_elem = hash_delete(vm, &vme->elem);

    if (deleted_elem != NULL)
    {
        void *kaddr = pagedir_get_page(thread_current()->pagedir, vme->vaddr);
        
        if (kaddr != NULL) {
            free_frame(kaddr);
        }

        free(vme);
        success = true;
    }

    lock_release(&frame_table_lock);
    return success;
}   

struct vm_entry *vm_entry_find (void *vaddr)
{
    struct hash *vm = &thread_current()->vm;
    struct vm_entry tmp;
    struct hash_elem *e;

    tmp.vaddr = pg_round_down(vaddr);

    e = hash_find(vm, &tmp.elem);
    
    if (e == NULL) {
        return NULL; 
    }

    return hash_entry(e, struct vm_entry, elem);
}


void vm_destroy_action(struct hash_elem *e, void *aux UNUSED)
{
    struct vm_entry *vme = hash_entry(e, struct vm_entry, elem);

    lock_acquire(&frame_table_lock);

    if (vme != NULL)
    {
        if (vme->is_loaded)
        {
            void *kaddr = pagedir_get_page(thread_current()->pagedir, vme->vaddr);
            
            if (kaddr != NULL) {
                free_frame(kaddr); 
            }
        }
        
        free(vme);
    }

    lock_release(&frame_table_lock);
    
}

void vm_destroy (struct hash *vm)
{ 
    hash_destroy(vm, vm_destroy_action);
}


bool read_file_to_page (void* addr, struct vm_entry *vm_entry)
{
    int byte_read;
    bool success = false;

    lock_acquire(&filesys_lock);

    byte_read = file_read_at(vm_entry->file, 
                             addr, 
                             vm_entry->bytes_to_read, 
                             vm_entry->offset);
    
    lock_release(&filesys_lock);
    
    if (byte_read != (int)vm_entry->bytes_to_read) {
        success = false;
    } else {
        memset(addr + vm_entry->bytes_to_read, 0, vm_entry->zero_bytes);
        success = true;
    }
    
    return success;
}