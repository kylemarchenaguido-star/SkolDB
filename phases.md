# phases.md: Building a B+Tree Database from Scratch (C++)

A disk-resident B+Tree storage engine on fixed-size pages, with its own pager, buffer pool, WAL, crash recovery, concurrency, and network layer. Nothing is reused from the Redis project. Built on your own, using the book *Database Internals* (Alex Petrov) as the main theory source.

---

## 0. How to use this file

- Work the phases **in order**. Don't start phase N+1 until phase N's **Done when** checks pass.
- Each phase has the same sections: **Goal, Build, Layout/Design notes, Investigate, Search terms, Tests, Done when**.
- Tick the checkboxes as you go. Keep a `NOTES.md` where each phase gets a short entry: what you decided, what broke, what you learned.
- Before coding any on-disk structure, write its **byte layout** in a comment or doc. Disk formats are the hardest thing to change later.

---

## 1. Fixed decisions (change only on purpose)

| Decision | Choice |
|---|---|
| Language / build | C++17 or later, CMake |
| Page size | 4096 bytes (make it a compile-time constant so tests can use 256) |
| Byte order | Little-endian on disk |
| Page IDs | `uint32_t`, page 0 is the file header |
| Keys / values | Arbitrary byte strings; keys compared with `memcmp` (then length) |
| Max key size | About page size / 4, so a page always holds several cells |
| Tree type | B+Tree: values only in leaves, leaves linked for range scans |
| Concurrency | Single writer first; latching comes in phase 7 |
| Durability design | WAL with full page images, no-steal for in-flight mini-transactions |
| Debug flags | `-fsanitize=address,undefined` always on in debug; ThreadSanitizer in a separate build |

### Engine API (the engine knows nothing about networking)

```
Status open(path, options)         Status close()
Status put(key, value)             Status get(key, std::string* out)
Status del(key)                    Iterator scan(start_key, end_key)
Status checkpoint()
```

---

## 2. Project layout

```
db/
  CMakeLists.txt
  NOTES.md
  phases.md
  include/
    slice.h  status.h  coding.h  engine.h  config.h
  src/
    io/        file.cpp  fault_file.cpp          # I/O wrapper + fault injection
    pager/     pager.cpp  free_list.cpp
    buffer/    buffer_pool.cpp  page_guard.cpp
    page/      slotted_page.cpp  node_layout.cpp
    btree/     btree.cpp  iterator.cpp  overflow.cpp
    wal/       wal_writer.cpp  wal_reader.cpp  recovery.cpp
    server/    epoll_loop.cpp  protocol.cpp  connection.cpp
  tools/       dbcheck.cpp  dump_page.cpp  cli.cpp
  tests/       differential.cpp  crash_recovery.cpp  fuzz_open.cpp
  bench/       bench_main.cpp
```

---

## Phase 0: Foundations and test harness

**Goal:** the tools that make every later phase testable.

**Build**
- [ ] `Slice` (pointer + length, non-owning), `Status` (code + message), little-endian `encode_u32/u64`, `decode_u32/u64`.
- [ ] CRC32 (write your own table-driven version; you may compare against zlib in a test).
- [ ] File wrapper over `pread`, `pwrite`, `fsync`, `ftruncate`, behind an abstract interface.
- [ ] **Fault-injecting file** that implements the same interface and can: fail after N writes, drop all unsynced writes ("power loss"), write only the first half of a page (torn write), return short writes.
- [ ] Randomized **differential test**: random put/get/del/scan against your engine and `std::map<std::string,std::string>`, comparing every result.
- [ ] A CLI with `put/get/del/scan` for manual poking.

**Investigate**
- Property-based / differential testing, deterministic random seeds (print the seed on failure so any failure is reproducible).
- What `fsync` actually guarantees, and what it doesn't (directory fsync after creating or renaming a file).

**Search terms**
- `fsync directory after rename durability`
- `crash consistency file systems`
- `deterministic simulation testing database`
- `fault injection storage testing`

**Tests / Done when**
- [ ] The differential harness catches a bug you plant on purpose.
- [ ] A failing run can be replayed from its seed.

---

## Phase 1: Pager (disk manager)

**Goal:** a file that is an array of fixed-size pages, with allocation and a free list.

**File header (page 0)**
```
magic (8)  version (4)  page_size (4)  page_count (4)
root_page_id (4)  free_list_head (4)  last_checkpoint_lsn (8)
header_crc32 (4)  ...reserved...
```

