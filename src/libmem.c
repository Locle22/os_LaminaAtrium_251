/*
 * Copyright (C) 2026 pdnguyen of HCMC University of Technology VNU-HCM
 */

/* LamiaAtrium release
 * Source Code License Grant: The authors hereby grant to Licensee
 * personal permission to use and modify the Licensed Source Code
 * for the sole purpose of studying while attending the course CO2018.
 */

// #ifdef MM_PAGING
/*
 * System Library
 * Memory Module Library libmem.c 
 */

#include "string.h"
#include "mm.h"
#include "mm64.h"
#include "syscall.h"
#include "libmem.h"
#include <stdlib.h>
#include <stdio.h>
#include <pthread.h>

static pthread_mutex_t mmvm_lock = PTHREAD_MUTEX_INITIALIZER;

/*enlist_vm_freerg_list - add new rg to freerg_list
 *@mm: memory region
 *@rg_elmt: new region
 *
 */

/* Helper function: Coalesce adjacent free regions */
static void coalesce_freerg_list(struct vm_rg_struct **freerg_list) {
    struct vm_rg_struct *curr = *freerg_list;
    
    while (curr != NULL && curr->rg_next != NULL) {
        /* If current region's end is adjacent to next region's start */
        if (curr->rg_end == curr->rg_next->rg_start) {
            /* Merge the two regions */
            curr->rg_end = curr->rg_next->rg_end;
            struct vm_rg_struct *to_free = curr->rg_next;
            curr->rg_next = curr->rg_next->rg_next;
            free(to_free);
            /* Don't move to next yet, check if further merging is possible */
        } else {
            curr = curr->rg_next;
        }
    }
}

/* Helper function: Insert region into free list in sorted order */
static int insert_sorted_freerg(struct vm_rg_struct **list, struct vm_rg_struct *newrg) {
    struct vm_rg_struct *curr = *list;
    struct vm_rg_struct *prev = NULL;
    
    /* Find correct position (sorted by rg_start) */
    while (curr != NULL && curr->rg_start < newrg->rg_start) {
        prev = curr;
        curr = curr->rg_next;
    }
    
    /* Insert at the found position */
    if (prev == NULL) {
        /* Insert at head */
        newrg->rg_next = *list;
        *list = newrg;
    } else {
        newrg->rg_next = prev->rg_next;
        prev->rg_next = newrg;
    }
    
    return 0;
}

int enlist_vm_freerg_list(struct mm_struct *mm, struct vm_rg_struct *rg_elmt)
{

  if (rg_elmt->rg_start >= rg_elmt->rg_end){
    free(rg_elmt);
    return -1;
  }
    

  // /* Enlist the new region */
  // mm->mmap->vm_freerg_list = rg_elmt;

    /* Insert in sorted order */
    insert_sorted_freerg(&mm->mmap->vm_freerg_list, rg_elmt);
    
    /* Coalesce adjacent regions */
    coalesce_freerg_list(&mm->mmap->vm_freerg_list);

  return 0;
}

/*get_symrg_byid - get mem region by region ID
 *@mm: memory region
 *@rgid: region ID act as symbol index of variable
 *
 */
struct vm_rg_struct *get_symrg_byid(struct mm_struct *mm, int rgid)
{
  if (rgid < 0 || rgid >= PAGING_MAX_SYMTBL_SZ)
    return NULL;

  return &mm->symrgtbl[rgid];
}

/*__alloc - allocate a region memory
 *@caller: caller
 *@vmaid: ID vm area to alloc memory region
 *@rgid: memory region ID (used to identify variable in symbole table)
 *@size: allocated size
 *@alloc_addr: address of allocated memory region
 *
 */
