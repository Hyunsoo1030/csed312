#include "vm/page.h"
#include "vm/frame.h"
#include <string.h>
#include "threads/vaddr.h"
#include "threads/thread.h"
#include "threads/malloc.h"
#include "filesys/file.h"
#include "vm/swap.h"

// 해시 함수 정의 (vaddr 기반)
static unsigned vm_hash (const struct hash_elem *e, void *aux);
// 정렬 비교 함수 정의 (vaddr 기반)
static bool vm_less (const struct hash_elem *a, const struct hash_elem *b, void *aux);

// 외부에서 선언된 락 (파일 시스템 락, 프레임 테이블 락) 참조
extern struct lock filesys_lock;
extern struct lock frame_table_lock;

/* 보조 페이지 테이블 (SPT) 초기화 */
void vm_init (struct hash *vm) 
{
    // 해시 테이블 초기화: 해시 함수와 비교 함수 지정
    hash_init(vm, vm_hash, vm_less, NULL);
}

/* 두 vm_entry의 가상 주소(vaddr)를 비교 */
static bool vm_less (const struct hash_elem *a, const struct hash_elem *b, void *aux UNUSED)
{
    // 해시 요소(elem)를 vm_entry 구조체로 변환
    struct vm_entry *vm_entry_a = hash_entry(a, struct vm_entry, elem);
    struct vm_entry *vm_entry_b = hash_entry(b, struct vm_entry, elem);
    
    // vaddr 값으로 정렬 순서 결정
    return vm_entry_a->vaddr < vm_entry_b->vaddr;
} 

/* vm_entry의 vaddr을 기반으로 해시 값 계산 */
static unsigned vm_hash (const struct hash_elem *e, void *aux UNUSED)
{
    // 해시 요소를 vm_entry로 변환
    struct vm_entry *vm_entry = hash_entry(e, struct vm_entry, elem);
    // vaddr을 정수형으로 변환하여 해시 값 반환
    return hash_int((int)vm_entry->vaddr);
}   

/* 새로운 vm_entry 구조체 생성 및 초기화 */
struct vm_entry *vm_entry_create ( uint8_t type, void *vaddr, bool is_writable, bool is_loaded, struct file* file, size_t offset, size_t bytes_to_read, size_t zero_bytes)
{
    // vm_entry 구조체 메모리 할당
    struct vm_entry* new_entry = (struct vm_entry*)malloc(sizeof(struct vm_entry));
    
    // 할당 실패 시 NULL 반환
    if (new_entry == NULL) {
        return NULL;
    }

    // 할당된 메모리 0으로 초기화
    memset(new_entry, 0, sizeof(struct vm_entry));

    // 매개변수를 구조체 필드에 대입
    new_entry->type = type;
    new_entry->vaddr = vaddr;
    new_entry->is_writable = is_writable;
    new_entry->is_loaded = is_loaded;
    
    new_entry->file = file;
    new_entry->offset = offset;
    
    new_entry->bytes_to_read = bytes_to_read;
    new_entry->zero_bytes = zero_bytes;

    // 생성된 엔트리 포인터 반환
    return new_entry;
}

/* 보조 페이지 테이블(vm)에 vm_entry 삽입 */
bool vm_entry_insert (struct hash *vm, struct vm_entry *vm_entry)
{   
    // 해시 테이블에 삽입 시도
    if (hash_insert(vm, &vm_entry->elem) == NULL) {
        return true; // 삽입 성공 (중복 없었음)
    } else {
        return false; // 삽입 실패 (중복 vaddr 존재)
    }
}

/* 보조 페이지 테이블(vm)에서 vm_entry 삭제 및 관련 프레임 해제 */
bool vm_entry_delete (struct hash *vm, struct vm_entry *vme)
{
    bool success = false;
    struct hash_elem *deleted_elem;

    // 프레임 테이블 접근 동기화 락 획득
    lock_acquire(&frame_table_lock);

    // 해시 테이블에서 vm_entry 삭제 시도
    deleted_elem = hash_delete(vm, &vme->elem);

    if (deleted_elem != NULL)
    {
        // 페이지 디렉토리를 통해 물리 주소(kaddr) 조회
        void *kaddr = pagedir_get_page(thread_current()->pagedir, vme->vaddr);
        
        // 물리 페이지가 로드되어 있었다면 프레임 해제 및 PTE 정리
        if (kaddr != NULL) {
            free_frame(kaddr);
        }

        // vm_entry 구조체 메모리 해제
        free(vme);
        success = true;
    }

    // 락 해제
    lock_release(&frame_table_lock);
    return success;
}   

/* 가상 주소(vaddr)에 해당하는 vm_entry 검색 */
struct vm_entry *vm_entry_find (void *vaddr)
{
    struct hash *vm = &thread_current()->vm;
    struct vm_entry tmp;
    struct hash_elem *e;

    // 검색 키: 가상 주소를 페이지 경계로 내림 (pg_round_down)
    tmp.vaddr = pg_round_down(vaddr);

    // 해시 테이블에서 검색
    e = hash_find(vm, &tmp.elem);
    
    // 검색 실패 시 NULL 반환
    if (e == NULL) {
        return NULL; 
    }

    // 찾은 해시 요소를 vm_entry로 변환하여 반환
    return hash_entry(e, struct vm_entry, elem);
}


/* 프로세스 종료 시 SPT의 각 요소(vm_entry)를 정리하는 액션 함수 */
void vm_destroy_action(struct hash_elem *e, void *aux UNUSED)
{
    // 해시 요소를 vm_entry로 변환
    struct vm_entry *vme = hash_entry(e, struct vm_entry, elem);

    // 프레임 테이블 접근 동기화 락 획득
    lock_acquire(&frame_table_lock);

    if (vme != NULL)
    {
        // 페이지가 메모리에 로드되어 있는지 확인
        if (vme->is_loaded)
        {
            // 물리 주소 조회
            void *kaddr = pagedir_get_page(thread_current()->pagedir, vme->vaddr);
            
            // 물리 페이지가 존재하는 경우 프레임 해제 및 PTE 정리
            if (kaddr != NULL) {
                free_frame(kaddr); 
            }
        }
        
        // vm_entry 구조체 메모리 해제
        free(vme);
    }

    // 락 해제
    lock_release(&frame_table_lock);
    
}

/* 보조 페이지 테이블(vm) 전체 파괴 */
void vm_destroy (struct hash *vm)
{ 
    // 해시 테이블의 모든 요소에 대해 vm_destroy_action 호출 및 테이블 파괴
    hash_destroy(vm, vm_destroy_action);
}


/* 파일에서 데이터를 읽어 페이지(addr)에 로드 */
bool read_file_to_page (void* addr, struct vm_entry *vm_entry)
{
    int byte_read;
    bool success = false;

    // 파일 시스템 접근 동기화 락 획득
    lock_acquire(&filesys_lock);

    // vm_entry 정보를 기반으로 파일의 특정 오프셋에서 데이터를 페이지(addr)로 읽기
    byte_read = file_read_at(vm_entry->file, 
                             addr, 
                             vm_entry->bytes_to_read, 
                             vm_entry->offset);
    
    // 락 해제
    lock_release(&filesys_lock);
    
    // 읽은 바이트 수가 예상과 일치하는지 확인
    if (byte_read != (int)vm_entry->bytes_to_read) {
        success = false;
    } else {
        // 읽고 남은 공간(zero_bytes)을 0으로 채우기
        memset(addr + vm_entry->bytes_to_read, 0, vm_entry->zero_bytes);
        success = true;
    }
    
    return success;
}