**Build**
- [ ] `open/create` file, validate magic, version, and page size.
- [ ] `read_page(id, buf)`, `write_page(id, buf)`, with a CRC32 stored in each page header and verified on read.
- [ ] `allocate_page()`: pop the free list, otherwise extend the file.
- [ ] `free_page(id)`: push onto the free list (the next pointer lives inside the freed page).
- [ ] Header update that is safe against torn writes (ideas: write header twice, or put a sequence number and checksum on two header copies).

**Investigate**
- OS page cache vs `O_DIRECT`; alignment requirements for direct I/O.
- `fallocate` vs `ftruncate` for growing the file.
- Why a 4 KB write is not guaranteed atomic on all hardware.

**Search terms**
- `sqlite file format database header`
- `torn page write atomic sector size`
- `O_DIRECT alignment 512 4096`
- `free list page allocation database`

**Tests / Done when**
- [ ] Allocate, write, free, reallocate, close, reopen: data identical.
- [ ] A flipped byte on disk is detected on read (checksum failure, not a crash).
- [ ] Opening a file with a wrong magic or version fails cleanly.

---

## Phase 2: Buffer pool

**Goal:** a bounded in-memory cache of pages with pinning and eviction.

**Build**
- [ ] Fixed array of frames, `page_id -> frame` hash table, per-frame pin count and dirty flag.
- [ ] `fetch(id)`, `new_page()`, `unpin(id, dirty)`, `flush(id)`, `flush_all()`.
- [ ] LRU eviction first. Never evict a pinned page; write a dirty page back before reusing its frame.
- [ ] RAII `PageGuard` so every pin is released automatically.
- [ ] Hooks for later phases: "page LSN" per frame, and a way to pin a page as "in a mini-transaction, don't evict."

