# EE Demand-Paging Plan

A staged plan to migrate ps2sdk from "virtual = physical, 32 MB total" to a
software-managed virtual memory system with demand paging from a storage
device. The intent is to give applications a large flat address space
(target: 256 MB initially, optionally up to 2 GB) backed by HDD/USB swap,
without forcing every consumer to manage chunked I/O explicitly.

This document is a *plan*, not an implementation. Every phase ends in a
measurable decision point. The plan can be aborted at any phase with no
sunk-cost obligation to continue.

---

## Goals

1. **Larger-than-RAM working sets** — applications can address >32 MB of data
   transparently. Random access to a 200 MB asset becomes a load instruction,
   not an explicit `lseek + read`.
2. **Zero impact on existing code.** Default `malloc` / sbrk / stack / code
   stay physical-pinned. Existing ps2sdk binaries see no behavioral change,
   no fault overhead, no API churn.
3. **Predictable performance.** Page faults only happen on memory backed by
   the new `mmap_file`-style API. Apps that don't use it never pay the cost.

## Non-goals

1. **Pageable `malloc`.** The existing C heap stays physical-pinned by
   default. Apps that want pageable storage opt in via a new API.
2. **Per-process isolation.** PS2 runs one app at a time; we do not need
   multiple address spaces.
3. **POSIX `mmap` semantics on day one.** A simpler "reserve a virtual range,
   back it with a file or with anonymous swap" API is sufficient initially.
4. **Sharing pages between EE and IOP.** IOP has no MMU; IOP-shared memory
   stays in the existing IOP-RAM window and is treated as pinned physical
   memory by the EE.
5. **Paging executable code.** Code stays pinned. Self-modifying code,
   exception vectors, and the TLB miss handler itself cannot fault.

## Memory categories

Every byte of EE memory falls into one of these classes. The class
determines whether faults can happen and whether DMA needs special handling.

| Class | Source | Pinned? | DMA-safe directly? | Faults? |
|-------|--------|---------|--------------------|---------|
| **Code** (`.text`) | Loader-allocated | Yes | n/a | Never |
| **Static data** (`.data`, `.bss`, `.rodata`) | Loader | Yes | Yes | Never |
| **Stack** | Thread allocation | Yes | Yes | Never |
| **C heap** (`malloc`, sbrk) | libc | Yes | **Yes** | Never |
| **DMA-explicit pool** (`memalign`, `dma_alloc`) | libc / new API | Yes | Yes | Never |
| **Paged region** (`vm_mmap_file`, `vm_anon`) | New API | **No** | **Pin first** | Yes |

**Key consequence:** existing code that allocates DMA buffers via `malloc`
or `memalign` continues to work unchanged. The pin/unpin protocol applies
only to the paged region — and only when you DMA out of it. That's a small
audit, not a global retrofit.

## Capacity model

Conceptually:

```
total physical RAM            = 32 MB
- pinned classes (code/data/heap/stack/DMA pool) = X
- TLB handler + page table working set            = ~32 KB
= page-pool free physical                         = 32 MB - X - 32 KB
```

The page pool is whatever physical RAM remains after the pinned classes.
If an app's pinned footprint is 30 MB, only ~2 MB is available for paging
— working sets larger than that thrash. If pinned is 16 MB, 16 MB pages.