int __alloc(struct pcb_t *caller, int vmaid, int rgid, addr_t size, addr_t *alloc_addr)
{
  /*Allocate at the toproof */
  pthread_mutex_lock(&mmvm_lock);
  struct vm_rg_struct rgnode;
  struct vm_area_struct *cur_vma = get_vma_by_num(caller->mm, vmaid);
  if (cur_vma == NULL) {
      pthread_mutex_unlock(&mmvm_lock);
        return -1;
  }

  if (get_free_vmrg_area(caller, vmaid, size, &rgnode) == 0)
  {
    caller->mm->symrgtbl[rgid].rg_start = rgnode.rg_start;
    caller->mm->symrgtbl[rgid].rg_end = rgnode.rg_start + size;
    caller->mm->symrgtbl[rgid].rg_next = NULL;
 
    *alloc_addr = rgnode.rg_start;

    pthread_mutex_unlock(&mmvm_lock);
    return 0;
  }

  /* TODO get_free_vmrg_area FAILED handle the region management (Fig.6)*/
  addr_t inc_sz;
  /*Attempt to increate limit to get space */
#ifdef MM64
  inc_sz = PAGING64_PAGE_ALIGNSZ(size);
#else
  inc_sz = PAGING_PAGE_ALIGNSZ(size);
#endif
  addr_t old_sbrk = cur_vma->sbrk;
  if (old_sbrk + size > cur_vma->vm_end) {
      pthread_mutex_unlock(&mmvm_lock);
      return -1; /* Not enough space in heap */
  }

  /* TODO INCREASE THE LIMIT
   * SYSCALL 1 sys_memmap
   */
  struct sc_regs regs;
  regs.a1 = SYSMEM_INC_OP;
  regs.a2 = vmaid;
  regs.a3 = inc_sz;
  
// #ifdef MM64
//   regs.a3 = size;
// #else
//   regs.a3 = PAGING_PAGE_ALIGNSZ(size);
// #endif  
  int ret = syscall(caller->krnl, caller->pid, 17, &regs); /* SYSCALL 17 sys_memmap */
  if (ret < 0){
      pthread_mutex_unlock(&mmvm_lock);
      return -1;
  }
  /*Successful increase limit */
  caller->mm->symrgtbl[rgid].rg_start = old_sbrk;
  caller->mm->symrgtbl[rgid].rg_end = old_sbrk + size;
  caller->mm->symrgtbl[rgid].rg_next = NULL;

  /* Update sbrk to point to end of allocated region (page-aligned) */
  cur_vma->sbrk = old_sbrk + inc_sz;

  *alloc_addr = old_sbrk;

    /* If we allocated less than the page-aligned size, add remainder to free list */
    if (size < inc_sz) {
        struct vm_rg_struct *remainder = malloc(sizeof(struct vm_rg_struct));
        if (remainder) {
            remainder->rg_start = old_sbrk + size;
            remainder->rg_end = old_sbrk + inc_sz;
            remainder->rg_next = NULL;
            enlist_vm_freerg_list(caller->mm, remainder);
        }
    }

  pthread_mutex_unlock(&mmvm_lock);
  return 0;

}

/*__free - remove a region memory
 *@caller: caller
 *@vmaid: ID vm area to alloc memory region
 *@rgid: memory region ID (used to identify variable in symbole table)
 *@size: allocated size
 *
 */
int __free(struct pcb_t *caller, int vmaid, int rgid)
{
  pthread_mutex_lock(&mmvm_lock);

  if (rgid < 0 || rgid > PAGING_MAX_SYMTBL_SZ)
  {
    pthread_mutex_unlock(&mmvm_lock);
    return -1;
  }

  /* TODO: Manage the collect freed region to freerg_list */
  struct vm_rg_struct *rgnode = get_symrg_byid(caller->mm, rgid);

  if (rgnode->rg_start == 0 && rgnode->rg_end == 0)
  {
    pthread_mutex_unlock(&mmvm_lock);
    return -1;
  }
  struct vm_rg_struct *freerg_node = malloc(sizeof(struct vm_rg_struct));
  if (freerg_node == NULL)
  {
    pthread_mutex_unlock(&mmvm_lock);
    return -1;
  }
  freerg_node->rg_start = rgnode->rg_start;
  freerg_node->rg_end = rgnode->rg_end;
  freerg_node->rg_next = NULL;

  rgnode->rg_start = rgnode->rg_end = 0;
  rgnode->rg_next = NULL;

  /*enlist the obsoleted memory region */
  enlist_vm_freerg_list(caller->mm, freerg_node);

  pthread_mutex_unlock(&mmvm_lock);
  return 0;
}

