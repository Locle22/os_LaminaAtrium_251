# GIẢI THÍCH CHI TIẾT CÁC HÀM VÀ CƠ CHẾ ĐỒNG BỘ

## TỔNG QUAN

Chương trình có 3 loại thread chính:
- **1 Timer thread**: Điều phối thời gian
- **1 Loader thread**: Nạp processes
- **N CPU threads**: Thực thi processes

```
                    ┌─────────────┐
                    │    TIMER    │
                    │  (1 thread) │
                    └──────┬──────┘
                           │
           ┌───────────────┼───────────────┐
           │               │               │
           ▼               ▼               ▼
    ┌──────────┐    ┌──────────┐    ┌──────────┐
    │  LOADER  │    │   CPU 0  │    │   CPU N  │
    │(1 thread)│    │ (thread) │    │ (thread) │
    └──────────┘    └──────────┘    └──────────┘
```

---

## 1. CẤU TRÚC TIMER_ID_T

Mỗi device (CPU hoặc Loader) có một `timer_id`:

```c
struct timer_id_t {
    // === DEVICE → TIMER (báo cáo xong việc) ===
    pthread_mutex_t event_lock;    // Mutex bảo vệ biến done và fsh
    pthread_cond_t event_cond;     // Condition để signal Timer
    int done;                      // = 1 khi device xong việc slot này
    int fsh;                       // = 1 khi device kết thúc hoàn toàn
    
    // === TIMER → DEVICE (cho phép tiếp tục) ===
    pthread_mutex_t timer_lock;    // Mutex bảo vệ việc chờ
    pthread_cond_t timer_cond;     // Condition để Timer đánh thức device
};
```

**Tại sao cần 2 cặp lock/cond?**
- Cặp 1 (`event_*`): Device báo cho Timer biết "tôi xong rồi"
- Cặp 2 (`timer_*`): Timer báo cho Device biết "slot mới bắt đầu, chạy tiếp đi"

---

## 2. HÀM TIMER_ROUTINE

```c
static void * timer_routine(void * args) {
    // ① In Time slot 0 TRƯỚC khi vào vòng lặp
    printf("Time slot %3lu\n", current_time());
    fflush(stdout);
    
    while (!timer_stop) {
        int fsh = 0;   // Đếm số device đã finish hoàn toàn
        int event = 0; // Đếm tổng số device
        
        // ② CHỜ TẤT CẢ DEVICES XONG VIỆC
        for (temp = dev_list; temp != NULL; temp = temp->next) {
            pthread_mutex_lock(&temp->id.event_lock);
            
            // Chờ cho đến khi device này done HOẶC finish
            while (!temp->id.done && !temp->id.fsh) {
                pthread_cond_wait(&temp->id.event_cond, &temp->id.event_lock);
            }
            
            if (temp->id.fsh) fsh++;  // Device này kết thúc hoàn toàn
            event++;
            
            pthread_mutex_unlock(&temp->id.event_lock);
        }

        // ③ KIỂM TRA KẾT THÚC
        if (fsh == event) {
            break;  // Tất cả devices đã finish → thoát
        }

        // ④ TĂNG THỜI GIAN
        _time++;
        
        // ⑤ IN TIME SLOT MỚI (trước khi đánh thức ai)
        printf("Time slot %3lu\n", current_time());
        fflush(stdout);
        
        // ⑥ ĐÁNH THỨC TẤT CẢ DEVICES
        for (temp = dev_list; temp != NULL; temp = temp->next) {
            pthread_mutex_lock(&temp->id.timer_lock);
            temp->id.done = 0;  // Reset cờ done
            pthread_cond_signal(&temp->id.timer_cond);  // Đánh thức device
            pthread_mutex_unlock(&temp->id.timer_lock);
        }
    }
    pthread_exit(args);
}
```

### Giải thích từng bước:

**Bước ②: Chờ tất cả devices**
```c
while (!temp->id.done && !temp->id.fsh) {
    pthread_cond_wait(&temp->id.event_cond, &temp->id.event_lock);
}
```
- `pthread_cond_wait()`: Timer NGỦ ở đây
- Khi device gọi `pthread_cond_signal(&event_cond)`, Timer thức dậy
- Điều kiện: chờ đến khi `done=1` (xong slot) HOẶC `fsh=1` (kết thúc hoàn toàn)

**Bước ⑤: In Time slot TRƯỚC khi đánh thức**
- Rất quan trọng! Đảm bảo "Time slot X" in TRƯỚC các output của CPU/Loader

**Bước ⑥: Đánh thức devices**
```c
temp->id.done = 0;  // Reset để device biết slot mới
pthread_cond_signal(&temp->id.timer_cond);  // Đánh thức
```

---

## 3. HÀM NEXT_SLOT (Device gọi)

