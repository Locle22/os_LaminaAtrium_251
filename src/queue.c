#include <stdio.h>
#include <stdlib.h>
#include "queue.h"

int empty(struct queue_t *q) // kiểm tra queue có trống không, có return 1 không thì 0
{
        if (q == NULL)
                return 1;
        return (q->size == 0);
}

void enqueue(struct queue_t *q, struct pcb_t *proc) // thêm 1 process vào queue
{
        /* TODO: put a new process to queue [q] */
        if (q == NULL || proc == NULL){
                return;
        }
        if (q->size >= MAX_QUEUE_SIZE){
                // hàng đợi đầy => lỗi / bỏ qua
                //fprinf (stderr, "enqueue : queue full \n");
                return;
        }
        q->proc[q->size] = proc;
        q->size++;
}

struct pcb_t *dequeue(struct queue_t *q) // Gỡ 1 proc ra khỏi queue
{
        /* TODO: return a pcb whose prioprity is the highest
         * in the queue [q] and remember to remove it from q
         * */
        if (q == NULL || q->size == 0){
                return NULL;
        }
        struct pcb_t *p = q->proc[0];
        // Dồn trái
        for (int i = 1; i < q->size; i++) {
                q->proc[i - 1] = q->proc[i];
        }
        q->size--;
        return p;
}
struct pcb_t *purgequeue(struct queue_t *q, struct pcb_t *proc) // Gỡ bỏ 1 process mình muốn ra khỏi queue
{
        /* TODO: remove a specific item from queue
         * */
        if (q == NULL || proc == NULL || q->size == 0){
                return NULL;
        }
        int idx = -1;
        for (int i = 0; i < q->size; i++) {
                if (q->proc[i] == proc) {
                        idx = i;
                        break;
                }
        }
        if (idx < 0){
                return NULL;

        }
        struct pcb_t *p = q->proc[idx];
        for (int i = idx + 1; i < q->size; i++) {
                q->proc[i - 1] = q->proc[i];
        }
        q->size--;
        return p;
}