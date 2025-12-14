/*
 * Copyright (C) 2026 pdnguyen of HCMC University of Technology VNU-HCM
 */

/* LamiaAtrium release
 * Source Code License Grant: The authors hereby grant to Licensee
 * personal permission to use and modify the Licensed Source Code
 * for the sole purpose of studying while attending the course CO2018.
 */

/*
 * PAGING based Memory Management
 * Memory management unit mm/mm.c
 */

#include "mm64.h"
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <string.h>
#include <inttypes.h>
#include <stdlib.h>

#include <pthread.h> // Đảm bảo có include này

// --- BIẾN TOÀN CỤC CHO THỐNG KÊ HỆ THỐNG ---
static uint64_t g_total_accesses = 0;       // Tổng số lần truy cập
static uint64_t g_total_storage_bytes = 0;  // Tổng dung lượng bảng trang
static pthread_mutex_t g_stats_lock = PTHREAD_MUTEX_INITIALIZER; // Khóa bảo vệ

// Hàm in báo cáo tổng kết (Sẽ gọi ở cuối os.c)
void print_system_stats() {
    printf("\n===========================================================================\n");
    printf("                  SYSTEM-WIDE MEMORY STATISTICS REPORT                       \n");
    printf("===========================================================================\n");
    printf(" 1. Total Memory Access Times (All Processes): %" PRIu64 " accesses\n", g_total_accesses);
    printf(" 2. Total Page Table Storage Size            : %" PRIu64 " bytes (%.2f KB)\n", 
           g_total_storage_bytes, g_total_storage_bytes / 1024.0);
    printf("===========================================================================\n\n");
}

#if defined(MM64)

/*
 * init_pte - Initialize PTE entry
 */
int init_pte(addr_t *pte,
             int pre,    // present
             addr_t fpn,    // FPN
             int drt,    // dirty
             int swp,    // swap
             int swptyp, // swap type
             addr_t swpoff) // swap offset
{
  if (pre != 0) {
    if (swp == 0) { // Non swap ~ page online
      if (fpn == 0)
        return -1;  // Invalid setting

      /* Valid setting with FPN */
      SETBIT(*pte, PAGING_PTE_PRESENT_MASK);
      CLRBIT(*pte, PAGING_PTE_SWAPPED_MASK);
      CLRBIT(*pte, PAGING_PTE_DIRTY_MASK);

      SETVAL(*pte, fpn, PAGING_PTE_FPN_MASK, PAGING_PTE_FPN_LOBIT);
    }
    else
    { // page swapped
      SETBIT(*pte, PAGING_PTE_PRESENT_MASK);
      SETBIT(*pte, PAGING_PTE_SWAPPED_MASK);
      CLRBIT(*pte, PAGING_PTE_DIRTY_MASK);

      SETVAL(*pte, swptyp, PAGING_PTE_SWPTYP_MASK, PAGING_PTE_SWPTYP_LOBIT);
      SETVAL(*pte, swpoff, PAGING_PTE_SWPOFF_MASK, PAGING_PTE_SWPOFF_LOBIT);
    }
  }

  return 0;
}


/*
 * get_pd_from_pagenum - Parse address to 5 page directory level
 * @pgn   : pagenumer
 * @pgd   : page global directory
 * @p4d   : page level directory
 * @pud   : page upper directory
 * @pmd   : page middle directory
 * @pt    : page table 
 */
int get_pd_from_address(addr_t addr, addr_t* pgd, addr_t* p4d, addr_t* pud, addr_t* pmd, addr_t* pt)
{
	/* Extract page direactories */
	*pgd = (addr&PAGING64_ADDR_PGD_MASK)>>PAGING64_ADDR_PGD_LOBIT;
	*p4d = (addr&PAGING64_ADDR_P4D_MASK)>>PAGING64_ADDR_P4D_LOBIT;
	*pud = (addr&PAGING64_ADDR_PUD_MASK)>>PAGING64_ADDR_PUD_LOBIT;
	*pmd = (addr&PAGING64_ADDR_PMD_MASK)>>PAGING64_ADDR_PMD_LOBIT;
	*pt = (addr&PAGING64_ADDR_PT_MASK)>>PAGING64_ADDR_PT_LOBIT;

	/* TODO: implement the page direactories mapping */

	return 0;
}