**Real-time guidance:**
- Apps whose total *per-frame* working set already fits in 32 MB get zero
  page faults during the frame loop, full stop. (This is the case today;
  paging doesn't change it.)
- Apps whose total data exceeds 32 MB but per-frame working set fits in
  the page pool: paging works, faults amortize to acceptable.
- Apps whose per-frame working set exceeds the page pool: thrashing. This
  scenario is *already impossible today* on a 32 MB system; the new
  architecture doesn't enable it, just doesn't prevent it from being slow.

---

## Hardware constraints to respect

These are immovable. Every architectural choice must respect them.

### EE MMU

- **48-entry TLB.** Tiny by modern standards. Working sets that exceed
  `48 * page_size` thrash the TLB regardless of how clever the page algorithm
  is.
- **Variable page sizes per entry**: 4 KB, 16 KB, 64 KB, 256 KB, 1 MB, 4 MB,
  16 MB. Mixed-size mappings are legal and recommended.
- **No hardware A (accessed) bit, no hardware D (dirty) bit.** Reference
  tracking and dirty tracking must be emulated in software via TLB
  invalidation cycles or page-fault sampling.
- **TLB miss vector at `0x80000000`** (general exception). Must be installed
  carefully without breaking BIOS- or libkernel-installed handlers.
- **TLB entries hold physical frame number + cache attribute** (cached /
  uncached / uncached-accelerated). Page tables must track the cache
  attribute alongside the frame.

### EE memory map (regions that *cannot* be paged)

| Region | Why pinned |
|--------|-----------|
| `0x00000000-0x00080000` (kernel + ISR text) | Faulting in an exception handler is fatal |
| `0x70000000-0x70004000` (scratchpad, 16 KB) | Not in physical map; not TLB-translated |
| `0x10000000-0x1FC00000` (I/O regs, GS, VU, IPU, IOP RAM window, BIOS) | HW-fixed addresses |
| `0xA0000000-0xBFFFFFFF` (KSEG1, uncached) | Bypasses TLB by definition |
| TLB miss handler text + working data | Recursion-fatal |
| Current thread stack + TCBs | Cannot fault while handling a fault |

The pageable region is the cached user space: typically `0x00080000` upward,
plus whatever is reserved for VM-managed virtual ranges (e.g.
`0x40000000-0x4FFFFFFF` for a 256 MB virtual heap).

### EE DMA controllers

- **All 10 DMAC channels work in physical addresses.** VIF0/1, GIF, IPU,
  SIF0/1/2, SPR. None translate through the TLB.
- For pinned classes (`malloc`, stack, static data) virtual = physical
  identity-mapped — DMA from these works without any pin/unpin step.
  This is the common case and existing ps2sdk callers do not need to
  change.
- Only DMA *from a paged region* needs pinning: the buffer's pages must be
  resident, marked pinned for the transfer, and translated to physical at
  kick time. This is opt-in; if you don't allocate from the paged region,
  you don't see the API.

### IOP

- **No MMU.** IOP sees a flat 2 MB address space.
- IOP-shared memory accessed by SIF must be pinned in EE physical RAM with a
  stable physical address until the SIF transfer completes.
- The IOP cannot trigger an EE page fault. If the IOP needs to DMA into
  paged virtual memory, the EE must arrange the pin first.

### Storage as required dependency

A working VM system needs a swap device. The minimums:

| Device | Throughput | Latency | Verdict for swap |
|--------|-----------:|--------:|------------------|
| Internal HDD via ATA | ~10 MB/s | ~5 ms seek | **Primary target**. Acceptable. |
| USB Mass Storage 1.1 | ~700 KB/s | ~10 ms | Workable but slow; viable for read-only paging |
| Memory Card | ~130 KB/s | high | **Unusable** for swap; rule out |
| MX4SIO (SD over SIO2) | ~3-6 MB/s | ~few ms | Workable; secondary target |

The system must refuse to enable paging if no eligible swap device is
present. Apps that don't request paging are unaffected.

---

## Address-space layout (proposed)

```
0x00000000  ┌───────────────────────────────────────────┐
            │ Kernel text + IRQ vectors  (pinned)       │
0x00080000  ├───────────────────────────────────────────┤
            │ Application text + RO data (pinned)       │
            │ Application heap (pinned, sbrk legacy)    │
0x01E00000  ├───────────────────────────────────────────┤
            │ Stack(s)                   (pinned)       │
0x02000000  ├───────────────────────────────────────────┤
            │ Reserved for ps2sdk internals             │
0x10000000  ├───────────────────────────────────────────┤
            │ EE I/O registers           (HW)           │
            │ ...                                       │
0x1FC00000  ├───────────────────────────────────────────┤
            │ BIOS                       (HW)           │
0x20000000  ├───────────────────────────────────────────┤
            │ Uncached mirror of 0x00000000..0x01FFFFFF │
0x30100000  ├───────────────────────────────────────────┤
            │ UCAB mirror                               │
0x40000000  ├───────────────────────────────────────────┤
            │ ===========================               │
            │   PAGEABLE VIRTUAL RANGE                  │  <-- new
            │   256 MB initially, expand later          │
            │ ===========================               │
0x50000000  ├───────────────────────────────────────────┤
            │ Reserved                                  │
0x70000000  └───────────────────────────────────────────┘
            (scratchpad — virtual only, not in TLB)
```

The pageable range can be anywhere not conflicting with HW-fixed regions.
`0x40000000` is far enough from existing layout to avoid collisions.

---

## Architecture decisions (must be made before Phase 1)

| Decision | Options | Notes / lean |
|----------|---------|--------------|
| Default page size | 4 KB / 16 KB / 64 KB / 256 KB / 1 MB | Lean: **64 KB**. Matches FAT cluster size, gives 3 MB TLB span at 48 entries, balances fault latency vs. fragmentation. |
| Mixed page sizes | Yes / No | Yes: large pages for pinned regions (1 MB), 64 KB for paged data. |
| Page table layout | Single-level array / Two-level / Inverted | Lean: **two-level** (PD + PT) keyed on virtual address. 256 MB / 64 KB = 4096 PTEs. PD covers 1 MB regions = 256 PDEs. ~16 KB total page-table memory. |
| Replacement algorithm | FIFO / Random / Clock / Approx-LRU | Lean: **Clock** with software-emulated reference bit (cycle-invalidate then re-fault sets the bit). |
| Swap backend | Raw HDD partition / File on FAT / Pre-allocated file | Lean: **pre-allocated swap file** on the boot device. Simpler to install; no partition tooling. Fast path: HDD; fallback: USB. |
| Swap I/O API | Synchronous / Async with worker thread | Lean: **synchronous initially** (Phase 4); add async (overlap with EE work) in Phase 7. |
| Pin/unpin API | Implicit (per-call) / Explicit (caller-managed) | Lean: **explicit**. `vm_pin(ptr, size)` / `vm_unpin(ptr, size)`. ps2sdk's existing DMA wrappers internally pin. |
| Cache attribute per page | Always cached / Caller-specified | Lean: **caller-specified at mmap time**. DMA-bound regions request UCAB; data-processing regions request cached. |
| Multi-thread fault safety | Lockless / Spinlock / Disable interrupts | Lean: **disable interrupts during the critical section** of the miss handler. EE thread scheduling cooperates with libkernel. |

These should be confirmed (or revised) at the end of Phase 0 based on real
measurements.

---

## Phasing

Each phase ends with a **decision** — proceed, abort, or scope-change. No
phase commits to the next.

### Phase 0 — Research prototype  (1–2 weeks)

**Goal:** answer "is the per-fault latency tolerable on real PS2 hardware?"

- Map a single backing file into a fixed virtual range (no eviction yet).
- Implement only TLB miss → read 64 KB page from file → install entry.
- Pool of fixed physical frames; refuse new fills when exhausted.
- No DMA pin/unpin (manual: don't DMA into the paged range).

**Measure:**
- Average TLB miss handling time (handler entry → resume).
- Storage read time per 64 KB page (HDD vs USB vs MX4SIO).
- Cache-cold and cache-warm fault times.

**Decision criterion:**
- HDD page-in < 8 ms median, < 20 ms p99 → **proceed**.
- HDD page-in > 30 ms median → **abort**, architecture not viable for
  realtime apps on this tier of hardware.
- USB-only setups: document as "best-effort, not realtime".

### Phase 1 — TLB management foundation  (1 week)

**Goal:** install a custom TLB miss handler without breaking the world.

- Identify and override the BIOS / libkernel TLB miss vector cleanly.
- Initially install identity mappings for all currently-used regions
  (so behavior is bit-for-bit identical to today).
- Validate against ps2sdk samples — every sample must continue to work.
- Add diagnostic: count TLB misses, log to a debug ringbuffer.

**Decision criterion:**
- All `samples/` build & run unchanged → **proceed**.
- Any regression → halt, isolate, fix before moving on.

### Phase 2 — Page table data structures  (1 week)

**Goal:** introduce two-level page tables alongside the identity map.

- PDE/PTE structures with: physical frame, present bit, cache attribute,
  software A/D bits, swap location.
- Allocator for PDEs/PTEs from a pinned reserved pool.
- API: `vm_map(va, size, flags)` / `vm_unmap(va, size)`.
- Wire into the TLB miss handler (consult page table on miss).

**Decision criterion:**
- Anonymous mappings (RAM-backed only, no swap) work — `vm_map` returns a
  virtual range, page faults install entries from the physical pool, reads
  and writes land in the right place.

### Phase 3 — Storage backend  (1–2 weeks)

**Goal:** abstract the swap device behind a simple block API.

- Define `swap_dev_t` interface: `swap_read(off, buf, size)`,
  `swap_write(off, buf, size)`, `swap_size`.
- Implement HDD backend (raw partition or pre-allocated file).
- Implement USB backend (file on FAT).
- Pre-allocate a swap region of N pages (N=1024 by default → 64 MB swap).
- Validate independently of paging: `swap_write` then `swap_read` round-trips
  test patterns.

**Decision criterion:**
- HDD read + write throughput within 10% of raw fileXio measurements →
  **proceed**.
- Storage backend stable across hot-unplug, full-disk, error injection →
  **proceed**.

### Phase 4 — Page-in / page-out  (2–3 weeks)

**Goal:** demand paging works end-to-end on a single mapped region.

- TLB miss → consult page table → if not present, allocate physical frame
  (evicting an existing one if the pool is full) → swap-in from backing
  file → install TLB entry.
- Eviction: pick a frame via Clock, write back if dirty, mark PTE not
  present, invalidate any TLB entry.
- Test: open a 200 MB file, map it, random-access reads validate against
  direct fileXio reads.

**Decision criterion:**
- 200 MB random-access benchmark completes correctly → **proceed**.
- Throughput: random-read throughput within 50% of `fileXio + bd_cache` for
  comparable working set → **proceed**. Below that → reassess algorithm.

### Phase 5 — DMA pin protocol for paged regions  (1 week)

**Goal:** allow DMA into/out of paged regions safely. Existing DMA from
pinned classes (`malloc`, stack, static) is unchanged.

- `vm_pin(va, size)` — touch all pages, prevent eviction, return a
  `pin_handle_t` and a list of `(physical_addr, size)` segments.
- `vm_unpin(handle)`.
- Add a debug-build assertion in DMA helpers: if the source/dest virtual
  address falls inside the paged virtual range, require an active pin.
  This catches misuse without disturbing the legacy path.
- Document the "DMA from paged regions requires explicit pin/unpin"
  invariant in the new API's header.

**Decision criterion:**
- Existing samples build and run with zero changes → **proceed**.
- A paged-buffer fileXio test: pin → fileXioRead into paged buffer → unpin
  → read back via virtual addresses. Round-trip correct → **proceed**.
- Any silent corruption from un-pinned DMA-into-paged-buffer scenarios →
  halt, tighten assertions, fix.

### Phase 6 — Heap integration  (1 week)

**Goal:** application heaps optionally allocate from the paged region.

- New API: `vm_malloc(size, flags)` returning a paged pointer.
- Optional: build-time switch to make `malloc` return paged memory by
  default. (Risky; default off until Phase 9.)

**Decision criterion:**
- Sample programs that opt into `vm_malloc` work and benchmark within
  expected margins → **proceed**.

### Phase 7 — Async swap I/O  (2 weeks, optional)

**Goal:** overlap swap reads with EE work.

- Worker thread on EE that owns swap I/O.
- Page-out of dirty pages happens in background.
- Speculative read-ahead based on access patterns (e.g. sequential).

**Decision criterion:**
- Measurable reduction in page-fault stall time on realistic workloads.
- No new race conditions in the pin/unpin path.

### Phase 8 — Real-time tuning  (1–2 weeks)

**Goal:** make the system safe for game-style frame-deadline workloads.

- Pin set: identify and pin every page touched by the frame loop's hot path
  during a profiling phase.
- `vm_lock_region(va, size)` — pin a range for the lifetime of the program.
- Frame-budget audit: zero TLB misses during the inner render loop in a
  reference sample.

**Decision criterion:**
- A sample with a 60-Hz render loop pins enough state that no page faults
  occur during a 10-second run → **proceed**.

### Phase 9 — Hardening  (open-ended)

- Recursive fault prevention: TLB handler text and working memory pinned.
- Stack overflow in fault context → escalate to kernel panic with diagnostics.
- Storage error during page-in → process kill or panic; do not silently
  return zeros.
- Out-of-physical-pool with all pages pinned → panic with reason.
- Out-of-swap → return ENOMEM at the failing API call.
- Concurrent-thread fault safety review.

### Phase 10 — Documentation & API stabilization  (1 week)

- ABI for `vm_*` APIs frozen.
- ps2sdk samples demonstrating each pattern.
- Migration guide for existing apps that want to opt in.

---

## Risk register

| Risk | Severity | Mitigation |
|------|---------:|------------|
| TLB miss handler bug → instant lockup | **High** | Phase 1 stays identity-mapped; only enable paging behind a flag. Extensive Phase 0 testing on emulator before real hw. |
| DMA into evicted page → silent corruption | **High** | Phase 5 pin/unpin contract; debug-build assertions that DMA into the paged virtual range requires an active pin. Pinned-class DMA (the common case) is structurally unaffected. |
| Real-time deadline misses | Low | Faults only happen against the paged region; an app that doesn't `vm_mmap_file` sees zero faults. Per-frame working sets that already fit in 32 MB are unaffected. The bad scenario (per-frame working set > page pool) is already infeasible today. |
| Storage wear (frequent paging on flash) | Medium | Document. Default swap-out only on dirty pages. Provide read-only mappings to skip dirty handling entirely. |
| IOP-EE coordination bug → SIF DMA misses | Medium | Common case: SIF DMA uses pinned-class buffers, no change. Edge case: SIF DMA into paged region requires `vm_pin` first. Add assertion in fileXio's EE side. |
| Recursive fault → kernel crash | **High** | Pin all handler resources; static allocation only inside the handler; pre-validated working set on enter. |
| 48-entry TLB thrashing under realistic working sets | Medium | Phase 7 async + read-ahead; Phase 8 pinning advice. Variable page sizes. |
| Swap fragmentation degrading over time | Low | Use fixed-slot swap (one swap slot per physical frame). |

---

## Measurement plan

For each phase, the same set of microbenchmarks plus phase-specific tests:

**Microbenchmarks** (all on real hw, HDD swap):
- Cold TLB miss latency (page in cache, no swap I/O).
- Cold page fault latency (page on swap, must read).
- Eviction round-trip: dirty page-out + clean page-in.
- Sustained streaming through a 256 MB mapped file.
- Random access through the same.
- Multiple-region random access (cross-page-table-entry working sets).

**System-level tests:**
- All existing ps2sdk samples.
- A "huge file scrub" benchmark (extends `disk_io_perf_sample` to use mmap
  semantics).
- Real-time frame loop with paged texture data, with and without pinning.

**Comparison baseline:**
- Same workload using existing `fileXio + bd_cache` explicit I/O.
- Goal at Phase 4 exit: paging within 50% of explicit I/O on random
  workloads.
- Goal at Phase 7 exit: paging matches or beats explicit I/O on random
  workloads with reuse.

---

## Open questions to answer in Phase 0

1. **Where does the BIOS install the TLB miss vector?** Can we override it
   cleanly, or do we need to compile-time replace the handler in
   `iop/kernel`/`ee/kernel`?
2. **What is the actual cycle cost of a TLB miss handler entry on EE?**
   (Save context, dispatch, install TLB entry, restore.)
3. **Is the 4 GB virtual address space safely usable, or does the BIOS
   reserve regions we haven't mapped?**
4. **How does PCSX2 emulate TLB miss handling?** Are timings representative?
5. **Can we share the TLB handler with libkernel's existing exception
   plumbing, or does it need to be a separate vector?**
6. **What's the recommended way to measure dirty-page rate on a real
   workload?** (Software-only; no hardware D bit.)

---

## Out of scope (for v1)

- Per-process / per-thread address spaces.
- Copy-on-write semantics (fork emulation).
- Shared memory between EE and IOP (still routed via existing IOP RAM window).
- Swap encryption.
- Compressed swap.

---

## Estimated calendar

Optimistic, single-developer, focused work:

| Phase | Effort | Cumulative |
|-------|-------:|-----------:|
| 0 — Research prototype | 2 weeks | 2 weeks |
| 1 — TLB foundation | 1 week | 3 weeks |
| 2 — Page tables | 1 week | 4 weeks |
| 3 — Storage backend | 2 weeks | 6 weeks |
| 4 — Page-in/out | 3 weeks | 9 weeks |
| 5 — DMA pin protocol | 2 weeks | 11 weeks |
| 6 — Heap integration | 1 week | 12 weeks |
| 7 — Async swap | 2 weeks | 14 weeks |
| 8 — Real-time tuning | 2 weeks | 16 weeks |
| 9 — Hardening | open-ended | — |
| 10 — Docs + API freeze | 1 week | ≥17 weeks |

Realistic: **3–5 months** for production quality. Phase 0 alone (≤2 weeks)
gives us the data to decide whether to commit to the rest.

---

## Suggested first action

Spin up a separate branch (`ee_paging`) off master and start Phase 0 there.
Phase 0's deliverable is a measurement report, not production code — its
job is to answer "is this worth doing on PS2 hardware in 2026?" without
committing to the full 4-month investment.
