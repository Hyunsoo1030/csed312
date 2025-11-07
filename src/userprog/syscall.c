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
  //thread_current()->esp = f->esp;
  
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
      f->eax = exec((const char*)argv[0]);
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
      f->eax = read((int)argv[0], (void *)argv[1], (unsigned)argv[2]);
      break;
    case SYS_WRITE:
      get_argument(f->esp+4, argv, 3);
      f->eax = write((int)argv[0], (const void *)argv[1], (unsigned)argv[2]);
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


pid_t exec (const char *cmd_line)
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

  // create child process
  pid = process_execute(cmd_line);
  if(pid == -1) return -1; // if fail to create child process

  child = get_child(pid); // get process descriotor of child

  sema_down(&(child->sema_load)); // wait until child is loaded

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

  /*lock_acquire(&filesys_lock);
  bool success = filesys_create(file, initial_size);
  lock_release(&filesys_lock);
  
  return success;*/
  return filesys_create(file, initial_size);
}

bool remove(const char* file)
{
  //is_valid_addr((void*)file);

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
 if(f==NULL){
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

int read (int fd, void *buffer, unsigned size)
{
  int bytes_read=0;
  struct file *f;
  
  unsigned i;
  for (i = 0; i < size; i++)
    is_valid_addr(buffer+i);
  
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
  return bytes_read;
}

int write (int fd, const void *buffer, unsigned size)
{
  int bytes_write = 0;
  struct file* f;
  unsigned i;
  for (i = 0; i < size; i++){
    is_valid_addr(buffer+i);
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
  //lock_acquire(&filesys_lock);
  struct file *f = process_get_file(fd);
  if (f){
    lock_release(&filesys_lock);

    return file_tell(f);
  }
  else{
    //lock_release(&filesys_lock);

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