// PTE TRAVELLING HELPER
/*
addr_t* pteTravelling(struct mm_struct *mm, addr_t pgn){
    addr_t pgd=0;
    addr_t p4d=0;
    addr_t pud=0;
    addr_t pmd=0;
    addr_t	pt=0;
    addr_t *pgd_entry;
    addr_t *p4d_entry;
    addr_t *pud_entry;
    addr_t *pmd_entry;
    addr_t *pt_entry;
    get_pd_from_address(pgn, &pgd, &p4d,
                        &pud, &pmd, &pt);
    pgd_entry = (addr_t *)mm->pgd;
    if(pgd_entry[pgd]== 0){
      p4d_entry = malloc(sizeof(addr_t)*TABLE_SIZE_64);
      if(!p4d_entry){
          return 0;
      }
      memset(p4d_entry, 0, sizeof(addr_t)*TABLE_SIZE_64);
      pgd_entry[pgd] = (addr_t)p4d_entry;
    }
    p4d_entry = (addr_t *)pgd_entry[pgd];
    if(p4d_entry[p4d]== 0){
      pud_entry = malloc(sizeof(addr_t)*TABLE_SIZE_64);
      if(!pud_entry){
          return 0;
      }
      memset(pud_entry, 0, sizeof(addr_t)*TABLE_SIZE_64);
      p4d_entry[p4d] = (addr_t)pud_entry;
    }
    pud_entry = (addr_t *)p4d_entry[p4d];
    if(pud_entry[pud]== 0){
      pmd_entry = malloc(sizeof(addr_t)*TABLE_SIZE_64);
      if(!pmd_entry){
          return 0;
      }
      memset(pmd_entry, 0, sizeof(addr_t)*TABLE_SIZE_64);
      pud_entry[pud] = (addr_t)pmd_entry;
    }
    pmd_entry = (addr_t *)pud_entry[pud];
    if(pmd_entry[pmd]== 0){
      pt_entry = malloc(sizeof(addr_t)*TABLE_SIZE_64);
      if(!pt_entry){
          return 0;
      }
      memset(pt_entry, 0, sizeof(addr_t)*TABLE_SIZE_64);
      pmd_entry[pmd] = (addr_t)pt_entry;
    }
    pt_entry = (addr_t *)pmd_entry[pmd];
    return &pt_entry[pt];
  
}
*/

/*
 * PTE TRAVELLING HELPER
 * Chức năng: Đi từ địa chỉ ảo -> PGD -> P4D -> PUD -> PMD -> PT -> Trả về địa chỉ PTE
 * Kết hợp: Thống kê số lần truy cập và cấp phát bộ nhớ cho các bảng còn thiếu
 */
