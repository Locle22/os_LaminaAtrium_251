#include <stdint.h>
#include "common.h"     // Must be first - defines basic types
#include "syscall.h"    // Defines struct sc_regs
#include "queue.h"      // Defines struct queue_t
#include "sched.h"      // Defines scheduling functions

int __sys_getprocinfo(struct krnl_t *krnl, uint32_t pid, struct sc_regs* reg) {
   uint32_t target_pid = reg->a1; // PID to query (passed from user)
   struct pcb_t *proc = NULL;
   
   // Search for process in kernel queues using PID (NOT passing PCB directly)
   // This demonstrates proper user/kernel space separation
   
   // Search in ready_queue
   if (krnl && krnl->ready_queue != NULL) {
       for (int i = 0; i < krnl->ready_queue->size; i++) {
           if (krnl->ready_queue->proc[i] && 
               krnl->ready_queue->proc[i]->pid == target_pid) {
               proc = krnl->ready_queue->proc[i];
               break;
           }
       }
   }
   
   // Search in running_list if not found
   if (!proc && krnl && krnl->running_list != NULL) {
       for (int i = 0; i < krnl->running_list->size; i++) {
           if (krnl->running_list->proc[i] && 
               krnl->running_list->proc[i]->pid == target_pid) {
               proc = krnl->running_list->proc[i];
               break;
           }
       }
   }
   
#ifdef MLQ_SCHED
   // Search in MLQ ready queues if still not found
   // mlq_ready_queue is a pointer to an array of queues
   if (!proc && krnl && krnl->mlq_ready_queue != NULL) {
       for (int prio = 0; prio < MAX_PRIO; prio++) {
           struct queue_t *q = &krnl->mlq_ready_queue[prio];
           for (int i = 0; i < q->size; i++) {
               if (q->proc[i] && q->proc[i]->pid == target_pid) {
                   proc = q->proc[i];
                   break;
               }
           }
           if (proc) break;
       }
   }
#endif
   
   // Return process information through registers
   if (proc) {
       // Return priority information
#ifdef MLQ_SCHED
       reg->a1 = proc->prio; // Return on-fly priority for MLQ
#else
       reg->a1 = proc->priority; // Return default priority
#endif
   } else {
       // Process not found
       reg->a1 = (uint32_t)-1;
   }
   
   return 0;
}
