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
struct lock frame_lock; // modified for p3

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

    default:
      exit(-1);
  }
}

//modified for p2
void is_valid_addr(void *addr)
{
  //if (!addr || !is_user_vaddr(addr)) 
  //  exit(-1);
  if (!addr || !is_user_vaddr(addr) || !pagedir_get_page(thread_current()->pagedir, addr)) 
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
  size_t remained = strlen(cmd_line)+1;
  void *buffer_temp = (void*)cmd_line;
  while(remained > 0)
  {
    // size_t ofs = buffer_temp - pg_round_down(buffer_temp);
    struct vm_entry* vme = vme_find(pg_round_down(buffer_temp));
    if(vme)
    {
      if(!vme->is_loaded)
      {
        if (!handle_fault(vme))
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
    
    lock_acquire(&frame_lock);
    size_t read_bt = remained > PGSIZE - pg_ofs(buffer_temp) ? PGSIZE - pg_ofs(buffer_temp) : remained;
    struct frame* frame_to_pin = find_frame_for_vaddr(pg_round_down(buffer_temp));
    frame_pin(frame_to_pin->page_addr);
    lock_release(&frame_lock);
    remained -= read_bt;
    buffer_temp += read_bt;
  }

  // create child process
  pid = process_execute(cmd_line);
  if(pid == -1) return -1; // if fail to create child process

  child = get_child(pid); // get process descriotor of child

  sema_down(&(child->sema_load)); // wait until child is loaded

  // modified for p3
  remained = strlen(cmd_line)+1;
  buffer_temp = (void*)cmd_line;
  while(remained > 0)
  {
    lock_acquire(&frame_lock);
    // size_t ofs = buffer_temp - pg_round_down(buffer_temp);
    size_t read_bt = remained > PGSIZE - pg_ofs(buffer_temp) ? PGSIZE - pg_ofs(buffer_temp) : remained;
    struct frame* frame_to_pin = find_frame_for_vaddr(pg_round_down(buffer_temp));
    frame_unpin(frame_to_pin->page_addr);
    lock_release(&frame_lock);
    remained -= read_bt;
    buffer_temp += read_bt;
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
  size_t remained = size;
  void *buffer_temp = (void*)buffer;
  while(remained > 0)
  {
    // size_t ofs = buffer_temp - pg_round_down(buffer_temp);
    struct vm_entry* vme = vme_find(pg_round_down(buffer_temp));
    if(vme)
    {
      if(!vme->is_loaded)
      {
        if (!handle_fault(vme))
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
    lock_acquire(&frame_lock);
    size_t read_bt = remained > PGSIZE - pg_ofs(buffer_temp) ? PGSIZE - pg_ofs(buffer_temp) : remained;
    struct frame* frame_to_pin = find_frame_for_vaddr(pg_round_down(buffer_temp));
    frame_pin(frame_to_pin->page_addr);
    lock_release(&frame_lock);
    remained -= read_bt;
    buffer_temp += read_bt;
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
  remained = size;
  buffer_temp = (void*)buffer;
  while(remained > 0)
  {
    lock_acquire(&frame_lock);
    // size_t ofs = buffer_temp - pg_round_down(buffer_temp);
    size_t read_bt = remained > PGSIZE - pg_ofs(buffer_temp) ? PGSIZE - pg_ofs(buffer_temp) : remained;
    struct frame* frame_to_pin = find_frame_for_vaddr(pg_round_down(buffer_temp));
    frame_unpin(frame_to_pin->page_addr);
    remained -= read_bt;
    buffer_temp += read_bt;
    lock_release(&frame_lock);
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
  size_t remained = size;
  void *buffer_temp = (void*)buffer;
  while(remained > 0)
  {
    // size_t ofs = buffer_temp - pg_round_down(buffer_temp);
    struct vm_entry* vme = vme_find(pg_round_down(buffer_temp));
    if(vme)
    {
      if(!(vme->is_loaded))
      {
        if (!handle_fault(vme))
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
    
    lock_acquire(&frame_lock);
    size_t write_bt = remained > PGSIZE - pg_ofs(buffer_temp) ? PGSIZE - pg_ofs(buffer_temp) : remained;
    struct frame* frame_to_pin = find_frame_for_vaddr(pg_round_down(buffer_temp));
    frame_pin(frame_to_pin->page_addr);
    remained -= write_bt;
    buffer_temp += write_bt;
    lock_release(&frame_lock);
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
 remained = size;
 buffer_temp = (void*)buffer;
 
 while(remained > 0)
  {
    lock_acquire(&frame_lock);
    // size_t ofs = buffer_temp - pg_round_down(buffer_temp);
    size_t write_bt = remained > PGSIZE - pg_ofs(buffer_temp) ? PGSIZE - pg_ofs(buffer_temp) : remained;
    struct frame* frame_to_pin = find_frame_for_vaddr(pg_round_down(buffer_temp));
    frame_unpin(frame_to_pin->page_addr);
    remained -= write_bt;
    buffer_temp += write_bt;
    lock_release(&frame_lock);
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
  if(is_kernel_vaddr(addr))
    exit(-1);
  // addr이 0인 경우, addr이 page 정렬되지 않은 경우
  if(!addr || pg_ofs(addr) != 0 || (int)addr%PGSIZE !=0)
    return -1;

  // for vm_entry
  int file_remained;
  size_t offset = 0;

  // 1. mmap_file 구조체 생성 및 메모리 할당
	struct mmap_file *mfe = (struct mmap_file *)malloc(sizeof(struct mmap_file));
  if (!mfe) return -1;   
	memset(mfe, 0, sizeof(struct mmap_file));

	// 2. file open
  lock_acquire(&filesys_lock);
  struct file* file = file_reopen(process_get_file(fd));
  file_remained = file_length(file);
  lock_release(&filesys_lock);
  // fd로 열린 파일의 길이가 0바이트인 경우
  if (!file_remained) 
  {
    return -1;
  }


	// 3. vm_entry 할당
	list_init(&mfe->vme_list);	
  
	while(file_remained > 0)// file 다 읽을 때 까지 반복
	{
		// vm entry 할당
    if (vme_find(addr)) return -1;

    size_t page_read_bytes = file_remained < PGSIZE ? file_remained : PGSIZE;
    size_t page_zero_bytes = PGSIZE - page_read_bytes;

    struct vm_entry* vme = vme_construct(VM_FILE, addr, true, false, file, offset, page_read_bytes, page_zero_bytes);
    if (!vme) 
      return false;

		// 2. vme_list에 mmap_elem과 연결된 vm entry 추가
    list_push_back(&mfe->vme_list, &vme->mmap_elem);
		// 3. current thread에 대해 vme insert
    vme_insert(&thread_current()->vm, vme);
		
    // 4. file addr, offset 업데이트 (page size만큼)
    addr += PGSIZE;
    offset += PGSIZE;
		// 5. file에 남은 길이 업데이트 (page size만큼)
    file_remained -= PGSIZE;
	}

  // 4. mmap_list, mmap_next 관리
  mfe->mapid = thread_current()->mmap_next++;
  list_push_back(&thread_current()->mmap_list, &mfe->elem);
  mfe->file = file;
	return mfe->mapid;
}


void munmap(mapid_t mapid)
{
  // 1. thread의 mmap_list에서 mapid에 해당하는 mfe 찾기
	struct mmap_file *mfe = NULL;
  struct list_elem *e;
  for (e = list_begin(&thread_current()->mmap_list); e != list_end(&thread_current()->mmap_list); e = list_next(e))
  {
    mfe = list_entry (e, struct mmap_file, elem);
    if (mfe->mapid == mapid) break;
  }
  if(mfe == NULL) return;

	// 2. 해당 mfe의 vme_list를 돌면서 vme를 지우기
	for (e = list_begin(&mfe->vme_list); e != list_end(&mfe->vme_list);)
  {
    struct vm_entry *vme = list_entry(e, struct vm_entry, mmap_elem);
    if(vme->is_loaded && (pagedir_is_dirty(thread_current()->pagedir, vme->vaddr)))
    {
      lock_acquire(&filesys_lock);
      file_write_at(vme->file, vme->vaddr, vme->read_bytes, vme->offset);
      lock_release(&filesys_lock);
      lock_acquire(&frame_lock);
      free_frame(pagedir_get_page(thread_current()->pagedir, vme->vaddr));
      lock_release(&frame_lock);
    }
    vme->is_loaded = false;
    e = list_remove(e);
    vme_delete(&thread_current()->vm, vme);
  }
	// 4. mfe를 mmap_list에서 제거
  list_remove(&mfe->elem);
  // 5. mfe 구조체 자체를 free
  free(mfe); 
}