addr_t* pteTravelling(struct mm_struct *mm, addr_t pgn) {
    addr_t pgd = 0;
    addr_t p4d = 0;
    addr_t pud = 0;
    addr_t pmd = 0;
    addr_t pt = 0;

    addr_t *pgd_entry;
    addr_t *p4d_entry;
    addr_t *pud_entry;
    addr_t *pmd_entry;
    addr_t *pt_entry;

    // --- 1. THỐNG KÊ TRUY CẬP (ACCESS TIMES) ---
    // Mỗi lần gọi hàm này tương đương việc CPU phải "đi bộ" qua 5 cấp bảng trang
    // Tăng thống kê riêng của Process
    mm->access_counter += 5; 

    // Tăng thống kê toàn hệ thống (Cần Lock)
    pthread_mutex_lock(&g_stats_lock);
    g_total_accesses += 5;
    pthread_mutex_unlock(&g_stats_lock);

    // Lấy index tại các cấp
    get_pd_from_address(pgn, &pgd, &p4d, &pud, &pmd, &pt);

    // --- CẤP 1: PGD (Đã có sẵn trong mm->pgd) ---
    pgd_entry = (addr_t *)mm->pgd;

    // --- CẤP 2: P4D ---
    if (pgd_entry[pgd] == 0) {
        // Chưa có bảng P4D -> Cấp phát mới
        p4d_entry = malloc(sizeof(addr_t) * TABLE_SIZE_64);
        if (!p4d_entry) return NULL;
        memset(p4d_entry, 0, sizeof(addr_t) * TABLE_SIZE_64);
        pgd_entry[pgd] = (addr_t)p4d_entry;

        // Cập nhật thống kê Size
        mm->byte_counter += sizeof(addr_t) * TABLE_SIZE_64;
        
        pthread_mutex_lock(&g_stats_lock);
        g_total_storage_bytes += sizeof(addr_t) * TABLE_SIZE_64;
        pthread_mutex_unlock(&g_stats_lock);
    }
    p4d_entry = (addr_t *)pgd_entry[pgd];

    // --- CẤP 3: PUD ---
    if (p4d_entry[p4d] == 0) {
        pud_entry = malloc(sizeof(addr_t) * TABLE_SIZE_64);
        if (!pud_entry) return NULL;
        memset(pud_entry, 0, sizeof(addr_t) * TABLE_SIZE_64);
        p4d_entry[p4d] = (addr_t)pud_entry;

        // Cập nhật thống kê Size
        mm->byte_counter += sizeof(addr_t) * TABLE_SIZE_64;

        pthread_mutex_lock(&g_stats_lock);
        g_total_storage_bytes += sizeof(addr_t) * TABLE_SIZE_64;
        pthread_mutex_unlock(&g_stats_lock);
    }
    pud_entry = (addr_t *)p4d_entry[p4d];

    // --- CẤP 4: PMD ---
    if (pud_entry[pud] == 0) {
        pmd_entry = malloc(sizeof(addr_t) * TABLE_SIZE_64);
        if (!pmd_entry) return NULL;
        memset(pmd_entry, 0, sizeof(addr_t) * TABLE_SIZE_64);
        pud_entry[pud] = (addr_t)pmd_entry;

        // Cập nhật thống kê Size
        mm->byte_counter += sizeof(addr_t) * TABLE_SIZE_64;

        pthread_mutex_lock(&g_stats_lock);
        g_total_storage_bytes += sizeof(addr_t) * TABLE_SIZE_64;
        pthread_mutex_unlock(&g_stats_lock);
    }
    pmd_entry = (addr_t *)pud_entry[pud];

    // --- CẤP 5: PT (Page Table - Chứa PTE) ---
    if (pmd_entry[pmd] == 0) {
        pt_entry = malloc(sizeof(addr_t) * TABLE_SIZE_64);
        if (!pt_entry) return NULL;
        memset(pt_entry, 0, sizeof(addr_t) * TABLE_SIZE_64);
        pmd_entry[pmd] = (addr_t)pt_entry;

        // Cập nhật thống kê Size
        mm->byte_counter += sizeof(addr_t) * TABLE_SIZE_64;

        pthread_mutex_lock(&g_stats_lock);
        g_total_storage_bytes += sizeof(addr_t) * TABLE_SIZE_64;
        pthread_mutex_unlock(&g_stats_lock);
    }
    pt_entry = (addr_t *)pmd_entry[pmd];

    // Trả về địa chỉ của PTE cụ thể
    return &pt_entry[pt];
}


/*
 * get_pd_from_pagenum - Parse page number to 5 page directory level
 * @pgn   : pagenumer
 * @pgd   : page global directory
 * @p4d   : page level directory
 * @pud   : page upper directory
 * @pmd   : page middle directory
 * @pt    : page table 
 */
int get_pd_from_pagenum(addr_t pgn, addr_t* pgd, addr_t* p4d, addr_t* pud, addr_t* pmd, addr_t* pt)
{
	/* Shift the address to get page num and perform the mapping*/
	return get_pd_from_address(pgn << PAGING64_ADDR_PT_SHIFT,
                         pgd,p4d,pud,pmd,pt);
}


/*
 * pte_set_swap - Set PTE entry for swapped page
 * @pte    : target page table entry (PTE)
 * @swptyp : swap type
 * @swpoff : swap offset
 */
int pte_set_swap(struct pcb_t *caller, addr_t pgn, int swptyp, addr_t swpoff)
{
  addr_t *pte;
	
  // dummy pte alloc to avoid runtime error
 // pte = malloc(sizeof(addr_t));
#ifdef MM64	
  /* Get value from the system */
  /* TODO Perform multi-level page mapping */
  //... caller->mm->pgd
  //... caller->mm->pt
  pte = pteTravelling(caller->mm, pgn << PAGING64_ADDR_PT_SHIFT);
#else
  pte = &caller->mm->pgd[pgn];
#endif
	
  SETBIT(*pte, PAGING_PTE_PRESENT_MASK);
  SETBIT(*pte, PAGING_PTE_SWAPPED_MASK);

  SETVAL(*pte, swptyp, PAGING_PTE_SWPTYP_MASK, PAGING_PTE_SWPTYP_LOBIT);
  SETVAL(*pte, swpoff, PAGING_PTE_SWPOFF_MASK, PAGING_PTE_SWPOFF_LOBIT);

  return 0;
}

