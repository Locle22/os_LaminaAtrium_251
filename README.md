# OS Simulation - LamiaAtrium (Course 251)

Dự án mô phỏng Hệ điều hành (Operating System Simulator) được phát triển dựa trên khung chương trình của môn Hệ điều hành (Course 251 - HCMUT). Dự án tập trung vào việc hiện thực hóa các thành phần cốt lõi như Lập lịch (Scheduler), Quản lý bộ nhớ (Memory Management) và Đồng bộ hóa (Synchronization).

Phiên bản này là bản cải tiến (`fork`) từ source code gốc, bao gồm việc sửa lỗi logic nghiêm trọng, hỗ trợ cấu hình bộ nhớ động và đặc biệt là **tích hợp bộ công cụ kiểm thử tự động (Automation Testing Suite)**.

## Tính năng nổi bật

### 1. Hệ thống Lập lịch (Scheduler)
* Hiện thực giải thuật **Multi-Level Queue (MLQ)**.
* Hỗ trợ độ ưu tiên (Priority) và cơ chế time-slot cho từng tiến trình.
* Quản lý `Ready Queue` và `Running List` đảm bảo tính công bằng và hiệu quả CPU.

### 2. Quản lý Bộ nhớ (Memory Management)
* **Virtual Memory:** Hiện thực cơ chế ánh xạ địa chỉ ảo sang vật lý.
* **Paging 64-bit:** Hỗ trợ cơ chế phân trang 5 cấp (5-level paging: PGD, P4D, PUD, PMD, PT) theo chuẩn kiến trúc hiện đại (được định nghĩa trong `mm64.c`).
* **Swapping:** Mô phỏng cơ chế chuyển đổi trang giữa RAM và Swap devices (lên đến 4 thiết bị Swap).
* **Protection:** Tách biệt không gian nhớ giữa User space và Kernel space.

### 3. System Calls
* Hỗ trợ các system call cơ bản: `listsyscall`, `memmap`.
* Bổ sung implementation cho syscall `sys_getprocinfo` .

---


### 1. Bộ kiểm thử tự động (New Feature)
Nhóm em đã thêm 2 script bash giúp tự động hóa quy trình test, thay vì phải chạy tay từng lệnh:

* **`run_all_tests.sh`**:
    * Chạy một tập hợp các test case quan trọng (`os_syscall_list`, `os_0_mlq_paging`, `sched_0`,...).
    * Tự động kiểm tra timeout (tránh treo chương trình).
    * Phân tích log đầu ra để xác nhận CPU scheduling và Syscall hoạt động đúng.
    * Báo cáo tóm tắt Pass/Fail.

* **`test_all_inputs.sh`**:
    * Quét toàn bộ file trong thư mục `input/`.
    * Thực hiện stress test với timeout 10s.
    * Kiểm tra kỹ các chỉ số: CPU ops, Process Loading, Dispatching và Syscall execution.
    * Tạo báo cáo chi tiết trong thư mục `test_results/`.


## Hướng dẫn cài đặt và chạy

### Yêu cầu hệ thống
* Linux (Ubuntu, Debian, Fedora...) hoặc WSL trên Windows.
* GCC Compiler.
* Make.
* Thư viện `pthread`.

### Biên dịch (Build)
Sử dụng `make` để biên dịch toàn bộ dự án:

```bash
make clean
make all

Ngoài ra để chạy toàn bộ test toàn diện:

```bash
chmod +x test_all_inputs.sh
./test_all_inputs.sh

Chạy thủ công:

```bash
./os input_file_name
# Ví dụ: ./os sched
