#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
/* modified for p2 */
#include "threads/vaddr.h"
#include "devices/shutdown.h"
#include "devices/input.h"
#include "filesys/filesys.h"
#include "filesys/file.h"
#include "userprog/process.h"
#include <string.h>

// modified for p3
#include "vm/page.h"
#include "vm/frame.h"

static void syscall_handler (struct intr_frame *);
struct lock filesys_lock; // modified for p2


void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
  lock_init(&filesys_lock); // modified for p2
}

static void
syscall_handler (struct intr_frame *f UNUSED) 
{
  //printf ("system call!\n");
  //thread_exit ();

  //modified for p2
  thread_current()->esp = f->esp;
  
  is_valid_addr((void *)(f->esp));
  int i;
  for (i = 0; i < 3; i++) {
    is_valid_addr(f->esp + 4*i);
  }
  int argv[3];
  switch(*(uint32_t *)(f->esp))
  {
    case SYS_HALT:
      halt();
      break;
    case SYS_EXIT:
      get_argument(f->esp+4, argv, 1);
      exit((int)argv[0]);
      break;
    case SYS_EXEC:
      get_argument(f->esp+4, argv, 1);
      f->eax = exec((const char*)argv[0], f->esp);
      break;
    case SYS_WAIT:
      get_argument(f->esp+4, argv, 1);
      f->eax = wait((pid_t)argv[0]);
      break;
    case SYS_CREATE:
      get_argument(f->esp+4, argv, 2);
      f->eax = create((const char*)argv[0], (unsigned)argv[1]);
      break;
    case SYS_REMOVE:
      get_argument(f->esp+4, argv, 1);
      f->eax = remove((const char*)argv[0]);
      break;
    case SYS_OPEN:
      get_argument(f->esp+4, argv, 1);
      f->eax = open((const char*)argv[0]);
      break;
    case SYS_FILESIZE:
      get_argument(f->esp+4, argv, 1);
      f->eax = filesize((int)argv[0]);
      break;
    case SYS_READ:
      get_argument(f->esp+4, argv, 3);
      f->eax = read((int)argv[0], (void *)argv[1], (unsigned)argv[2], f->esp);
      break;
    case SYS_WRITE:
      get_argument(f->esp+4, argv, 3);
      f->eax = write((int)argv[0], (const void *)argv[1], (unsigned)argv[2], f->esp);
      break;
    case SYS_SEEK:
      get_argument(f->esp+4, argv, 2);
      seek((int)argv[0], (unsigned)argv[1]);
      break;
    case SYS_TELL:
      get_argument(f->esp+4, argv, 1);
      f->eax = tell((int)argv[0]);
      break;
    case SYS_CLOSE:
      get_argument(f->esp+4, argv, 1);
      close((int)argv[0]);
      break;
    // modified for p3
    case SYS_MMAP:
      get_argument(f->esp+4, argv, 2);
      f->eax = mmap(argv[0], (void *)argv[1]);
      break;
    case SYS_MUNMAP:
      get_argument(f->esp+4, argv, 1);
      munmap(argv[0]);
      break;
    default:
      exit(-1);
  }
}

//modified for p3
void is_valid_addr(void *addr)
{
  if (!addr || !is_user_vaddr(addr)) 
    exit(-1);
}

//modified for p2
void get_argument(void *esp, int *arg, int count)
{
  int i;
  void* arg_pos;
  for (i=0;i<count;i++){
    arg_pos=esp+4*i;
    is_valid_addr(arg_pos);
    arg[i]=*(int*)(arg_pos);
  }
}

void halt(void)
{
  shutdown_power_off();
}

void exit(int exit_code)
{
  thread_current()->exit_status = exit_code;
  thread_exit();
}