/*
 * pte_set_fpn - Set PTE entry for on-line page
 * @pte   : target page table entry (PTE)
 * @fpn   : frame page number (FPN)
 */
int pte_set_fpn(struct pcb_t *caller, addr_t pgn, addr_t fpn) // Gán địa chỉ 1 frame vào 1 pte
{
  addr_t *pte;
	
  // dummy pte alloc to avoid runtime error
 // pte = malloc(sizeof(addr_t));
#ifdef MM64	
  /* Get value from the system */
  /* TODO Perform multi-level page mapping */
  addr_t address = pgn << PAGING64_ADDR_PT_SHIFT;
  pte = pteTravelling(caller->mm, address);
  //... caller->mm->pgd
  //... caller->mm->pt
#else
  pte = &caller->mm->pgd[pgn];
#endif

  SETBIT(*pte, PAGING_PTE_PRESENT_MASK);
  CLRBIT(*pte, PAGING_PTE_SWAPPED_MASK);

  SETVAL(*pte, fpn, PAGING_PTE_FPN_MASK, PAGING_PTE_FPN_LOBIT);

  return 0;
}


/* Get PTE page table entry
 * @caller : caller
 * @pgn    : page number
 * @ret    : page table entry
 **/
uint32_t pte_get_entry(struct pcb_t *caller, addr_t pgn)
{
  uint32_t pte = 0;
  #ifdef MM64 
	
  /* TODO Perform multi-level page mapping */
  addr_t address= pgn << PAGING64_ADDR_PT_SHIFT;
  //... caller->mm->pgd
  //... caller->mm->pt
  addr_t *pte_ptr = pteTravelling(caller->mm, address);
  if (pte_ptr) {
      pte = (uint32_t)(*pte_ptr);
  }
  #else
  return (uint32_t)caller->mm->pgd[pgn];
  #endif
  return pte;
}

/* Set PTE page table entry
 * @caller : caller
 * @pgn    : page number
 * @ret    : page table entry
 **/
int pte_set_entry(struct pcb_t *caller, addr_t pgn, uint32_t pte_val)
{
  addr_t *pte;
  
  #ifdef MM64  
  addr_t address= pgn << PAGING64_ADDR_PT_SHIFT;
  pte = pteTravelling(caller->mm, address);
            
  *pte=(addr_t)pte_val;

  #else
	caller->mm->pgd[pgn]=pte_val;
  #endif
	
	return 0;
}


/*
 * vmap_pgd_memset - map a range of page at aligned address
 */
int vmap_pgd_memset(struct pcb_t *caller,           // process call
                    addr_t addr,                       // start address which is aligned to pagesz
                    int pgnum)                      // num of mapping page
{ // Ghi pattern vào n trang tại địa chỉ lưu trong pte
  //int pgit = 0;
  uint64_t pattern = 0xdeadbeef;
  addr_t*pte;
  /* TODO memset the page table with given pattern
   */
  for(int i=0; i<pgnum; i++)
  {
    addr_t pgn = (addr >> PAGING64_ADDR_PT_SHIFT) + i;
    pte=pteTravelling(caller->mm, pgn << PAGING64_ADDR_PT_SHIFT) ;
    *pte = pattern;
  }
  return 0;
}

// HELPER FUNCTIONS
void MultiLevelHelper(struct mm_struct *mm, addr_t pgn){
    addr_t pgd=0;
    addr_t p4d=0;
    addr_t pud=0;
    addr_t pmd=0;
    addr_t	pt=0;
    get_pd_from_pagenum(pgn, &pgd, &p4d,
                        &pud, &pmd, &pt);

    if(mm->used_pgd[pgd]==0){
        mm->used_pgd[pgd] = 1;
        mm->byte_counter += sizeof(uint64_t);
    }
    if(mm->used_p4d[p4d]==0){
        mm->used_p4d[p4d] = 1;
        mm->byte_counter += sizeof(uint64_t);
    }
    if(mm->used_pud[pud]==0){
        mm->used_pud[pud] = 1;
        mm->byte_counter += sizeof(uint64_t);
    }
    if(mm->used_pmd[pmd]==0){
        mm->used_pmd[pmd] = 1;
        mm->byte_counter += sizeof(uint64_t);
    }
    if(mm->used_pt[pt]==0){
        mm->used_pt[pt] = 1;
        mm->byte_counter += sizeof(uint64_t);
    }
    mm->access_counter += 5;
    return;

}