**Investigate**
- LRU vs Clock vs LRU-K vs 2Q; scan resistance (a big scan must not flush the hot set).
- What happens when every frame is pinned (return an error, don't deadlock).

**Search terms**
- `buffer pool manager LRU-K`
- `clock replacement algorithm database`
- `scan resistant cache eviction 2Q`
- `bustub buffer pool manager project`

**Tests / Done when**
- [ ] A 4-frame pool over thousands of pages gives correct results.
- [ ] A leaked pin fails the test suite (assert all pins are zero at shutdown).
- [ ] Hit rate is exposed as a stat.

---

## Phase 3: Slotted page and node format

**Goal:** variable-length cells inside a fixed-size page.

**Common page header**
```
crc32 (4)  page_type (1: leaf/internal/free/overflow)  flags (1)
cell_count (2)  free_start (2)  free_end (2)  page_lsn (8)
extra_ptr (4)   // leaf: right sibling   internal: rightmost child
```

**Layout idea**
```
[ header | slot array -> ...free space... <- cells ]
```
- Slots (2-byte offsets) are kept **sorted by key**; the cell bytes can sit anywhere, so inserting never moves cell data.
- Leaf cell: `[klen][vlen][key][value]` (or `[klen][total_vlen][key][overflow_page_id]` for large values).
- Internal cell: `[child_page_id][klen][key]`.

**Build**
- [ ] `insert_cell`, `remove_cell`, `find_slot` (binary search), `free_space`, `defragment`, `can_fit(cell_size)`.
- [ ] A function that serializes a page to a readable dump (this will save you hours; make `dump_page` a tool).

**Investigate**
- Why slotted pages (vs fixed-size records), and fragmentation after deletes.
- Separator key choice for internal nodes; suffix truncation and prefix compression (optional, later).

**Search terms**
- `slotted page layout database`
- `sqlite btree page format cell pointer array`
- `postgres page layout line pointer`
- `database internals chapter 3 file formats`

**Tests / Done when**
- [ ] A single page behaves exactly like a sorted `std::vector` under random insert/delete.
- [ ] Heavy churn that forces `defragment` loses no data.
- [ ] No cell ever overlaps another, checked by an invariant function.

---

## Phase 4: B+Tree (no durability yet)

**Goal:** a correct tree on top of the buffer pool and slotted pages.

**Build, in this order**
1. [ ] **Search:** root to leaf, then binary search in the leaf.
2. [ ] **Insert without split:** leaf has space.
3. [ ] **Leaf split:** split by *bytes*, not cell count; link siblings; push separator up.
4. [ ] **Internal split:** the median key moves up (is not copied).
5. [ ] **Root split:** allocate a new root, update `root_page_id` in the header.
6. [ ] **Scan iterator:** follow leaf links; stay correct when pages are evicted between `next()` calls.
7. [ ] **Delete, simple:** remove the cell; free a leaf only when it becomes empty.
8. [ ] **Delete, full:** merge or redistribute siblings under a minimum fill threshold; shrink the root when it has one child.
9. [ ] **Overflow pages:** values above a threshold go to a chain of overflow pages.
10. [ ] `check_invariants()`: key order inside and across pages, parent separators bound children, sibling links consistent, no page below minimum fill (except root), no leaked or double-owned pages.

**Investigate**
- Fill factor, and 90/10 splits for sequential (append-only) inserts.
- Bulk loading from sorted input.
- Why B+Tree (not B-Tree) for range scans.

**Search terms**
- `B+tree insertion deletion algorithm variable length keys`
- `btree split by bytes variable sized cells`
- `database internals chapter 4 implementing b-trees`
- `bulk loading b+tree bottom up`
- `bustub b+tree project`

**Tests / Done when**
- [ ] Differential test passes over millions of ops at a tiny page size (256 bytes) where splits and merges happen constantly.
- [ ] `check_invariants()` runs after every N operations and always passes.
- [ ] Scans return correct ranges across splits and deletes.

---

## Phase 5: Durability (WAL and recovery)

**Goal:** committed operations survive crashes; the tree is never corrupt after recovery.

**Design (start simple)**
- **Mini-transaction (mtr):** each `put`/`del` is one atomic unit that may touch several pages (a split touches at least three). Pages modified by an unfinished mtr stay pinned and cannot be evicted (**no-steal**), so recovery never needs undo.
- **WAL record:** `[len][crc32][lsn][type][payload]`
  - `PAGE_IMAGE(page_id, 4096 bytes)`
  - `COMMIT(mtr_id)`
  - `CHECKPOINT(lsn)`
- **Commit:** write full images of all modified pages, then the `COMMIT` record, then `fsync` the WAL. Durable at that moment.
- **WAL rule:** a dirty page may be written to the data file only after the WAL is flushed up to that page's LSN.
- **Recovery:** scan the WAL from the last checkpoint; verify checksums; stop at the first bad or torn record; apply page images for committed mtrs only; discard the rest.
- **Checkpoint:** flush all dirty pages, fsync the data file, write `CHECKPOINT`, then truncate or recycle the WAL.
- **Full page images** also protect you from torn data-page writes.

**Build**
- [ ] WAL writer and reader with checksums and LSNs.
- [ ] mtr abstraction that collects dirty pages and emits records at commit.
- [ ] Recovery on `open`.
- [ ] Checkpointing (manual first, automatic by WAL size later).
- [ ] Group commit: batch several commits into one `fsync`.

**Investigate**
- ARIES: steal/no-steal, force/no-force, redo vs undo, LSNs on pages.
- Physical vs logical vs physiological logging, and ways to reduce write amplification vs full page images.
- Shadow paging / copy-on-write as an alternative design (LMDB).
- `fsync` cost and `fdatasync`; what your disk's write cache does.

**Search terms**
- `ARIES recovery algorithm paper Mohan`
- `write-ahead logging full page writes postgres`
- `group commit write ahead log`
- `torn write detection WAL checksum`
- `lmdb copy on write btree design`
- `database internals chapter 5 transaction processing and recovery`
- `ARIES vs shadow paging`

**Tests / Done when**
- [ ] `kill -9` loop during a running workload: reopen, run `check_invariants()`, compare to a model containing only acknowledged ops. Zero losses.
- [ ] Fault-injection loop: crash after the Nth write for every N in a workload, including half-written pages and dropped unsynced writes. Thousands of cycles pass.
- [ ] Recovery is idempotent (crash *during* recovery, recover again, same result).

---

## Phase 6: Hardening the storage layer

**Build**
- [ ] Free-list updates go through the WAL like any other page change; freed pages get reused.
- [ ] `dbcheck file.db`: offline integrity checker (header, checksums, tree invariants, free-list consistency, leaked pages).
- [ ] Fuzzer that feeds corrupt or truncated files to `open` (must return an error, never crash).
- [ ] Format versioning and a clear error for an unsupported version.
- [ ] Stats: cache hit rate, splits, merges, WAL bytes written, fsync count.

**Search terms**
- `libFuzzer fuzzing file format parser`
- `sqlite integrity_check how it works`
- `postgres amcheck btree`

**Done when**
- [ ] `dbcheck` passes after every crash test and rejects deliberately corrupted files without crashing.
- [ ] Fuzzing for a fixed time budget finds no crashes or sanitizer errors.

---

## Phase 7: Concurrency

**Build, in this order**
1. [ ] **Coarse lock:** reader-writer lock on the whole tree (many readers, one writer). Make the buffer pool thread-safe (page-table lock plus per-frame latch).
2. [ ] **Latch crabbing:** per-page shared/exclusive latches. Release the parent's latch once the child is "safe" (won't split or merge).
3. [ ] **Optimistic descent:** shared latches on the way down; retry with exclusive latches only if a split or merge is needed.
4. [ ] Concurrent-safe iterators.
5. [ ] Interaction with the WAL: concurrent mtrs, ordered LSNs, and group commit with multiple writers.