/*liballoc - PAGING-based allocate a region memory
 *@proc:  Process executing the instruction
 *@size: allocated size
 *@reg_index: memory region ID (used to identify variable in symbole table)
 */
int liballoc(struct pcb_t *proc, addr_t size, uint32_t reg_index)
{
  addr_t  addr;

  int val = __alloc(proc, 0, reg_index, size, &addr);
  if (val == -1)
  {
    return -1;
  }
#ifdef IODUMP
  /* TODO dump IO content (if needed) */
  printf("%s:%d\n", __func__, __LINE__);
  fflush(stdout);
#ifdef PAGETBL_DUMP
  print_pgtbl(proc, 0, -1); // print max TBL
#endif
#endif

  /* By default using vmaid = 0 */
  return val;
}

/*libfree - PAGING-based free a region memory
 *@proc: Process executing the instruction
 *@size: allocated size
 *@reg_index: memory region ID (used to identify variable in symbole table)
 */

int libfree(struct pcb_t *proc, uint32_t reg_index)
{
  if (reg_index >= 10){
    return -1;
  }
  int val = __free(proc, 0, reg_index);
  if (val == -1)
  {
    return -1;
  }
  proc->regs[reg_index] = 0;
#ifdef IODUMP
  /* TODO dump IO content (if needed) */
  printf("%s:%d\n", __func__, __LINE__);
  fflush(stdout);
#ifdef PAGETBL_DUMP
  print_pgtbl(proc, 0, -1); // print max TBL
#endif
#endif
  return 0;//val;
}

/*pg_getpage - get the page in ram
 *@mm: memory region
 *@pagenum: PGN
 *@framenum: return FPN
 *@caller: caller
 *
 */
int pg_getpage(struct mm_struct *mm, int pgn, int *fpn, struct pcb_t *caller)
{

  uint32_t pte = pte_get_entry(caller, pgn);

  if (!PAGING_PAGE_PRESENT(pte))
  { /* Page is not online, make it actively living */
    addr_t vicpgn, swpfpn;
//    addr_t vicfpn;
//    addr_t vicpte;
//  struct sc_regs regs;

    /* TODO Initialize the target frame storing our variable */
//  addr_t tgtfpn 

    /* TODO: Play with your paging theory here */
    /* Find victim page */
    if (find_victim_page(caller->mm, &vicpgn) == -1)
    {
      return -1;
    }

    /* Get free frame in MEMSWP */
    if (MEMPHY_get_freefp(caller->krnl->active_mswp, &swpfpn) == -1)
    {
      return -1;
    }

    /* TODO: Implement swap frame from MEMRAM to MEMSWP and vice versa*/

    /* TODO copy victim frame to swap 
     * SWP(vicfpn <--> swpfpn)
     * SYSCALL 1 sys_memmap
     */


    /* Update page table */
    //pte_set_swap(...);

    /* Update its online status of the target page */
    //pte_set_fpn(...);

    enlist_pgn_node(&caller->mm->fifo_pgn, pgn);
  }

  *fpn = PAGING_FPN(pte_get_entry(caller,pgn));

  return 0;
}

/*pg_getval - read value at given offset
 *@mm: memory region
 *@addr: virtual address to acess
 *@value: value
 *
 */
