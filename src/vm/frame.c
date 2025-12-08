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
struct list_elem *eviction_ptr; // 페이지 교체를 위한 포인터(clock hand)


// 프레임 테이블 초기화
void ft_init(void)
{
    list_init(&frame_table);
	lock_init(&frame_table_lock);
	eviction_ptr = NULL;
}

// 프레임 테이블에 새로운 프레임 추가
void frame_add(struct frame *frame)
{
    list_push_back(&frame_table, &frame->table_elem);
}

// 프레임 테이블에서 프레임 제거
void frame_remove(struct frame *frame)
{	
	// 이 프레임이 가리키고 있던 위치가 eviction_ptr이라면, eviction_ptr를 다음으로 이동
	if (eviction_ptr == &frame->table_elem) eviction_ptr = list_remove(eviction_ptr);
	// 그렇지 않다면, 단순히 리스트에서 제거
	else if (eviction_ptr != &frame->table_elem) list_remove(&frame->table_elem);
}

// 새로운 프레임 할당 함수 (필요 시 페이지 교체 수행)
struct frame* allocate_frame(enum palloc_flags flags)
{
    struct frame *frame = NULL; 
    
    ASSERT(flags & PAL_USER); // 반드시 user 영역에서만 할당하도록 제한.

	// 프레임 구조체 할당을 위해 메모리 할당
    frame = (struct frame *)malloc(sizeof(struct frame));
    if (frame == NULL) {
        return NULL;
    }
    memset(frame, 0, sizeof(struct frame));
    
    frame->thread = thread_current(); // 프레임을 소유한 스레드 설정
    frame->pinned = false; 

	// 실제 물리 페이지 할당 시도
    frame->page_addr = palloc_get_page(flags);
    
	// 페이지 할당에 실패한 경우, 페이지 교체를 시도하여 프레임 확보
    while (frame->page_addr == NULL) {
        evict_page_frame(); // 페이지 교체 수행
        frame->page_addr = palloc_get_page(flags); // 다시 페이지 할당 시도
    }
    
    ASSERT(pg_ofs(frame->page_addr) == 0);  // 페이지 오프셋이 0인지 확인
    
    frame_add(frame);   // 프레임 테이블에 새 프레임 추가

    return frame;
}

// 가상 주소로 프레임 찾기
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

// 물리 주소로 프레임 찾기
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

// 프레임 해제 함수
void free_frame(void *addr)
{
	struct frame *frame = find_frame_by_paddr(addr);
	if (frame) 
	{
		frame->vm_entry->is_loaded = false; // 프레임이 더 이상 로드되지 않았음을 표시

		pagedir_clear_page(frame->thread->pagedir, frame->vm_entry->vaddr); // 페이지 디렉토리에서 매핑 제거

		palloc_free_page(frame->page_addr); // 물리 페이지 해제
		frame_remove(frame); // 프레임 테이블에서 프레임 제거
		free(frame); // 프레임 구조체 메모리 해제
	}
}
// 프레임을 pinned 상태로 설정 (eviction에서 제외)
void pin_frame(void *kaddr)
{
	struct frame *frame;
	frame = find_frame_by_paddr(kaddr);
	frame->pinned = true;
}

// 프레임을 pinned 상태 해제 (eviction 가능)
void unpin_frame(void *kaddr)
{
	struct frame *frame;
	frame = find_frame_by_paddr(kaddr);
	frame->pinned = false;
}

// 페이지 교체 알고리즘(Second Chance Clock)으로 victim 프레임 선택
struct frame* chose_victim_frame()
{
    struct list_elem *e;
    struct frame *frame;
    
    if (list_empty(&frame_table)) {
        return NULL;
    }
    
    while (true)
    { 	
		// eviction_ptr가 NULL이거나 리스트 끝에 도달했으면 처음으로 되돌림
        if (eviction_ptr == NULL || eviction_ptr == list_end(&frame_table)) { 
            eviction_ptr = list_begin(&frame_table);
        }
        
        e = eviction_ptr;
        frame = list_entry(e, struct frame, table_elem);

        eviction_ptr = list_next(eviction_ptr); // 다음 프레임으로 이동	

        if (!frame->pinned) // pinned 상태가 아닌 프레임만 고려
        {	
			// Accessed bit 검사 -> clock 알고리즘 적용
            if (!pagedir_is_accessed(frame->thread->pagedir, frame->vm_entry->vaddr)) {
                return frame; 
            }
            else {
                pagedir_set_accessed(frame->thread->pagedir, frame->vm_entry->vaddr, false); 
            }
        }
    }
}

// victim 프레임을 페이지 교체 후 해제하는 함수
void evict_page_frame()
{
    struct frame *frame = chose_victim_frame();
    
    bool is_modified = pagedir_is_dirty(frame->thread->pagedir, frame->vm_entry->vaddr);
    
	// vm_entry 타입에 따라 적절한 처리 수행
    if (frame->vm_entry->type == VM_FILE) 
    {
        if (is_modified) // 파일 매핑된 페이지가 수정된 경우, 파일에 다시 써줌(write back)
        {   
            lock_acquire(&filesys_lock);
            file_write_at(frame->vm_entry->file, frame->page_addr, frame->vm_entry->bytes_to_read, frame->vm_entry->offset);
            lock_release(&filesys_lock);
        }
    }
    else if (frame->vm_entry->type == VM_BIN)
    {
        if (is_modified) // 실행 파일에서 로드된 페이지가 수정된 경우, swap 영역에 저장
        {   
            frame->vm_entry->swap_slot = swap_out(frame->page_addr);
            frame->vm_entry->type = VM_ANON;
        }
    }
    else if (frame->vm_entry->type == VM_ANON)
    { // Anonymous 페이지인 경우, swap 영역에 저장
        frame->vm_entry->swap_slot = swap_out(frame->page_addr);
    }
    
	 // 페이지 디렉토리에서 매핑 제거
    pagedir_clear_page(frame->thread->pagedir, frame->vm_entry->vaddr);
    
	// 물리 페이지 해제
    palloc_free_page(frame->page_addr);
    
    frame_remove(frame); // 프레임 테이블에서 프레임 제거
    frame->vm_entry->is_loaded = false;
    free(frame); // 프레임 구조체 메모리 해제
}