/*
 * vmap_page_range - map a range of page at aligned address
 */
addr_t vmap_page_range(struct pcb_t *caller,           // process call
                    addr_t addr,                       // start address which is aligned to pagesz
                    int pgnum,                      // num of mapping page
                    struct framephy_struct *frames, // list of the mapped frames
                    struct vm_rg_struct *ret_rg)    // return mapped region, the real mapped fp   ** dùng để theo dõi vùng nhớ đã map
{                                                   // no guarantee all given pages are mapped
//  struct framephy_struct *fpit;
//  int pgit = 0;
//  addr_t pgn;

  /* TODO: update the rg_end and rg_start of ret_rg 
  //ret_rg->rg_end =  ....
  //ret_rg->rg_start = ...
  //ret_rg->vmaid = ...
  */

  /* TODO map range of frame to address space
   *      [addr to addr + pgnum*PAGING_PAGESZ
   *      in page table caller->mm->pgd,
   *                    caller->mm->pud...
   *                    ...
   */

  /* Tracking for later page replacement activities (if needed)
   * Enqueue new usage page */
  //enlist_pgn_node(&caller->mm->fifo_pgn, pgn64 + pgit);
  ret_rg->rg_start = addr;
  ret_rg->rg_end   = addr + pgnum * PAGING_PAGESZ;

  struct framephy_struct *fpit = frames;
  addr_t pgn = addr >> PAGING64_ADDR_PT_SHIFT;   // page number ảo
  struct mm_struct *mm = caller->mm;

  for (int i = 0; i < pgnum && fpit != NULL; i++) {
        // printf("DEBUG: vmap_page_range mapping pgn=%ld to fpn=%d\n", pgn, fpit->fpn);
        pte_set_fpn(caller, pgn, fpit->fpn);
  
  #ifdef MM64
        MultiLevelHelper(mm, pgn);
  #endif
        enlist_pgn_node(&mm->fifo_pgn, pgn);
        pgn++;
        fpit = fpit->fp_next;
  }
  return 0;
}

/*
 * alloc_pages_range - allocate req_pgnum of frame in ram
 * @caller    : caller
 * @req_pgnum : request page num
 * @frm_lst   : frame list
 */

addr_t alloc_pages_range(struct pcb_t *caller, int req_pgnum, struct framephy_struct **frm_lst)
{
  //addr_t fpn;
  //int pgit;
  //struct framephy_struct *newfp_str = NULL;

  /* TODO: allocate the page 
  //caller-> ...
  //frm_lst-> ...
  //
  for (pgit = 0; pgit < req_pgnum; pgit++)
  {
    // TODO: allocate the page 
    if (MEMPHY_get_freefp(caller->mram, &fpn) == 0)
    {
      newfp_str->fpn = fpn;
    }
    else
    { // TODO: ERROR CODE of obtaining somes but not enough frames
    }
  }
*/
  
  *frm_lst = NULL;
  for(int i=0;i<req_pgnum;i++)
  {
    struct framephy_struct *newfp_str = malloc(sizeof(struct framephy_struct));
    if (MEMPHY_get_freefp(caller->krnl->mram, &newfp_str->fpn) == 0)
    {
      newfp_str->fp_next = *frm_lst;
      *frm_lst = newfp_str;
    }
    else
    {
      while(*frm_lst != NULL){
          struct framephy_struct *temp = *frm_lst;
          *frm_lst = (*frm_lst)->fp_next;
          free(temp);
      }
      free(newfp_str);
      return -3000;
    }
  }
  /* End TODO */
  return 0;
}

/*
 * vm_map_ram - do the mapping all vm are to ram storage device
 * @caller    : caller
 * @astart    : vm area start
 * @aend      : vm area end
 * @mapstart  : start mapping point
 * @incpgnum  : number of mapped page
 * @ret_rg    : returned region
 */