int pg_getval(struct mm_struct *mm, int addr, BYTE *data, struct pcb_t *caller)
{
  int pgn = PAGING_PGN(addr);
//  int off = PAGING_OFFST(addr);
  int fpn;

  if (pg_getpage(mm, pgn, &fpn, caller) != 0)
    return -1; /* invalid page access */

//  int phyaddr = (fpn << PAGING_ADDR_FPN_LOBIT) + off;

  /* TODO 
   *  MEMPHY_read(caller->krnl->mram, phyaddr, data);
   *  MEMPHY READ 
   *  SYSCALL 17 sys_memmap with SYSMEM_IO_READ
   */

  return 0;
}

/*pg_setval - write value to given offset
 *@mm: memory region
 *@addr: virtual address to acess
 *@value: value
 *
 */
int pg_setval(struct mm_struct *mm, int addr, BYTE value, struct pcb_t *caller)
{
  int pgn = PAGING_PGN(addr);
//  int off = PAGING_OFFST(addr);
  int fpn;

  /* Get the page to MEMRAM, swap from MEMSWAP if needed */
  if (pg_getpage(mm, pgn, &fpn, caller) != 0)
    return -1; /* invalid page access */


  /* TODO 
   *  MEMPHY_write(caller->krnl->mram, phyaddr, value);
   *  MEMPHY WRITE with SYSMEM_IO_WRITE 
   * SYSCALL 17 sys_memmap
   */

  return 0;
}

/*__read - read value in region memory
 *@caller: caller
 *@vmaid: ID vm area to alloc memory region
 *@offset: offset to acess in memory region
 *@rgid: memory region ID (used to identify variable in symbole table)
 *@size: allocated size
 *
 */
int __read(struct pcb_t *caller, int vmaid, int rgid, addr_t offset, BYTE *data)
{
  struct vm_rg_struct *currg = get_symrg_byid(caller->mm, rgid);

//  struct vm_area_struct *cur_vma = get_vma_by_num(caller->mm, vmaid);

  /* TODO Invalid memory identify */
  pthread_mutex_lock(&mmvm_lock);
  pg_getval(caller->mm, currg->rg_start + offset, data, caller);
  pthread_mutex_unlock(&mmvm_lock);

  return 0;
}

/*libread - PAGING-based read a region memory */
int libread(
    struct pcb_t *proc, // Process executing the instruction
    uint32_t source,    // Index of source register
    addr_t offset,    // Source address = [source] + [offset]
    uint32_t* destination)
{
  BYTE data;
  int val = __read(proc, 0, source, offset, &data);

  *destination = data;
#ifdef IODUMP
  /* TODO dump IO content (if needed) */
  printf("%s:%d\n", __func__, __LINE__);
  fflush(stdout);
#endif

  return val;
}

/*__write - write a region memory
 *@caller: caller
 *@vmaid: ID vm area to alloc memory region
 *@offset: offset to acess in memory region
 *@rgid: memory region ID (used to identify variable in symbole table)
 *@size: allocated size
 *
 */
int __write(struct pcb_t *caller, int vmaid, int rgid, addr_t offset, BYTE value)
{
  pthread_mutex_lock(&mmvm_lock);
  struct vm_rg_struct *currg = get_symrg_byid(caller->mm, rgid);

  struct vm_area_struct *cur_vma = get_vma_by_num(caller->mm, vmaid);

  if (currg == NULL || cur_vma == NULL) /* Invalid memory identify */
  {
    pthread_mutex_unlock(&mmvm_lock);
    return -1;
  }

  pg_setval(caller->mm, currg->rg_start + offset, value, caller);

  pthread_mutex_unlock(&mmvm_lock);
  return 0;
}

/*libwrite - PAGING-based write a region memory */
int libwrite(
    struct pcb_t *proc,   // Process executing the instruction
    BYTE data,            // Data to be wrttien into memory
    uint32_t destination, // Index of destination register
    addr_t offset)
{
  int val = __write(proc, 0, destination, offset, data);
  if (val == -1)
  {
    return -1;
  }
#ifdef IODUMP
  printf("%s:%d\n", __func__, __LINE__);
  fflush(stdout);
#ifdef PAGETBL_DUMP
  print_pgtbl(proc, 0, -1); // print max TBL
#endif
  MEMPHY_dump(proc->krnl->mram);
#endif

  return val;
}