```c
void next_slot(struct timer_id_t * timer_id) {
    // ======= PHẦN 1: BÁO CÁO XONG VIỆC =======
    pthread_mutex_lock(&timer_id->event_lock);
    timer_id->done = 1;  // Đặt cờ "tôi xong rồi"
    pthread_cond_signal(&timer_id->event_cond);  // Đánh thức Timer
    pthread_mutex_unlock(&timer_id->event_lock);

    // ======= PHẦN 2: CHỜ SLOT MỚI =======
    pthread_mutex_lock(&timer_id->timer_lock);
    while (timer_id->done) {  // Chờ Timer reset done = 0
        pthread_cond_wait(&timer_id->timer_cond, &timer_id->timer_lock);
    }
    pthread_mutex_unlock(&timer_id->timer_lock);
}
```

### Timeline của next_slot():

```
Device (CPU/Loader)              Timer
─────────────────────────────────────────────────────
1. done = 1                      
2. signal(event_cond) ─────────► Timer thức dậy
3. wait(timer_cond) ◄─┐          
   [NGỦ Ở ĐÂY]        │          
                      │          (chờ các device khác)
                      │          _time++
                      │          print "Time slot X"
                      │          done = 0
                      └───────── signal(timer_cond)
4. Thức dậy, tiếp tục            
```

### Tại sao check `while (timer_id->done)`?
- Timer set `done = 0` trước khi signal
- Device chờ cho đến khi `done = 0` (nghĩa là Timer đã xử lý xong)
- Dùng `while` thay vì `if` để tránh **spurious wakeup**

---

## 4. HÀM CPU_ROUTINE

```c
static void * cpu_routine(void * args) {
    int time_left = 0;        // Số slot còn lại trong quantum
    struct pcb_t * proc = NULL;  // Process đang chạy
    
    while (1) {
        // ======= PRE-CHECK: Kiểm tra ở ĐẦU slot =======
        if (proc != NULL) {
            if (proc->pc == proc->code->size) {
                // Process chạy hết code → FINISHED
                printf("CPU %d: Processed %d has finished\n", ...);
                free(proc);
                proc = NULL;
                time_left = 0;
            } else if (time_left == 0) {
                // Hết quantum → PUT back to queue
                printf("CPU %d: Put process %d to run queue\n", ...);
                put_proc(proc);  // Trả về hàng đợi
                proc = NULL;
            }
        }
        
        // ======= LẤY PROCESS MỚI =======
        if (proc == NULL) {
            proc = get_proc();  // Lấy từ MLQ
            if (proc == NULL) {
                if (done) {
                    // Không còn process, Loader cũng xong
                    printf("CPU %d stopped\n", id);
                    break;
                }
                next_slot(timer_id);  // Chờ slot tiếp
                continue;
            }
        }
        
        // ======= DISPATCH =======
        if (time_left == 0) {
            printf("CPU %d: Dispatched process %d\n", ...);
            time_left = time_slot;  // Reset quantum
        }
        
        // ======= CHẠY 1 INSTRUCTION =======
        run(proc);
        time_left--;
        
        // ======= CHUYỂN SLOT =======
        next_slot(timer_id);
    }
    
    detach_event(timer_id);  // Báo Timer: CPU này finish
    pthread_exit(NULL);
}
```

### Giải thích PRE-CHECK vs POST-CHECK:

**PRE-CHECK (đúng):** Kiểm tra ở ĐẦU slot
```
Slot 6: run(proc) → time_left = 1 → 0 → next_slot()
Slot 7: PRE-CHECK: time_left == 0 → "Put process" → "Dispatched" → run()
```
→ "Put" và "Dispatched" in CÙNG slot 7 ✓

**POST-CHECK (sai):** Kiểm tra ở CUỐI slot
```
Slot 6: run(proc) → time_left = 0 → POST-CHECK → "Put process" → next_slot()
Slot 7: "Dispatched" → run()
```
→ "Put" in slot 6, "Dispatched" in slot 7 ✗

---

## 5. HÀM LD_ROUTINE

```c
static void * ld_routine(void * args) {
    printf("ld_routine\n");  // Thông báo bắt đầu
    
    while (i < num_processes) {
        // ======= CHỜ ĐẾN ĐÚNG TIME SLOT =======
        while (current_time() < start_time[i]) {
            next_slot(timer_id);  // Ngủ chờ
        }
        
        // ======= LOAD PROCESS =======
        proc = load(path[i]);           // Đọc file process
        proc->mm = malloc(...);         // Tạo mm_struct riêng
        init_mm(proc->mm, proc);        // Khởi tạo page table
        
        // ======= THÔNG BÁO VÀ THÊM VÀO QUEUE =======
        printf("Loaded a process at %s, PID: %d PRIO: %ld\n", ...);
        add_proc(proc);  // Thêm vào MLQ
        
        i++;
        next_slot(timer_id);  // Chuyển slot
    }
    
    done = 1;  // Báo hiệu: không còn process nào cần load
    detach_event(timer_id);  // Báo Timer: Loader finish
    pthread_exit(NULL);
}
```

### Timeline của Loader:

```
Time slot 0: ld_routine bắt đầu
             Process 1 có start_time=1 → chờ
             next_slot() → ngủ

Time slot 1: current_time() == 1 == start_time[0]
             load("p0s") → "Loaded process PID: 1"
             add_proc(proc)
             next_slot() → ngủ

Time slot 2: Process 2 có start_time=2
             load("s3") → "Loaded process PID: 2"
             ...
```

