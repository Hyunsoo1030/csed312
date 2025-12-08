#include "vm/swap.h"
#include <bitmap.h>
#include "threads/synch.h"
#include "devices/block.h"
#include "threads/vaddr.h"
#include "threads/interrupt.h"

// 페이지당 필요한 섹터 수 정의
#define SECTOR_NUM_PER_PAGE (PGSIZE/BLOCK_SECTOR_SIZE)

// 스왑 영역 접근 시 동기화를 위한 락
static struct lock lock_swap;
// 스왑 슬롯 사용 여부를 추적하는 비트맵
static struct bitmap *swap_bitmap;
// 스왑 파티션 블록 장치 포인터
static struct block *swap_block;

void swap_init()
{
    // 1. swap block device
    swap_block = block_get_role(BLOCK_SWAP);
    
    // 2. 스왑 슬롯 총 개수 계산 및 비트맵 생성
    size_t swap_bitmap_size = block_size(swap_block) / SECTOR_NUM_PER_PAGE;
    swap_bitmap = bitmap_create(swap_bitmap_size);
    
    // 3. 락 초기화
    lock_init(&lock_swap);
}

bool swap_in(size_t slot_index, void *kaddr)
{
    int start_sector;
    int i;
    
    // 1. 해당 슬롯의 시작 섹터 번호 계산
    start_sector = SECTOR_NUM_PER_PAGE * slot_index;

    // 2. 동기화 시작: 락 획득
    lock_acquire(&lock_swap); 
    
    // 3. 페이지 전체를 섹터 단위로 읽기
    for (i = 0; i < SECTOR_NUM_PER_PAGE; i++)
    {   
        // 스왑 블록에서 kaddr 메모리 공간으로 데이터 읽기
        block_read(swap_block, start_sector + i, kaddr + i * BLOCK_SECTOR_SIZE);
    }
    
    // 4. 스왑 완료 후, 해당 슬롯을 사용 가능(false) 상태로 설정
    bitmap_set(swap_bitmap, slot_index, false);

    // 5. 동기화 종료: 락 해제
    lock_release(&lock_swap); 

    return true;
}

size_t swap_out(void* kaddr)
{
    size_t slot_index;
    int start_sector;
    int i;

    lock_acquire(&lock_swap);

    // 1. 사용 가능한 슬롯을 검색 및 발견 즉시 사용 중(true)으로 설정
    slot_index = bitmap_scan_and_flip(swap_bitmap, 0, 1, false);

    // 2. 스왑 공간이 가득 차 슬롯을 찾지 못한 경우 처리
    if (slot_index == BITMAP_ERROR)
    {
        lock_release(&lock_swap);
        NOT_REACHED(); 
        return BITMAP_ERROR;
    }

    // 3. 해당 슬롯의 시작 섹터 번호 계산
    start_sector = SECTOR_NUM_PER_PAGE * slot_index;
    
    // 4. 물리 페이지 전체를 섹터 단위로 스왑 블록에 기록
    for (i = 0; i < SECTOR_NUM_PER_PAGE; i++)
    {
        // kaddr의 데이터를 스왑 블록의 해당 섹터에 기록
        block_write(swap_block, start_sector + i, kaddr + i * BLOCK_SECTOR_SIZE);
    }
    
    lock_release(&lock_swap);
    
    // 5. 할당된 스왑 슬롯 인덱스 반환
    return slot_index;
}