/*free_pcb_memphy - collect all memphy of pcb
 *@caller: caller
 *@vmaid: ID vm area to alloc memory region
 *@incpgnum: number of page
 */
int free_pcb_memph(struct pcb_t *caller)
{
  pthread_mutex_lock(&mmvm_lock);
  int pagenum, fpn;
  uint32_t pte;

  for (pagenum = 0; pagenum < PAGING_MAX_PGN; pagenum++)
  {
    pte = caller->mm->pgd[pagenum];

    if (PAGING_PAGE_PRESENT(pte))
    {
      fpn = PAGING_FPN(pte);
      MEMPHY_put_freefp(caller->krnl->mram, fpn);
    }
    else
    {
      fpn = PAGING_SWP(pte);
      MEMPHY_put_freefp(caller->krnl->active_mswp, fpn);
    }
  }

  pthread_mutex_unlock(&mmvm_lock);
  return 0;
}


/*find_victim_page - find victim page
 *@caller: caller
 *@pgn: return page number
 *
 */
int find_victim_page(struct mm_struct *mm, addr_t *retpgn)
{
  struct pgn_t *pg = mm->fifo_pgn;

  /* TODO: Implement the theorical mechanism to find the victim page */
  if (!pg)
  {
    return -1;
  }
  
  /* Special case: only one page in FIFO list */
  if (pg->pg_next == NULL)
  {
    *retpgn = pg->pgn;
    mm->fifo_pgn = NULL;
    free(pg);
    return 0;
  }
  
  /* Find the last page (oldest in FIFO) */
  struct pgn_t *prev = NULL;
  while (pg->pg_next)
  {
    prev = pg;
    pg = pg->pg_next;
  }
  *retpgn = pg->pgn;
  prev->pg_next = NULL;

  free(pg);

  return 0;
}

/*get_free_vmrg_area - get a free vm region
 *@caller: caller
 *@vmaid: ID vm area to alloc memory region
 *@size: allocated size
 *
 */
int get_free_vmrg_area(struct pcb_t *caller, int vmaid, int size, struct vm_rg_struct *newrg)
{
  struct vm_area_struct *cur_vma = get_vma_by_num(caller->mm, vmaid);

  struct vm_rg_struct *rgit = cur_vma->vm_freerg_list;

  if (rgit == NULL)
    return -1;

  /* Probe unintialized newrg */
  newrg->rg_start = newrg->rg_end = -1;

  /* Traverse on list of free vm region to find a fit space */
  while (rgit != NULL)
  {
    if (rgit->rg_start + size <= rgit->rg_end)
    { /* Current region has enough space */
      newrg->rg_start = rgit->rg_start;
      newrg->rg_end = rgit->rg_start + size;

      /* Update left space in chosen region */
      if (rgit->rg_start + size < rgit->rg_end)
      {
        rgit->rg_start = rgit->rg_start + size;
      }
      else
      { /*Use up all space, remove current node */
        /*Clone next rg node */
        struct vm_rg_struct *nextrg = rgit->rg_next;

        /*Cloning */
        if (nextrg != NULL)
        {
          rgit->rg_start = nextrg->rg_start;
          rgit->rg_end = nextrg->rg_end;

          rgit->rg_next = nextrg->rg_next;

          free(nextrg);
        }
        else
        {                                /*End of free list */
          rgit->rg_start = rgit->rg_end; // dummy, size 0 region
          rgit->rg_next = NULL;
        }
      }
      break;
    }
    else
    {
      rgit = rgit->rg_next; // Traverse next rg
    }
  }

  if (newrg->rg_start == -1) // new region not found
    return -1;

  return 0;
}

// #endif