addr_t vm_map_ram(struct pcb_t *caller, addr_t astart, addr_t aend, addr_t mapstart, int incpgnum, struct vm_rg_struct *ret_rg)
{
  if(astart >= aend || incpgnum <=0 || mapstart < astart ||
     (mapstart + incpgnum * PAGING_PAGESZ) > aend)
    return -1;
    
  struct framephy_struct *frm_lst = NULL;
  addr_t ret_alloc = 0;
  int pgnum = incpgnum;

  /*@bksysnet: author provides a feasible solution of getting frames
   *FATAL logic in here, wrong behaviour if we have not enough page
   *i.e. we request 1000 frames meanwhile our RAM has size of 3 frames
   *Don't try to perform that case in this simple work, it will result
   *in endless procedure of swap-off to get frame and we have not provide
   *duplicate control mechanism, keep it simple
   */
  ret_alloc = alloc_pages_range(caller, pgnum, &frm_lst);
  if (ret_alloc < 0 && ret_alloc != -3000)
    return -1;
  /* Out of memory */
  if (ret_alloc == -3000)
  {
    return -1;
  }
  /* it leaves the case of memory is enough but half in ram, half in swap
   * do the swaping all to swapper to get the all in ram */
   vmap_page_range(caller, mapstart, incpgnum, frm_lst, ret_rg);

  return 0;
}

/* Swap copy content page from source frame to destination frame
 * @mpsrc  : source memphy
 * @srcfpn : source physical page number (FPN)
 * @mpdst  : destination memphy
 * @dstfpn : destination physical page number (FPN)
 **/
int __swap_cp_page(struct memphy_struct *mpsrc, addr_t srcfpn,
                   struct memphy_struct *mpdst, addr_t dstfpn)
{
  int cellidx;
  addr_t addrsrc, addrdst;
  for (cellidx = 0; cellidx < PAGING_PAGESZ; cellidx++)
  {
    addrsrc = srcfpn * PAGING_PAGESZ + cellidx;
    addrdst = dstfpn * PAGING_PAGESZ + cellidx;

    BYTE data;
    MEMPHY_read(mpsrc, addrsrc, &data);
    MEMPHY_write(mpdst, addrdst, data);
  }

  return 0;
}

/*
 *Initialize a empty Memory Management instance
 * @mm:     self mm
 * @caller: mm owner
 */
int init_mm(struct mm_struct *mm, struct pcb_t *caller)
{
  struct vm_area_struct *vma0 = malloc(sizeof(struct vm_area_struct));
  if (!vma0)
      return -1;
  

  /* TODO init page table directory */
   mm->pgd = malloc(TABLE_SIZE_64* sizeof(uint64_t));
   mm->p4d = NULL;  
   mm->pud = NULL;
   mm->pmd = NULL;
   mm->pt = NULL;
   
   if(!mm->pgd)
   {
      free(vma0);
      return -1;
   }
   if (mm->pgd) {
       pthread_mutex_lock(&g_stats_lock);
       g_total_storage_bytes += TABLE_SIZE_64 * sizeof(uint64_t); // Tính size PGD
       pthread_mutex_unlock(&g_stats_lock);
   }
   memset(mm->pgd, 0, TABLE_SIZE_64 * sizeof(uint64_t));
   mm->mmap = vma0;  
   mm->fifo_pgn = NULL;


  /* By default the owner comes with at least one vma */
  vma0->vm_id = 0;
  vma0->vm_start = 0;
  vma0->vm_end = PAGING64_MAX_PGN * PAGING64_PAGESZ;  /* Set vm_end to max virtual address space */
  vma0->sbrk = vma0->vm_start;
  /* Do NOT initialize vm_freerg_list with the entire address space!
   * Free list should only contain regions that were previously allocated and then freed.
   * Initially, all allocation should go through inc_vma_limit to properly map physical pages.
   */
  vma0->vm_freerg_list = NULL;

  /* TODO update VMA0 next */
  vma0->vm_next = NULL;
  /* Point vma owner backward */
  vma0->vm_mm = mm; 

  /* TODO: update mmap */
  //mm->mmap = ...
  for(int i=0;i<PAGING_MAX_SYMTBL_SZ;i++)
  {
    mm->symrgtbl[i].rg_start = 0;
    mm->symrgtbl[i].rg_end = 0;
    mm->symrgtbl[i].rg_next = NULL;
  }
  memset(mm->used_pgd, 0, TABLE_SIZE_64*sizeof(uint8_t));
  memset(mm->used_p4d, 0, TABLE_SIZE_64*sizeof(uint8_t));
  memset(mm->used_pud, 0, TABLE_SIZE_64*sizeof(uint8_t));
  memset(mm->used_pmd, 0, TABLE_SIZE_64*sizeof(uint8_t));
  memset(mm->used_pt, 0, TABLE_SIZE_64*sizeof(uint8_t));

  mm->access_counter = 0;
  mm->byte_counter = 0;

  return 0;
}

