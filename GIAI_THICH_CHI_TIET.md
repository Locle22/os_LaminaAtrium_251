# GIẢI THÍCH CHI TIẾT CHƯƠNG TRÌNH OS SIMULATION

## MỤC LỤC
1. [Tổng quan kiến trúc](#1-tổng-quan-kiến-trúc)
2. [Các thành phần chính](#2-các-thành-phần-chính)
3. [Luồng hoạt động](#3-luồng-hoạt-động)
4. [Giải thích output chi tiết](#4-giải-thích-output-chi-tiết)
5. [Cơ chế đồng bộ hóa](#5-cơ-chế-đồng-bộ-hóa)
6. [Quản lý bộ nhớ 64-bit](#6-quản-lý-bộ-nhớ-64-bit)

---

## 1. TỔNG QUAN KIẾN TRÚC

### 1.1 Mô hình hệ thống
```
┌─────────────────────────────────────────────────────────────┐
│                         TIMER                                │
│              (Điều phối thời gian - Time Slots)              │
└─────────────────────┬───────────────────────────────────────┘
                      │ pthread_cond_signal()
          ┌───────────┴───────────┬───────────────────┐
          ▼                       ▼                   ▼
┌─────────────────┐    ┌─────────────────┐    ┌─────────────────┐
│     LOADER      │    │      CPU 0      │    │      CPU N      │
│  (Nạp process)  │    │  (Thực thi)     │    │   (Thực thi)    │
└────────┬────────┘    └────────┬────────┘    └────────┬────────┘
         │                      │                      │
         └──────────────────────┼──────────────────────┘
                                ▼
                    ┌─────────────────────┐
                    │     RUN QUEUE       │
                    │  (Hàng đợi ready)   │
                    └─────────────────────┘
```

### 1.2 File cấu hình (vd: os_1_mlq_paging_small_4K)
```
2 4 8                    # time_slot=2, num_cpus=4, num_processes=8
4096                     # RAM size = 4KB
16777216 0 0 0           # SWAP sizes (16MB, 0, 0, 0)
input/proc/p0s 1 130     # Process 1: file=p0s, start_time=1, priority=130
input/proc/s3 2 39       # Process 2: file=s3, start_time=2, priority=39
input/proc/m1s 4 15      # Process 3: file=m1s, start_time=4, priority=15
...
```

---

## 2. CÁC THÀNH PHẦN CHÍNH

### 2.1 Timer Thread (src/timer.c)
**Chức năng:** Điều phối thời gian, đồng bộ tất cả các thread.

```c
static void * timer_routine(void * args) {
    // In Time slot 0 trước khi vào vòng lặp
    printf("Time slot %3lu\n", current_time());
    
    while (!timer_stop) {
        // 1. Chờ tất cả devices (CPU, Loader) báo cáo xong việc
        for (temp = dev_list; ...) {
            pthread_cond_wait(&temp->id.event_cond, ...);
        }
        
        // 2. Kiểm tra kết thúc TRƯỚC khi in Time slot mới
        if (fsh == event) break;
        
        // 3. Tăng thời gian
        _time++;
        
        // 4. In Time slot MỚI (trước khi đánh thức ai)
        printf("Time slot %3lu\n", current_time());
        
        // 5. Đánh thức tất cả devices
        for (temp = dev_list; ...) {
            pthread_cond_signal(&temp->id.timer_cond);
        }
    }
}
```

**Cơ chế đồng bộ:**
- Mỗi CPU/Loader có một `timer_id` riêng
- `next_slot()`: Thread báo cáo xong việc và chờ slot tiếp theo
- Timer chờ TẤT CẢ devices xong rồi mới chuyển slot

### 2.2 Loader Thread (src/os.c - ld_routine)
**Chức năng:** Nạp các process vào hệ thống theo đúng thời điểm.

```c
static void * ld_routine(void * args) {
    while (i < num_processes) {
        // Chờ đến đúng time slot
        while (current_time() < start_time[i]) {
            // Signal cho CPUs biết không có gì load slot này
            loader_last_signal_slot = current_time();
            pthread_cond_broadcast(&loader_done_cond);
            next_slot(timer_id);
        }
        
        // Load process
        proc = load(path[i]);
        proc->mm = malloc(sizeof(struct mm_struct));
        init_mm(proc->mm, proc);
        
        printf("Loaded a process at %s, PID: %d PRIO: %ld\n", ...);
        add_proc(proc);  // Thêm vào run queue
        
        // Signal cho CPUs biết đã load xong
        loader_last_signal_slot = current_time();
        pthread_cond_broadcast(&loader_done_cond);
        
        next_slot(timer_id);
    }
    done = 1;  // Báo hiệu không còn process nào
}
```

### 2.3 CPU Thread (src/os.c - cpu_routine)
**Chức năng:** Thực thi các process, quản lý time quantum.

```c
static void * cpu_routine(void * args) {
    int time_left = 0;
    struct pcb_t * proc = NULL;
    
    while (1) {
        /* === PRE-CHECK: Kiểm tra ở ĐẦU mỗi slot === */
        if (proc != NULL) {
            if (proc->pc == proc->code->size) {
                // Process đã chạy hết code → FINISHED
                printf("CPU %d: Processed %d has finished\n", ...);
                free(proc);
                proc = NULL;
            } else if (time_left == 0) {
                // Hết time quantum → PUT back to queue
                printf("CPU %d: Put process %d to run queue\n", ...);
                put_proc(proc);
                proc = NULL;
            }
        }
        
        /* Lấy process mới nếu cần */
        if (proc == NULL) {
            proc = get_proc();  // Lấy từ run queue
            if (proc == NULL) {
                if (done) break;  // Không còn process → stop
                next_slot(timer_id);
                continue;
            }
        }
        
        /* DISPATCH nếu bắt đầu quantum mới */
        if (time_left == 0) {
            printf("CPU %d: Dispatched process %d\n", ...);
            time_left = time_slot;  // Reset quantum
        }
        
        /* Chờ Loader hoàn thành slot này */
        pthread_mutex_lock(&loader_lock);
        while (loader_last_signal_slot < current_time() && !done) {
            pthread_cond_wait(&loader_done_cond, &loader_lock);
        }
        pthread_mutex_unlock(&loader_lock);
        
        /* CHẠY process - thực thi 1 instruction */
        run(proc);
        time_left--;
        
        next_slot(timer_id);
    }
    printf("CPU %d stopped\n", id);
}
```

### 2.4 Scheduler (src/sched.c)
**Chức năng:** Quản lý Multi-Level Queue (MLQ) scheduling.

```c
// Cấu trúc hàng đợi nhiều mức ưu tiên
static struct queue_t mlq_ready_queue[MAX_PRIO];

// Thêm process vào queue theo priority
void add_proc(struct pcb_t * proc) {
    enqueue(&mlq_ready_queue[proc->prio], proc);
}

// Lấy process có priority cao nhất (số nhỏ = cao hơn)
struct pcb_t * get_proc(void) {
    for (int i = 0; i < MAX_PRIO; i++) {
        if (!empty(&mlq_ready_queue[i])) {
            return dequeue(&mlq_ready_queue[i]);
        }
    }
    return NULL;
}
```

**Giải thích Priority:**
- Priority càng NHỎ → ưu tiên càng CAO
- Priority 0 = cao nhất, Priority 139 = thấp nhất
- Process priority 15 sẽ chạy trước priority 130

---

## 3. LUỒNG HOẠT ĐỘNG

### 3.1 Khởi động hệ thống
```
1. main() đọc config file
2. Khởi tạo RAM, SWAP memory
3. Tạo các thread: 1 Timer, 1 Loader, N CPUs
4. Timer bắt đầu chạy → in "Time slot 0"
5. Loader chờ đến start_time của process đầu tiên
6. CPUs chờ có process trong run queue
```

### 3.2 Một Time Slot điển hình
```
┌─────────────────────────────────────────────────────────────┐
│                      TIME SLOT N                             │
├─────────────────────────────────────────────────────────────┤
│ 1. Timer in "Time slot N"                                    │
│ 2. Timer đánh thức tất cả devices                           │
│                                                              │
│ 3. Loader kiểm tra: có process nào start_time == N?         │
│    - Nếu có: load() → printf("Loaded...") → add_proc()      │
│    - Signal: loader_last_signal_slot = N                     │
│                                                              │
│ 4. CPUs (song song):                                         │
│    a. Pre-check: Finished? Put?                             │
│    b. Get process nếu cần                                   │
│    c. Dispatch nếu quantum mới                              │
│    d. Chờ Loader signal xong                                │
│    e. run(proc) → thực thi 1 instruction                    │
│    f. time_left--                                           │
│                                                              │
│ 5. Tất cả gọi next_slot() → báo cáo xong việc              │
│ 6. Timer chờ đủ → tăng _time → chuyển slot N+1             │
└─────────────────────────────────────────────────────────────┘
```

### 3.3 Vòng đời của một Process
```
┌──────────┐    load()     ┌──────────┐   get_proc()   ┌──────────┐
│  DISK    │ ────────────► │  READY   │ ─────────────► │ RUNNING  │
│ (file)   │               │ (queue)  │                │  (CPU)   │
└──────────┘               └──────────┘                └────┬─────┘
                                 ▲                          │
                                 │ put_proc()               │
                                 │ (hết quantum)            │
                                 └──────────────────────────┘
                                                            │
                                                            │ pc == code->size
                                                            ▼
                                                     ┌──────────┐
                                                     │ FINISHED │
                                                     └──────────┘
```

---

## 4. GIẢI THÍCH OUTPUT CHI TIẾT

### 4.1 Ví dụ: os_1_mlq_paging_small_4K

**Config:**
- `time_slot = 2` (quantum = 2 slots)
- `num_cpus = 4` (CPU 0, 1, 2, 3)
- `num_processes = 8`

**Output phân tích:**

```
Time slot   0
ld_routine                 ← Loader bắt đầu chạy
```
- Slot 0: Chỉ khởi động, chưa có process nào (start_time nhỏ nhất = 1)

```
Time slot   1
    Loaded a process at input/proc/p0s, PID: 1 PRIO: 130
```
- Process 1 có start_time=1 → được load vào slot 1
- Priority 130 (khá thấp)

```
Time slot   2
    CPU 3: Dispatched process  1
    Loaded a process at input/proc/s3, PID: 2 PRIO: 39
```
- CPU 3 lấy process 1 từ queue → Dispatch
- Process 2 (priority 39) được load

```
Time slot   3
    CPU 1: Dispatched process  2
liballoc:261
print_pgtbl:
 PDG=0000718924000bb0 ...
```
- CPU 1 lấy process 2 (priority 39 < 130, nên ưu tiên hơn process 1)
- Process 1 đang chạy trên CPU 3, gọi `liballoc()` (cấp phát bộ nhớ)
- `print_pgtbl` in ra địa chỉ Page Directory

```
Time slot   4
    CPU 3: Put process  1 to run queue
    CPU 3: Dispatched process  1
liballoc:261
...
    Loaded a process at input/proc/m1s, PID: 3 PRIO: 15
```
- Process 1 hết quantum (2 slots: slot 2, 3) → Put back
- CPU 3 lại lấy process 1 (vì không có process nào khác sẵn sàng có priority cao hơn 130 trong queue lúc đó)
- Wait... tại sao lại Dispatch lại process 1 ngay?
  - Vì process 2 (prio 39) đang chạy trên CPU 1
  - Run queue chỉ còn process 1
  - CPU 3 lấy process 1 → Dispatch lại

```
Time slot   5
    CPU 2: Dispatched process  3
libfree:291
...
liballoc:261
...
    CPU 1: Put process  2 to run queue
    CPU 1: Dispatched process  2
```
- Process 3 (priority 15) được dispatch lên CPU 2
- Process 1 gọi `libfree()` (giải phóng bộ nhớ)
- Process 3 gọi `liballoc()` 
- Process 2 hết quantum → Put back → Dispatch lại

### 4.2 Giải thích các operations

| Operation | Ý nghĩa |
|-----------|---------|
| `Loaded a process` | Loader nạp process mới vào hệ thống |
| `Dispatched process` | CPU bắt đầu chạy process (cấp CPU) |
| `Put process to run queue` | Process hết quantum, trả về hàng đợi |
| `Processed X has finished` | Process chạy xong tất cả instructions |
| `CPU X stopped` | Không còn process nào, CPU dừng |
| `liballoc:XXX` | Gọi hàm cấp phát bộ nhớ (line XXX) |
| `libfree:XXX` | Gọi hàm giải phóng bộ nhớ |
| `libread:XXX` | Gọi hàm đọc bộ nhớ |
| `libwrite:XXX` | Gọi hàm ghi bộ nhớ |
| `print_pgtbl:` | In page table (5-level paging) |

### 4.3 Giải thích Page Table Output
```
PDG=0000718924000bb0 P4g=0000718924001bc0 PUD=0000718924002bd0 PMD=0000718924003be0
```

Đây là 5-level page table trong x86-64:
```
Virtual Address (48-bit)
┌─────┬─────┬─────┬─────┬─────┬──────────┐
│ PGD │ P4D │ PUD │ PMD │ PT  │  Offset  │
│ 9b  │ 9b  │ 9b  │ 9b  │ 9b  │   12b    │
└──┬──┴──┬──┴──┬──┴──┬──┴──┬──┴────┬─────┘
   │     │     │     │     │       │
   ▼     ▼     ▼     ▼     ▼       ▼
┌─────┐ ┌─────┐ ┌─────┐ ┌─────┐ ┌─────┐ ┌─────────┐
│ PGD │→│ P4D │→│ PUD │→│ PMD │→│ PT  │→│ Physical│
│Table│ │Table│ │Table│ │Table│ │Table│ │  Frame  │
└─────┘ └─────┘ └─────┘ └─────┘ └─────┘ └─────────┘
```

- **PGD** (Page Global Directory): Cấp cao nhất
- **P4D** (Page 4th Directory): Cấp 4
- **PUD** (Page Upper Directory): Cấp 3
- **PMD** (Page Middle Directory): Cấp 2
- **PT** (Page Table): Cấp cuối, trỏ đến physical frame

---

## 5. CƠ CHẾ ĐỒNG BỘ HÓA

### 5.1 Timer-Device Synchronization
```c
// Device (CPU/Loader) báo cáo xong việc
void next_slot(struct timer_id_t * timer_id) {
    // 1. Báo cho Timer biết đã xong
    pthread_mutex_lock(&timer_id->event_lock);
    timer_id->done = 1;
    pthread_cond_signal(&timer_id->event_cond);
    pthread_mutex_unlock(&timer_id->event_lock);
    
    // 2. Chờ Timer cho phép tiếp tục
    pthread_mutex_lock(&timer_id->timer_lock);
    while (timer_id->done) {
        pthread_cond_wait(&timer_id->timer_cond, &timer_id->timer_lock);
    }
    pthread_mutex_unlock(&timer_id->timer_lock);
}
```

### 5.2 Loader-CPU Synchronization
```c
// Loader signal sau khi load xong
pthread_mutex_lock(&loader_lock);
loader_last_signal_slot = current_time();
pthread_cond_broadcast(&loader_done_cond);
pthread_mutex_unlock(&loader_lock);

// CPU chờ Loader
pthread_mutex_lock(&loader_lock);
while (loader_last_signal_slot < current_time() && !done) {
    pthread_cond_wait(&loader_done_cond, &loader_lock);
}
pthread_mutex_unlock(&loader_lock);
```

**Tại sao cần đồng bộ Loader-CPU?**
- Đảm bảo "Loaded a process" in TRƯỚC "liballoc"
- Nếu không: CPU chạy `run()` → gọi `liballoc()` → in ra trước khi Loader kịp in

### 5.3 Race Condition đã fix
```
❌ Trước khi fix:
Time slot   2
liballoc:261           ← CPU chạy trước
Loaded a process...    ← Loader in sau

✅ Sau khi fix:
Time slot   2
Loaded a process...    ← Loader in trước
liballoc:261           ← CPU chạy sau
```

---

## 6. QUẢN LÝ BỘ NHỚ 64-BIT

### 6.1 Cấu trúc mm_struct (mỗi process có riêng)
```c
struct mm_struct {
    struct pgd_t *pgd;           // Page Global Directory
    
    // Tracking cho allocation
    addr_t pgd_start, p4d_start, pud_start, pmd_start, pt_start;
    
    // Virtual Memory Areas
    struct vm_area_struct *mmap;
    
    // FIFO list cho page replacement
    struct pgn_t *fifo_pgn;
    
    // Mutex để thread-safe
    pthread_mutex_t mm_lock;
};
```

### 6.2 Quá trình cấp phát bộ nhớ (liballoc)
```
1. Process gọi alloc(size)
2. liballoc():
   a. Tìm VMA (Virtual Memory Area) có đủ không gian
   b. Nếu không có → tạo VMA mới
   c. Cập nhật sbrk (break pointer)
   d. Trả về virtual address

3. Khi process truy cập địa chỉ:
   a. pg_getpage(): Kiểm tra page có trong RAM?
   b. Nếu không → Page Fault:
      - Tìm frame trống trong RAM
      - Nếu không có → pg_getswp(): Swap out page cũ
      - Nạp page từ SWAP vào RAM
   c. Cập nhật page table
```

### 6.3 FIFO Page Replacement
```c
// Khi cần swap out một page
int find_victim_page(struct mm_struct *mm, int *retpgn) {
    // Lấy page đầu tiên trong FIFO list (page cũ nhất)
    struct pgn_t *victim = mm->fifo_pgn;
    *retpgn = victim->pgn;
    
    // Xóa khỏi FIFO list
    mm->fifo_pgn = victim->pg_next;
    free(victim);
    
    return 0;
}
```

---

## 7. TÓM TẮT LUỒNG THỰC THI

```
┌────────────────────────────────────────────────────────────────┐
│                    MAIN PROGRAM FLOW                            │
├────────────────────────────────────────────────────────────────┤
│                                                                 │
│  main() ─┬─► read_config()                                     │
│          ├─► init_mm() - Khởi tạo RAM, SWAP                    │
│          ├─► start_timer() - Tạo Timer thread                  │
│          ├─► pthread_create(ld_routine) - Tạo Loader          │
│          ├─► pthread_create(cpu_routine) x N - Tạo N CPUs     │
│          └─► pthread_join() - Chờ tất cả kết thúc             │
│                                                                 │
├────────────────────────────────────────────────────────────────┤
│                                                                 │
│  TIMER LOOP:                                                    │
│  ┌─────────────────────────────────────────────────────────┐   │
│  │ while (!timer_stop) {                                    │   │
│  │   1. Wait all devices done                               │   │
│  │   2. if (all finished) break                             │   │
│  │   3. _time++                                             │   │
│  │   4. printf("Time slot %d", _time)                       │   │
│  │   5. Signal all devices                                  │   │
│  │ }                                                        │   │
│  └─────────────────────────────────────────────────────────┘   │
│                                                                 │
├────────────────────────────────────────────────────────────────┤
│                                                                 │
│  LOADER LOOP:                                                   │
│  ┌─────────────────────────────────────────────────────────┐   │
│  │ for each process in config {                             │   │
│  │   1. Wait until current_time >= start_time               │   │
│  │   2. load(process_file)                                  │   │
│  │   3. init_mm(proc->mm)                                   │   │
│  │   4. printf("Loaded...")                                 │   │
│  │   5. add_proc(proc)                                      │   │
│  │   6. Signal CPUs                                         │   │
│  │   7. next_slot()                                         │   │
│  │ }                                                        │   │
│  │ done = 1                                                 │   │
│  └─────────────────────────────────────────────────────────┘   │
│                                                                 │
├────────────────────────────────────────────────────────────────┤
│                                                                 │
│  CPU LOOP (mỗi CPU chạy song song):                            │
│  ┌─────────────────────────────────────────────────────────┐   │
│  │ while (1) {                                              │   │
│  │   /* PRE-CHECK */                                        │   │
│  │   if (proc finished) { printf("Finished"); free(proc) } │   │
│  │   if (time_left == 0) { printf("Put"); put_proc(proc) } │   │
│  │                                                          │   │
│  │   /* GET PROCESS */                                      │   │
│  │   if (proc == NULL) proc = get_proc()                    │   │
│  │   if (proc == NULL && done) break                        │   │
│  │                                                          │   │
│  │   /* DISPATCH */                                         │   │
│  │   if (time_left == 0) {                                  │   │
│  │     printf("Dispatched")                                 │   │
│  │     time_left = time_slot                                │   │
│  │   }                                                      │   │
│  │                                                          │   │
│  │   /* WAIT FOR LOADER */                                  │   │
│  │   wait(loader_done_cond)                                 │   │
│  │                                                          │   │
│  │   /* RUN */                                              │   │
│  │   run(proc)  // Execute 1 instruction                    │   │
│  │   time_left--                                            │   │
│  │                                                          │   │
│  │   next_slot()                                            │   │
│  │ }                                                        │   │
│  │ printf("CPU stopped")                                    │   │
│  └─────────────────────────────────────────────────────────┘   │
│                                                                 │
└────────────────────────────────────────────────────────────────┘
```

---

## 8. CÁC LỖI ĐÃ SỬA (15 BUGS)

| # | Lỗi | File | Mô tả |
|---|-----|------|-------|
| 1 | TABLE_SIZE_64 | mm64.h | Thiếu định nghĩa |
| 2 | caller->krnl->mm | nhiều file | Sai cách truy cập mm_struct |
| 3 | mm_struct fields | os-mm.h | Thiếu tracking fields |
| 4 | Return type | mm64.c | int → addr_t |
| 5 | Typo | libmem.c | PAGING64_PAGE_ALIGNSZ_64 |
| 6 | ld_routine race | os.c | Chờ đúng time slot |
| 7 | print_pgtbl format | mm64.c | Không in PDG=, P4g= |
| 8 | find_victim_page | libmem.c | FIFO list 1 page |
| 9 | Output format | mm64.c | Sai format |
| 10 | print_pgtbl header | mm64.c | In header trống |
| 11 | Per-process mm | common.h, os.c | Shared mm_struct |
| 12 | sbrk update | libmem.c | Không update sbrk |
| 13 | Pre/Post check | os.c | Sai timing Put/Dispatch |
| 14 | Timer race | timer.c | Time slot in sau CPU |
| 15 | Loader-CPU race | os.c | Loaded in sau liballoc |

---

## 9. KẾT LUẬN

Chương trình OS Simulation mô phỏng:
1. **Multi-processor system** (nhiều CPU)
2. **Time-sharing** (chia sẻ thời gian với quantum)
3. **Multi-Level Queue scheduling** (ưu tiên theo priority)
4. **5-level paging** (64-bit virtual memory)
5. **Page replacement** (FIFO algorithm)
6. **Thread synchronization** (mutex, condition variables)

Output cho thấy cách các process được nạp, dispatch, preempt, và kết thúc theo thời gian, cùng với các memory operations (alloc, free, read, write).