pid_t exec (const char *cmd_line, void *esp)
{
  char *ptr = cmd_line;
  struct thread* child;
  pid_t pid;

  while (true) {
    is_valid_addr(ptr);   // check cmd_line addr
    if(*ptr == '\0') 
      break;
    ptr++;
  }

  // modified for p3
  // pinning
  size_t left = strlen(cmd_line) + 1;
  void *buffer_temp = (void*)cmd_line;

  while (left > 0)
  {
    void *page_base = pg_round_down(buffer_temp);
    struct vm_entry* vm_entry = vm_entry_find(page_base);

    if (vm_entry != NULL)
    {
      if (!vm_entry->is_loaded)
      {
        if (!fault_handling(vm_entry))
        {
          exit(-1);
        }
      }
    }
    else
    {
      uint32_t base = 0xC0000000;
      uint32_t limit = 0x800000;
      uint32_t lowest_stack_addr = base - limit;

      if ((buffer_temp >= (esp - 32)) && (buffer_temp >= lowest_stack_addr))
      {
        if (!expand_stack(buffer_temp))
        {
          exit(-1);
        }
      }
      else
      {
        exit(-1);
      }
    }

    lock_acquire(&frame_table_lock);
    
    size_t remaining_in_page = PGSIZE - pg_ofs(buffer_temp);
    size_t read_byte = left > remaining_in_page ? remaining_in_page : left;
    
    struct frame* frame_to_pin = find_frame_by_vaddr(page_base);
    pin_frame(frame_to_pin->page_addr);
    
    lock_release(&frame_table_lock);

    left -= read_byte;
    buffer_temp += read_byte;
  }

  // create child process
  pid = process_execute(cmd_line);
  if(pid == -1) return -1; // if fail to create child process

  child = get_child(pid); // get process descriotor of child

  sema_down(&(child->sema_load)); // wait until child is loaded

  // modified for p3
  left = strlen(cmd_line)+1;
  buffer_temp = (void*)cmd_line;
  while(left > 0)
  {
    lock_acquire(&frame_table_lock);
    // size_t ofs = buffer_temp - pg_round_down(buffer_temp);
    size_t read_byte = left > PGSIZE - pg_ofs(buffer_temp) ? PGSIZE - pg_ofs(buffer_temp) : left;
    struct frame* frame_to_pin = find_frame_by_vaddr(pg_round_down(buffer_temp));
    unpin_frame(frame_to_pin->page_addr);
    lock_release(&frame_table_lock);
    left -= read_byte;
    buffer_temp += read_byte;
  }
  
  if(child->isload) return pid;
  else return -1;
}

int wait (pid_t pid)
{
  return process_wait(pid);
}

/*--------file system call---------*/

bool create(const char* file, unsigned initial_size)
{ 
  is_valid_addr((void*)file);
  if(!file) exit(-1);

  lock_acquire(&filesys_lock);
  bool success = filesys_create(file, initial_size);
  lock_release(&filesys_lock);
  
  return success;
}

bool remove(const char* file)
{
  is_valid_addr((void*)file);
  return filesys_remove(file);
}

int open (const char *file)
{
 int fd;
 struct file* f;
 struct thread* cur;

 is_valid_addr((void*)file);

 lock_acquire(&filesys_lock);
 f = filesys_open(file);
 if(f==NULL)
 {
  lock_release(&filesys_lock);
  return -1;
 }

 /* deny write */
 if(!strcmp(thread_current()->name, file)){
  file_deny_write(f);
 }
 
 /* add file to process */ 
 cur = thread_current();
 fd = cur->fd_max;

 cur->fd_table[fd] = f;
 cur->fd_max++;

 lock_release(&filesys_lock);
 return fd;
}

int filesize (int fd)
{
  struct file* f;
  lock_acquire(&filesys_lock);
  f = process_get_file(fd);
  if(f){
    lock_release(&filesys_lock);
    return file_length(f);
  }
  lock_release(&filesys_lock);
  return -1;
}

struct file *process_get_file(int fd)
{
  struct file *f;

  if( (fd > 1) && (fd < thread_current()->fd_max)){
    f = thread_current()->fd_table[fd];
    return f;
  }
  return NULL; 
}