struct vm_rg_struct *init_vm_rg(addr_t rg_start, addr_t rg_end)
{
  struct vm_rg_struct *rgnode = malloc(sizeof(struct vm_rg_struct));

  rgnode->rg_start = rg_start;
  rgnode->rg_end = rg_end;
  rgnode->rg_next = NULL;

  return rgnode;
}

int enlist_vm_rg_node(struct vm_rg_struct **rglist, struct vm_rg_struct *rgnode)
{
  rgnode->rg_next = *rglist;
  *rglist = rgnode;

  return 0;
}

int enlist_pgn_node(struct pgn_t **plist, addr_t pgn)
{
  struct pgn_t *pnode = malloc(sizeof(struct pgn_t));

  pnode->pgn = pgn;
  pnode->pg_next = *plist;
  *plist = pnode;

  return 0;
}

int print_list_fp(struct framephy_struct *ifp)
{
  struct framephy_struct *fp = ifp;

  printf("print_list_fp: ");
  if (fp == NULL) { printf("NULL list\n"); return -1;}
  printf("\n");
  while (fp != NULL)
  {
    printf("fp[" FORMAT_ADDR "]\n", fp->fpn);
    fp = fp->fp_next;
  }
  printf("\n");
  return 0;
}

int print_list_rg(struct vm_rg_struct *irg)
{
  struct vm_rg_struct *rg = irg;

  printf("print_list_rg: ");
  if (rg == NULL) { printf("NULL list\n"); return -1; }
  printf("\n");
  while (rg != NULL)
  {
    printf("rg[" FORMAT_ADDR "->"  FORMAT_ADDR "]\n", rg->rg_start, rg->rg_end);
    rg = rg->rg_next;
  }
  printf("\n");
  return 0;
}

int print_list_vma(struct vm_area_struct *ivma)
{
  struct vm_area_struct *vma = ivma;

  printf("print_list_vma: ");
  if (vma == NULL) { printf("NULL list\n"); return -1; }
  printf("\n");
  while (vma != NULL)
  {
    printf("va[" FORMAT_ADDR "->" FORMAT_ADDR "]\n", vma->vm_start, vma->vm_end);
    vma = vma->vm_next;
  }
  printf("\n");
  return 0;
}

int print_list_pgn(struct pgn_t *ip)
{
  printf("print_list_pgn: ");
  if (ip == NULL) { printf("NULL list\n"); return -1; }
  printf("\n");
  while (ip != NULL)
  {
    printf("va[" FORMAT_ADDR "]-\n", ip->pgn);
    ip = ip->pg_next;
  }
  printf("n");
  return 0;
}

// PRINTING HELPER
/*
void printHelper(struct mm_struct* mm , addr_t addr, short* printed)
{
  addr_t pgd_idx, p4d_idx, pud_idx, pmd_idx, pt_idx;
  addr_t *pgd_table, *p4d_table, *pud_table, *pmd_table, *pt_table;

  get_pd_from_address(addr, &pgd_idx, &p4d_idx, &pud_idx, &pmd_idx, &pt_idx);

  pgd_table = (addr_t *)mm->pgd;
  if(pgd_table == 0 || pgd_table[pgd_idx] == 0) {
      return;
  }
  p4d_table = (addr_t *)pgd_table[pgd_idx];

  if(p4d_table == 0 || p4d_table[p4d_idx] == 0) {
      return;
  }
  pud_table = (addr_t *)p4d_table[p4d_idx];

  if(pud_table == 0 || pud_table[pud_idx] == 0) {
      return;
  }
  pmd_table = (addr_t *)pud_table[pud_idx];

  if(pmd_table == 0 || pmd_table[pmd_idx] == 0) {
      return;
  }
  pt_table = (addr_t *)pmd_table[pmd_idx];

  if (printed[0] == (short)pgd_idx && printed[1] == (short)p4d_idx && 
      printed[2] == (short)pud_idx && printed[3] == (short)pmd_idx) {
      return;
  }
  printed[0] = pgd_idx;
  printed[1] = p4d_idx;
  printed[2] = pud_idx;
  printed[3] = pmd_idx;

  printf(" PDG=%016" PRIx64 " P4g=%016" PRIx64 " PUD=%016" PRIx64 " PMD=%016" PRIx64 "\n", 
         (uint64_t)pgd_table[pgd_idx], (uint64_t)p4d_table[p4d_idx], 
         (uint64_t)pud_table[pud_idx], (uint64_t)pmd_table[pmd_idx]);
  fflush(stdout);
}
*/

