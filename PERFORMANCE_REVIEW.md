# Saynaa OS – Performance and Efficiency Review

## Scope and method
- Static code review of the 32-bit kernel (no runtime profiling available in this environment).
- Attempted `make` (fails here because `nasm` is missing), so findings focus on structural inefficiencies visible in code.

## Prioritized hotspots and estimated impact
1. **Busy-waiting on input and process state (High)**  
   - `kb_getchar()` and `kb_get_scancode()` in `kernel/src/drivers/keyboard.c` spin in tight loops while interrupts are enabled, and `syscall_waitpid` in `kernel/src/sys/syscall.c` busy-loops on `sched_exists`.  
   - Impact: CPU stays fully loaded when waiting for keystrokes or child exit; prevents idle and burns battery/thermal headroom.
   - Optimization: Replace with sleepable wait queues/signals (block on a queue or condition and wake from ISR or process exit path).

2. **Kernel heap allocator fragmentation and linear scans (High)**  
   - `aligned_alloc`/`kmalloc` in `kernel/src/mem/malloc.c` keep a single forward-only list; `kfree` only flips the “used” bit and never coalesces. `mem_find_block` is linear first-fit.  
   - Impact: Allocation/free patterns quickly fragment the heap, and alloc/free paths scale linearly with the number of blocks. Long-running workloads risk premature OOM and variable syscall latency.
   - Optimization: Add boundary tags and free-list coalescing; keep per-size free bins or a next-fit cursor to avoid full-list scans.

3. **Physical page allocator full-list scans (Medium)**  
   - `mmap_find_free` / `mmap_find_free_frame` in `kernel/src/mem/pmm.c` linearly scan the entire bitmap for each allocation request.  
   - Impact: Page allocations during exec/sbrk incur O(n) bitmap walks, which will stall when RAM grows or under frequent paging activity.  
   - Optimization: Maintain a “next free” hint plus bitmap bit-scans (bsf/ctz) or a freelist of runs; optionally switch to a buddy allocator to reduce search time.

4. **Round-robin scheduler bookkeeping overhead (Medium)**  
   - `sched_robin_prune` in `kernel/src/sys/sched_robin.c` traverses and frees invalid nodes on every `sched_next`/`sched_add`, emitting multiple kprintfs inside the critical section.  
   - Impact: Every context switch can walk the entire ring, extending interrupt-off time and adding serial I/O latency.  
   - Optimization: Maintain a separate deferred-reap list or validate once per tick; minimize logging while interrupts are masked.

5. **PS/2 polling loops during init (Low)**  
   - `ps2_wait_write/read` in `kernel/src/drivers/mouse.c` use bounded busy-loops for controller readiness.  
   - Impact: Short spikes at boot; acceptable but could be converted to timeout-based sleeps to improve boot-time fairness.  
   - Optimization: Keep bounded but add small delays or fall back to sleeping if the loop iterates too long.

## Refactoring plan (actionable commits)
1. **Introduce waitable primitives for input/process waiters**  
   - Add a small wait-queue API; update keyboard ISR to signal waiters and make `kb_getchar`/`waitpid` block instead of spinning.
2. **Harden kernel heap allocator**  
   - Add free-block coalescing with boundary tags, and per-size free lists or next-fit cursor to avoid full scans; expose simple telemetry (allocated/free) for debugging.
3. **Optimize physical page allocation**  
   - Track last-allocated bitmap index and use bit-scan helpers; optionally add a simple buddy allocator path for large runs.
4. **Tighten scheduler bookkeeping**  
   - Move `sched_robin_prune` to a periodic maintenance hook or background sweep; reduce kprintfs in IRQ-disabled regions; keep a free-list for dead nodes to cut alloc/free churn.
5. **Polish device init waits**  
   - Make PS/2 readiness waits bounded with minimal delay/timeout logging to avoid long busy spins during slow hardware init.

These steps can be implemented incrementally in the order above to reclaim CPU when idle, reduce allocator latency/fragmentation, and cut scheduler overhead without changing external interfaces.