int read (int fd, void *buffer, unsigned size, void *esp)
{
  int bytes_read=0;
  struct file *f;
  
  unsigned i;
  for (i = 0; i < size; i++)
    is_valid_addr(buffer+i);
  
  // modified for p3
  // pinning
  size_t left = size;
  void *buffer_temp = (void*)buffer;
  while(left > 0)
  {
    // size_t ofs = buffer_temp - pg_round_down(buffer_temp);
    struct vm_entry* vm_entry = vm_entry_find(pg_round_down(buffer_temp));
    if(vm_entry)
    {
      if(!vm_entry->is_loaded)
      {
        if (!fault_handling(vm_entry))
        {
          exit(-1);
        }
      }
    }
    else
    {
      uint32_t base = 0xC0000000;
      uint32_t limit = 0x800000;
      uint32_t lowest_stack_addr = base-limit;
      if ( (buffer_temp >= (esp-32)) && (buffer_temp >= lowest_stack_addr))
      {
        if (!expand_stack(buffer_temp))
        {
          exit(-1);
        }
      }
      else
      {
        exit(-1);
      }
    }
    lock_acquire(&frame_table_lock);
    size_t read_byte = left > PGSIZE - pg_ofs(buffer_temp) ? PGSIZE - pg_ofs(buffer_temp) : left;
    struct frame* frame_to_pin = find_frame_by_vaddr(pg_round_down(buffer_temp));
    pin_frame(frame_to_pin->page_addr);
    lock_release(&frame_table_lock);
    left -= read_byte;
    buffer_temp += read_byte;
  }

  if(fd==0){
    for (i = 0; i < size;i++){
      ((char*)buffer)[i]=input_getc();
      if(((char*)buffer)[i] == '\0')
        break;
      bytes_read = i;
    }
  }
  else if(fd > 0){
    f = process_get_file(fd);
    if(!f) return -1;

    lock_acquire(&filesys_lock);
    bytes_read += file_read(f, buffer, size);
    lock_release(&filesys_lock);
  }

  // modified for p3
  left = size;
  buffer_temp = (void*)buffer;
  while(left > 0)
  {
    lock_acquire(&frame_table_lock);
    size_t read_byte = left > PGSIZE - pg_ofs(buffer_temp) ? PGSIZE - pg_ofs(buffer_temp) : left;
    struct frame* frame_to_pin = find_frame_by_vaddr(pg_round_down(buffer_temp));
    unpin_frame(frame_to_pin->page_addr);
    left -= read_byte;
    buffer_temp += read_byte;
    lock_release(&frame_table_lock);
  }

  return bytes_read;
}

int write (int fd, const void *buffer, unsigned size, void *esp)
{
  int bytes_write = 0;
  struct file* f;
  unsigned i;
  for (i = 0; i < size; i++){
    is_valid_addr(buffer+i);
  }

  // modified for p3
  // pinning
  size_t left = size;
  void *buffer_temp = (void*)buffer;
  while (left > 0)
{
    void *current_page_base = pg_round_down(buffer_temp);
    struct vm_entry* vm_entry = vm_entry_find(current_page_base);
    
    if (vm_entry != NULL)
    {
        if (!vm_entry->is_loaded)
        {
            if (!fault_handling(vm_entry))
            {
                exit(-1);
            }
        }
    }
    else
    {
        uint32_t base = 0xC0000000;
        uint32_t limit = 0x800000;
        uint32_t lowest_stack_addr = base - limit;

        if ((buffer_temp >= (esp - 32)) && (buffer_temp >= lowest_stack_addr))
        {
            if (!expand_stack(buffer_temp))
            {
                exit(-1); 
            }
        }
        else
        {
            exit(-1); 
        }
    }
    
    lock_acquire(&frame_table_lock);
    
    size_t page_offset = pg_ofs(buffer_temp);
    size_t bytes_in_current_page = PGSIZE - page_offset;
    size_t read_byte = left > bytes_in_current_page ? bytes_in_current_page : left;
    
    struct frame* frame_to_pin = find_frame_by_vaddr(current_page_base);
    pin_frame(frame_to_pin->page_addr);
    
    lock_release(&frame_table_lock);

    left -= read_byte;
    buffer_temp += read_byte;
}

 if(fd == 1){
  lock_acquire(&filesys_lock);
  putbuf(buffer, size);
  lock_release(&filesys_lock);

  bytes_write = size;
 }
 else if(fd > 1){
    f = process_get_file(fd);
    if(!f) return -1;

    lock_acquire(&filesys_lock);
    bytes_write += file_write(f, buffer, size);
    lock_release(&filesys_lock);
 }

 // modified for p3
 left = size;
 buffer_temp = (void*)buffer;
 
 while(left > 0)
  {
    lock_acquire(&frame_table_lock);
    size_t write_byte = left > PGSIZE - pg_ofs(buffer_temp) ? PGSIZE - pg_ofs(buffer_temp) : left;
    struct frame* frame_to_pin = find_frame_by_vaddr(pg_round_down(buffer_temp));
    unpin_frame(frame_to_pin->page_addr);
    left -= write_byte;
    buffer_temp += write_byte;
    lock_release(&frame_table_lock);
  }

 return bytes_write;

}