void printHelper(struct mm_struct* mm , addr_t addr, short* printed)
{
  addr_t pgd_idx, p4d_idx, pud_idx, pmd_idx, pt_idx;
  addr_t *pgd_table, *p4d_table, *pud_table, *pmd_table, *pt_table;

  get_pd_from_address(addr, &pgd_idx, &p4d_idx, &pud_idx, &pmd_idx, &pt_idx);

  pgd_table = (addr_t *)mm->pgd;
  if(pgd_table == 0 || pgd_table[pgd_idx] == 0) {
      return;
  }
  p4d_table = (addr_t *)pgd_table[pgd_idx];

  if(p4d_table == 0 || p4d_table[p4d_idx] == 0) {
      return;
  }
  pud_table = (addr_t *)p4d_table[p4d_idx];

  if(pud_table == 0 || pud_table[pud_idx] == 0) {
      return;
  }
  pmd_table = (addr_t *)pud_table[pud_idx];

  if(pmd_table == 0 || pmd_table[pmd_idx] == 0) {
      return;
  }
  pt_table = (addr_t *)pmd_table[pmd_idx];

  /* Check if we already printed this combination */
  /* THAY ĐỔI 1: Thêm kiểm tra pt_idx (cấp 5) để in chi tiết từng trang */
  if (printed[0] == (short)pgd_idx && printed[1] == (short)p4d_idx && 
      printed[2] == (short)pud_idx && printed[3] == (short)pmd_idx &&
      printed[4] == (short)pt_idx) { // <--- Thêm điều kiện này
      return;
  }
  printed[0] = pgd_idx;
  printed[1] = p4d_idx;
  printed[2] = pud_idx;
  printed[3] = pmd_idx;
  printed[4] = pt_idx; // <--- Cập nhật trạng thái in

  /* THAY ĐỔI 2: In thêm cột PTE */
  printf(" PDG=%016" PRIx64 " P4g=%016" PRIx64 " PUD=%016" PRIx64 " PMD=%016" PRIx64 " PTE=%016" PRIx64 "\n", 
         (uint64_t)pgd_table[pgd_idx], (uint64_t)p4d_table[p4d_idx], 
         (uint64_t)pud_table[pud_idx], (uint64_t)pmd_table[pmd_idx],
         (uint64_t)pt_table[pt_idx]); // <--- In giá trị thực sự của PTE (chứa FPN)
  fflush(stdout);
}



int print_pgtbl(struct pcb_t *caller, addr_t start, addr_t end)
{
  if (end == (addr_t)-1) {
      struct vm_area_struct *cur_vma = get_vma_by_num(caller->mm, 0);
      if (cur_vma) {
          end = cur_vma->sbrk;
      }
      else {
          end = 0;
      }
  }
//  addr_t pgn_start;//, pgn_end;
//  addr_t pgit;
//  struct krnl_t *krnl = caller->krnl;
  /* TODO traverse the page map and dump the page directory entries */
  struct mm_struct* mm= caller->mm;
  short printed[5]= {-1, -1, -1, -1 ,-1};
  
  /* Only print if we have valid range */
  if (start >= end) {
      return 0;
  }
  
  printf("print_pgtbl:\n");
  fflush(stdout);
  
  for(addr_t addr = start; addr < end; addr += PAGING64_PAGESZ)
  {
      printHelper(mm, addr, printed);
  }
  // THÊM ĐOẠN NÀY ĐỂ IN THỐNG KÊ
  printf("============================================================\n");
  printf("MEMORY ACCESS STATISTICS (PID %d):\n", caller->pid);
  printf("  1. Memory Access Times (Page Walking): %" PRIu64 " accesses\n", mm->access_counter);
  // (Tùy chọn) Quy đổi ra số byte nếu muốn:
  printf("  2. Page Table Storage Size: %" PRIu64 " bytes (approx %" PRIu64 " KB)\n", 
         mm->byte_counter, mm->byte_counter / 1024);
  printf("============================================================\n");
  return 0;
}

#endif  //def MM64