**Investigate**
- B-link trees (Lehman-Yao): right-sibling pointers plus high keys remove most parent-latching.
- Lock vs latch (a latch protects in-memory structures, a lock protects logical data).
- Deadlock avoidance through latch ordering (always top-down, left-to-right).

**Search terms**
- `latch crabbing b+tree concurrency`
- `Lehman Yao b-link tree`
- `optimistic lock coupling btree`
- `database internals chapter 6 b-tree variants concurrency`
- `ThreadSanitizer data race detection`

**Tests / Done when**
- [ ] Multi-threaded differential test (per-thread key ranges, plus a shared-range test checked at the end) passes under TSan.
- [ ] Read throughput scales with thread count.
- [ ] No deadlocks across a long randomized soak test.

---

## Phase 8: Network layer (written fresh)

**Protocol (binary, length-prefixed)**
```
request:  [len u32][version u8][opcode u8][request_id u32][payload...]
response: [len u32][request_id u32][status u8][payload...]
opcodes:  PING PUT GET DEL SCAN CHECKPOINT
```
- Pipelining allowed; responses matched by `request_id`.
- Max frame size enforced; malformed or truncated frames close the connection, never crash the server.

**Build**
- [ ] Non-blocking sockets with `epoll` (edge-triggered once you are confident).
- [ ] Per-connection state machine with read/write buffers, handling partial reads and writes.
- [ ] Worker thread pool for disk-touching requests so cache misses don't stall the event loop.
- [ ] Idle timeouts, backpressure, and connection limits.
- [ ] Client library and CLI.
- [ ] Optional: scan with pagination (cursor tokens) so a huge range doesn't build one giant response.

**Investigate**
- Level-triggered vs edge-triggered epoll, `EAGAIN` handling, `EPOLLOUT` registration only when there is data to send.
- `io_uring` (stretch goal).
- Slow-client handling and head-of-line blocking.

**Search terms**
- `epoll edge triggered vs level triggered`
- `non blocking server state machine partial read write`
- `length prefixed protocol framing design`
- `beej guide network programming`
- `io_uring tutorial echo server`

**Done when**
- [ ] Many concurrent clients pass the same differential tests through the socket.
- [ ] A fuzzer sending random or truncated frames never crashes or hangs the server.

---

## Phase 9: Benchmarks and tuning

**Workloads**
- Sequential vs random inserts, point reads, range scans, mixed read/write, with the working set smaller than and larger than the buffer pool.

**Sweeps**
- Page size, buffer pool size, fill factor, group-commit batch size, `fsync` on/off (to see its true cost).

**Compare against**
- SQLite and/or LMDB on the same workload. Find out *why* you are slower or faster using `perf`, flamegraphs, and your own stats.

**Search terms**
- `perf flamegraph c++ profiling linux`
- `YCSB benchmark workloads`
- `sqlite benchmark insert speed transaction batching`
- `lmdb benchmark design`

**Done when**
- [ ] You have a table of results and one short paragraph explaining each major gap.

---

## Optional extensions (after phase 9)

- **Transactions:** multi-operation transactions with rollback, then snapshot isolation / MVCC (CMU 15-445 lectures).
- **Copy-on-write B-Tree** (LMDB-style) as a second engine and compare it with your WAL design.
- **LSM engine** behind the same API for a direct comparison.
- **Replication:** ship the WAL to a follower (Database Internals Part II, ch. 8-14).
- **Compression:** per-page or prefix compression.
- **SQL layer:** parser, planner, executor on top of the key-value engine.

---

## Where to research

### Main book (map to phases)
| Phase | *Database Internals* chapters |
|---|---|
| 1, 3 | Ch. 3: File formats |
| 2, 4 | Ch. 2: B-Tree basics, Ch. 4: Implementing B-Trees |
| 5 | Ch. 5: Transaction processing and recovery |
| 6, 7 | Ch. 6: B-Tree variants (copy-on-write, lazy B-Trees, Bw-Trees, etc.) |
| Replication | Ch. 8-14 (Part II: distributed systems) |