void seek (int fd, unsigned position)
{
  lock_acquire(&filesys_lock);
  struct file* f = process_get_file(fd);

  ASSERT(f != NULL);
  file_seek(f, position);

  lock_release(&filesys_lock);
}

unsigned tell (int fd)
{
  lock_acquire(&filesys_lock);
  struct file *f = process_get_file(fd);
  if (f){
    lock_release(&filesys_lock);

    return file_tell(f);
  }
  else{
    lock_release(&filesys_lock);
    return -1;
  }
}

void close (int fd)
{
  struct file *f = process_get_file(fd);
  if(f){
    if((fd>1) && (fd<thread_current()->fd_max)){
      file_close(f);
      thread_current()->fd_table[fd] = NULL;
    }
  }
}

// modified for p3
mapid_t mmap(int fd, void* addr)
{
  if (is_kernel_vaddr(addr)) {
    exit(-1);
  }

  if (pg_ofs(addr) != 0 || (int)addr % PGSIZE != 0 || !addr) {
    return -1;
  }
  
  int left_page;
  size_t ofs = 0;
  
  struct mmap_file *mapped_file = (struct mmap_file *)malloc(sizeof(struct mmap_file));
  if (mapped_file == NULL) {
    return -1;
  }
  memset(mapped_file, 0, sizeof(struct mmap_file));

  lock_acquire(&filesys_lock);
  struct file* file = file_reopen(process_get_file(fd)); // fd에 해당하는 file 얻고 복제
  
  if (file == NULL) {
      lock_release(&filesys_lock);
      free(mapped_file);
      return -1;
  }
  
  left_page = file_length(file);
  lock_release(&filesys_lock);

  if (left_page <= 0) { 
    free(mapped_file);
    return -1;
  }
  
  list_init(&mapped_file->mapping_pages);	

  while (left_page > 0)
  {
    if (vm_entry_find(addr)) {
      return -1; 
    }

    size_t left_byte = left_page < PGSIZE ? left_page : PGSIZE;
    size_t zero_bytes = PGSIZE - left_byte;

    struct vm_entry* vm_entry = vm_entry_create(VM_FILE, addr, true, false, file, ofs, left_byte, zero_bytes);
    if (!vm_entry) {
      return -1; 
    }

    list_push_back(&mapped_file->mapping_pages, &vm_entry->mmap_elem);
    vm_entry_insert(&thread_current()->vm, vm_entry);
		
    addr += PGSIZE;
    left_page -= PGSIZE;
    ofs += PGSIZE;
  }

  mapped_file->mapid = thread_current()->mmap_next++;
  
  list_push_back(&thread_current()->mmap_list, &mapped_file->elem);
  
  mapped_file->file = file;
  
	return mapped_file->mapid;
}

void munmap(mapid_t mapid)
{
	struct mmap_file *mapped_file = NULL;
  struct list_elem *e;
  
  for (e = list_begin(&thread_current()->mmap_list); 
       e != list_end(&thread_current()->mmap_list); 
       e = list_next(e))
  {
    mapped_file = list_entry (e, struct mmap_file, elem);
    if (mapped_file->mapid == mapid) {
      break; 
    }
  }
  
  if (mapped_file == NULL) {
    return;
  }

	struct list_elem *next_e;
  e = list_begin(&mapped_file->mapping_pages);
  
  while (e != list_end(&mapped_file->mapping_pages))
  {
    struct vm_entry *vm_entry = list_entry(e, struct vm_entry, mmap_elem);
    
    next_e = list_next(e); 
    
    if (vm_entry->is_loaded && pagedir_is_dirty(thread_current()->pagedir, vm_entry->vaddr))
    {
      lock_acquire(&filesys_lock);
      file_write_at(vm_entry->file, 
                    vm_entry->vaddr, 
                    vm_entry->bytes_to_read, 
                    vm_entry->offset);
      lock_release(&filesys_lock);
      
      lock_acquire(&frame_table_lock);
      free_frame(pagedir_get_page(thread_current()->pagedir, vm_entry->vaddr)); 
      lock_release(&frame_table_lock);
    }
    
    vm_entry->is_loaded = false;
    list_remove(e);
    vm_entry_delete(&thread_current()->vm, vm_entry);
    
    e = next_e; 
  }
  list_remove(&mapped_file->elem);
  
  free(mapped_file); 
}