---

## 6. HÀM DETACH_EVENT

```c
void detach_event(struct timer_id_t * event) {
    pthread_mutex_lock(&event->event_lock);
    event->fsh = 1;  // Đặt cờ FINISH
    pthread_cond_signal(&event->event_cond);  // Đánh thức Timer
    pthread_mutex_unlock(&event->event_lock);
}
```

- Khi CPU/Loader kết thúc, gọi `detach_event()`
- Set `fsh = 1` để Timer biết device này không còn hoạt động
- Timer đếm `fsh == event` để biết khi nào tất cả xong

---

## 7. FLOW TỔNG HỢP

```
┌──────────────────────────────────────────────────────────────────────────┐
│                              TIME SLOT 0                                  │
├──────────────────────────────────────────────────────────────────────────┤
│ TIMER: printf("Time slot 0")                                              │
│ TIMER: Đánh thức tất cả devices                                          │
│                                                                           │
│ LOADER: current_time()=0 < start_time[0]=1 → next_slot()                 │
│ CPU 0:  get_proc() = NULL (queue trống) → next_slot()                    │
│ CPU 1:  get_proc() = NULL → next_slot()                                  │
│ ...                                                                       │
│                                                                           │
│ TIMER: Chờ tất cả done=1 → tăng _time=1 → printf("Time slot 1")         │
├──────────────────────────────────────────────────────────────────────────┤
│                              TIME SLOT 1                                  │
├──────────────────────────────────────────────────────────────────────────┤
│ LOADER: current_time()=1 == start_time[0]=1                              │
│         load("p0s") → printf("Loaded PID:1") → add_proc()                │
│         next_slot()                                                       │
│                                                                           │
│ CPU 0:  get_proc() = proc1 → printf("Dispatched 1")                      │
│         run(proc1) → time_left=2→1 → next_slot()                         │
│                                                                           │
│ TIMER: Chờ done → _time=2 → printf("Time slot 2")                        │
├──────────────────────────────────────────────────────────────────────────┤
│                              TIME SLOT 2                                  │
├──────────────────────────────────────────────────────────────────────────┤
│ CPU 0:  PRE-CHECK: time_left=1 > 0 → không Put                           │
│         run(proc1) → liballoc() → printf("liballoc:261")                 │
│         time_left=1→0 → next_slot()                                      │
│                                                                           │
│ LOADER: load("s3") → printf("Loaded PID:2") → add_proc()                 │
│         next_slot()                                                       │
├──────────────────────────────────────────────────────────────────────────┤
│                              TIME SLOT 3                                  │
├──────────────────────────────────────────────────────────────────────────┤
│ CPU 0:  PRE-CHECK: time_left=0 → printf("Put process 1")                 │
│         put_proc(proc1)                                                   │
│         get_proc() = proc2 (prio 39 < 130)                               │
│         printf("Dispatched 2") → run(proc2) → next_slot()                │
│                                                                           │
│ CPU 1:  get_proc() = proc1 → printf("Dispatched 1")                      │
│         run(proc1) → next_slot()                                         │
└──────────────────────────────────────────────────────────────────────────┘
```

---

## 8. TÓM TẮT CÁC LOCK/CONDITION

| Lock/Cond | Ai dùng | Mục đích |
|-----------|---------|----------|
| `event_lock` | Device, Timer | Bảo vệ biến `done`, `fsh` |
| `event_cond` | Device signal, Timer wait | Device báo "xong việc" |
| `timer_lock` | Device, Timer | Bảo vệ việc chờ slot mới |
| `timer_cond` | Timer signal, Device wait | Timer báo "slot mới" |

### Quy tắc sử dụng:
1. **Luôn lock trước khi access shared variable**
2. **Luôn dùng `while` với `cond_wait`** (tránh spurious wakeup)
3. **Signal sau khi đã set biến** (để thread thức dậy thấy đúng giá trị)

---

## 9. TẠI SAO CẦN ĐỒNG BỘ NHƯ VẬY?

### Không có đồng bộ → Race condition:
```
CPU: printf("Dispatched")
TIMER: printf("Time slot 3")   ← Sai thứ tự!
CPU: printf("liballoc")
```

### Có đồng bộ → Đúng thứ tự:
```
TIMER: printf("Time slot 3")
        signal(timer_cond) ─────► CPU thức dậy
CPU: printf("Dispatched")
CPU: printf("liballoc")
CPU: next_slot() ───────────────► Timer nhận done=1
TIMER: Chờ đủ devices → _time++ → printf("Time slot 4")
```

---

## 10. DEBUG TIPS

Nếu chương trình bị **deadlock**:
1. Kiểm tra có quên `pthread_mutex_unlock()` không
2. Kiểm tra điều kiện `while` trong `cond_wait` có đúng không
3. Kiểm tra có device nào không gọi `next_slot()` không

Nếu output **sai thứ tự**:
1. Kiểm tra Timer có in Time slot TRƯỚC khi signal không
2. Kiểm tra PRE-CHECK hay POST-CHECK
3. Kiểm tra Loader-CPU synchronization