### Courses and projects
- **CMU 15-445 / 645 Database Systems** (Andy Pavlo): free lectures on YouTube and course site (15445.courses.cs.cmu.edu). Look at lectures on storage, buffer pools, B+Trees, concurrency, logging and recovery.
- **BusTub**: the course's C++ teaching database (github.com/cmu-db/bustub). Read the project handouts for the buffer pool and B+Tree even if you don't use their code.
- **CMU 15-721** (advanced databases) for later topics.

### Real systems to read
- **SQLite file format docs** (sqlite.org/fileformat.html): the best small, real example of a pager, slotted pages, and a B-Tree. Also read the pages on atomic commit and the WAL (sqlite.org/atomiccommit.html, sqlite.org/wal.html).
- **LMDB** (copy-on-write B+Tree, tiny codebase): good to read when you reach phase 5/6 for the alternative design.
- **PostgreSQL docs** on page layout, WAL, and `amcheck`; the `src/backend/access/nbtree/README` is an excellent explanation of concurrent B-Trees.
- **LevelDB** (github.com/google/leveldb) docs on log format: useful for WAL record framing and checksums even though it is an LSM.
- **WiredTiger / InnoDB** documentation for how production B-Tree engines handle pages, checkpoints, and the buffer pool.

### Papers (search by title and author)
- Mohan et al., **ARIES** (recovery).
- Lehman and Yao, **Efficient Locking for Concurrent Operations on B-Trees**.
- Graefe, **Modern B-Tree Techniques** (very practical survey; great for phases 3, 4, 7).
- Graefe, **A Survey of B-Tree Locking Techniques**.
- Comer, **The Ubiquitous B-Tree** (classic overview).
- Pillai et al., **All File Systems Are Not Created Equal** (crash consistency pitfalls, great for phase 5).

### Community and Q&A
- Stack Overflow / Database Administrators Stack Exchange (tag: `b-tree`, `write-ahead-log`).
- r/databasedevelopment and the *Build Your Own Database* community resources.
- GitHub: search for small educational B-Tree databases (`btree database from scratch c++`) and read how others solved the same page-format problems. Read to understand design choices, not to copy.
- Andy Pavlo's and Alex Petrov's talks and blog posts.

---

## How to get research help (including from Claude)

Good questions are specific. Templates:

- **Design review:** "Here is my page layout and split algorithm [paste]. What invariants could break after a crash between these two writes?"
- **Explain a concept:** "Explain steal vs no-steal and force vs no-force in ARIES, and which combination my design (full page images, no-steal) corresponds to."
- **Debug a failing seed:** "My differential test fails with seed 8812 after op 40213: expected X, got Y. Here is the tree dump before and after. What is the likely bug?"
- **Compare designs:** "WAL with full page images vs copy-on-write with a shadow root: write amplification, recovery complexity, and read performance for my case."
- **Find the right source:** "Which chapter, paper, or source file would show me how SQLite handles X?"
- **Review code:** paste one function at a time (a split, a recovery loop) and ask for invariant violations, error-path leaks, and undefined behavior.

When stuck, narrow it down first:
1. Reproduce with the smallest page size and the fewest operations.
2. Dump the pages (`dump_page`) before and after the failing op.
3. Run `check_invariants()` after every operation to find the *first* point where the tree becomes invalid.
4. Check the sanitizers (ASan/UBSan/TSan) before assuming a logic bug.

---

## Tools to install

- `gdb` or `lldb`, `valgrind` (optional), `perf`
- `clang` with ASan/UBSan/TSan and libFuzzer
- `hexdump` / `xxd` to inspect database files by hand
- `strace` to confirm exactly which syscalls (`write`, `fsync`) your engine issues and in what order
- `cmake`, `ninja`, `clang-format`, `clang-tidy`

---

## Rules to keep this finishable

1. Byte layout written before code, for every on-disk structure.
2. Test or invariant checker written before the implementation of each phase.
3. Every file gets a magic number and a version.
4. Never trust data read from disk: verify checksums and bounds before using it.
5. One thing at a time: no SQL, compression, or replication until phase 9 is done.
6. Commit after each passing "Done when" item, with a message saying what now works.

---

## Progress tracker

| Phase | Name | Status | Notes |
|---|---|---|---|
| 0 | Foundations and test harness | [ ] | |
| 1 | Pager | [ ] | |
| 2 | Buffer pool | [ ] | |
| 3 | Slotted page and node format | [ ] | |
| 4 | B+Tree | [ ] | |
| 5 | WAL and recovery | [ ] | |
| 6 | Hardening | [ ] | |
| 7 | Concurrency | [ ] | |
| 8 | Network layer | [ ] | |
| 9 | Benchmarks | [ ] | |
