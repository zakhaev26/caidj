# CAIDJ — Complete C++ Implementation Design Document
### Concurrency Aware Indexing for Distributed Join
**IIIT Bhubaneswar BTech Final Year Project, 2026**

---

## Table of Contents

1. [Project Overview](#1-project-overview)
2. [Repository Layout](#2-repository-layout)
3. [Build System — CMake](#3-build-system--cmake)
4. [Third-Party Dependencies](#4-third-party-dependencies)
5. [Global Type System & Conventions](#5-global-type-system--conventions)
6. [Module 1 — Data Generator (`datagen`)](#6-module-1--data-generator-datagen)
7. [Module 2 — Base Index Interface (`index`)](#7-module-2--base-index-interface-index)
8. [Module 3 — Naive Hash Join Baseline (`nhj`)](#8-module-3--naive-hash-join-baseline-nhj)
9. [Module 4 — ECHI Index (`echi`)](#9-module-4--echi-index-echi)
10. [Module 5 — MPI-MVCC Index (`mpimvcc`)](#10-module-5--mpi-mvcc-index-mpimvcc)
11. [Module 6 — BF-CSI Index (`bfcsi`)](#11-module-6--bf-csi-index-bfcsi)
12. [Module 7 — Join Executor (`join`)](#12-module-7--join-executor-join)
13. [Module 8 — Transaction Manager (`txn`)](#13-module-8--transaction-manager-txn)
14. [Module 9 — Benchmark Harness (`bench`)](#14-module-9--benchmark-harness-bench)
15. [Module 10 — Metrics Collector (`metrics`)](#15-module-10--metrics-collector-metrics)
16. [Module 11 — CLI Driver (`main`)](#16-module-11--cli-driver-main)
17. [Module 12 — Configuration System (`config`)](#17-module-12--configuration-system-config)
18. [Module 13 — Unit Tests (`tests`)](#18-module-13--unit-tests-tests)
19. [End-to-End Data Flow](#19-end-to-end-data-flow)
20. [Algorithm Pseudocode (C++ Level)](#20-algorithm-pseudocode-c-level)
21. [CLI Reference](#21-cli-reference)
22. [Input / Output File Formats](#22-input--output-file-formats)
23. [Threading Model & Synchronisation Primitives](#23-threading-model--synchronisation-primitives)
24. [Memory Layout & Sizing Guidelines](#24-memory-layout--sizing-guidelines)
25. [Error Handling Strategy](#25-error-handling-strategy)
26. [Logging](#26-logging)
27. [Build, Run, and Test Commands](#27-build-run-and-test-commands)
28. [Known Design Constraints & Trade-offs](#28-known-design-constraints--trade-offs)
29. [Glossary](#29-glossary)

---

## 1. Project Overview

### 1.1 What CAIDJ Is

CAIDJ is a **single-binary, multi-threaded C++ simulator** that models a distributed equi-join between two relations (R and S) where concurrent write transactions continuously insert/delete tuples while join-probe threads simultaneously query the shared index structure.

The simulator implements and benchmarks four strategies:

| Tag | Full Name | Role |
|-----|-----------|------|
| `NHJ` | Naive Distributed Hash Join | Baseline — single global reader-writer lock |
| `ECHI` | Epoch-Based Concurrent Hash Index | Epoch-isolated probes; batch-applied writes |
| `MPI-MVCC` | Multi-Version Partition Index | Per-key version chains; non-blocking reads |
| `BF-CSI` | Bloom Filter + Concurrent Skip-list Index | Probabilistic pre-filter; CAS-based skip-list |

### 1.2 Scope

- Equi-join on a single integer key column.
- Single process; threads model distributed nodes.
- In-memory only; no disk I/O for index structures.
- Synthetic TPC-H-style data; no real database connection.
- Results emitted as structured JSON + human-readable tables to stdout/file.

### 1.3 Non-Goals

- Real network distribution (future work).
- Disk-based indexes.
- SQL parsing.
- Non-equi or multi-key joins.

---

## 2. Repository Layout

```
caidj/
├── CMakeLists.txt                  # Root CMake (see §3)
├── cmake/
│   └── Dependencies.cmake          # FetchContent / find_package wrappers
├── include/
│   └── caidj/
│       ├── common.hpp              # Global types, macros, constants
│       ├── config.hpp              # Config struct + TOML loader declaration
│       ├── datagen.hpp             # Data generator interface
│       ├── index/
│       │   ├── base_index.hpp      # Abstract base class BaseIndex
│       │   ├── nhj_index.hpp       # NHJ (baseline)
│       │   ├── echi_index.hpp      # ECHI
│       │   ├── mpimvcc_index.hpp   # MPI-MVCC
│       │   └── bfcsi_index.hpp     # BF-CSI
│       ├── join/
│       │   └── join_executor.hpp   # JoinExecutor
│       ├── txn/
│       │   └── txn_manager.hpp     # TransactionManager
│       ├── bench/
│       │   └── benchmark.hpp       # Benchmark orchestrator
│       ├── metrics/
│       │   └── metrics.hpp         # MetricsCollector
│       └── util/
│           ├── bloom_filter.hpp    # Standalone Bloom filter
│           ├── skiplist.hpp        # Lock-free concurrent skip-list
│           ├── zipf.hpp            # Zipfian distribution generator
│           ├── logger.hpp          # Lightweight logger
│           └── thread_pool.hpp     # Fixed-size thread pool
├── src/
│   ├── config.cpp
│   ├── datagen.cpp
│   ├── index/
│   │   ├── nhj_index.cpp
│   │   ├── echi_index.cpp
│   │   ├── mpimvcc_index.cpp
│   │   └── bfcsi_index.cpp
│   ├── join/
│   │   └── join_executor.cpp
│   ├── txn/
│   │   └── txn_manager.cpp
│   ├── bench/
│   │   └── benchmark.cpp
│   ├── metrics/
│   │   └── metrics.cpp
│   ├── util/
│   │   ├── bloom_filter.cpp
│   │   ├── skiplist.cpp
│   │   ├── zipf.cpp
│   │   ├── logger.cpp
│   │   └── thread_pool.cpp
│   └── main.cpp
├── tests/
│   ├── CMakeLists.txt
│   ├── test_bloom_filter.cpp
│   ├── test_skiplist.cpp
│   ├── test_echi.cpp
│   ├── test_mpimvcc.cpp
│   ├── test_bfcsi.cpp
│   ├── test_nhj.cpp
│   ├── test_join_executor.cpp
│   ├── test_datagen.cpp
│   └── test_metrics.cpp
├── configs/
│   ├── default.toml                # Default benchmark config
│   ├── low_concurrency.toml
│   └── high_concurrency.toml
├── scripts/
│   ├── run_all.sh                  # Run all concurrency configs
│   └── plot_results.py             # Optional: matplotlib charts from JSON
├── data/                           # Generated datasets land here
└── results/                        # JSON + CSV results land here
```

---

## 3. Build System — CMake

### 3.1 Root `CMakeLists.txt` Specification

```
cmake_minimum_required: 3.22
project: CAIDJ
language: CXX
C++ standard: 20 (CMAKE_CXX_STANDARD 20, REQUIRED ON)

compile options (all targets):
  -Wall -Wextra -Wpedantic
  Release: -O3 -DNDEBUG -march=native
  Debug:   -g -O0 -fsanitize=address,undefined

include directory: ${PROJECT_SOURCE_DIR}/include

targets:
  caidj_lib   — STATIC library from all src/**/*.cpp except main.cpp
  caidj       — EXECUTABLE linking caidj_lib; source: src/main.cpp
  caidj_tests — EXECUTABLE (see §18); links caidj_lib + GTest

install:
  caidj binary → bin/
  caidj_lib    → lib/
```

### 3.2 `cmake/Dependencies.cmake`

Fetch the following via `FetchContent` (no system install required):

| Library | Version | Purpose |
|---------|---------|---------|
| `google/googletest` | 1.14.0 | Unit testing |
| `nlohmann/json` | 3.11.3 | JSON output |
| `ToruNiina/toml11` | 4.2.0 | TOML config parsing |
| `gabime/spdlog` | 1.13.0 | Structured logging |
| `google/benchmark` | 1.8.3 | Micro-benchmarks (optional) |

MurmurHash3: **copy two files** (`MurmurHash3.h`, `MurmurHash3.cpp`) directly into `src/util/` and `include/caidj/util/`. No FetchContent needed — these are public-domain standalone files from Austin Appleby's original implementation.

---

## 4. Third-Party Dependencies

### 4.1 Dependency Detail

#### MurmurHash3
- Files: `MurmurHash3.h` / `MurmurHash3.cpp`
- Source: https://github.com/aappleby/smhasher
- Usage: Provides `MurmurHash3_x64_128()` used by `BloomFilter`
- Integration: Compiled as part of `caidj_lib`

#### nlohmann/json
- Header-only after FetchContent
- Usage: Serialising `BenchmarkResult` → JSON output files

#### toml11
- Header-only after FetchContent
- Usage: Parsing `configs/*.toml` into `Config` struct

#### spdlog
- Usage: All logging calls (`LOG_INFO`, `LOG_DEBUG`, `LOG_ERROR` macros)
- Pattern: `[%H:%M:%S.%e] [%l] [%n] %v`
- Sinks: stdout + rotating file (`logs/caidj.log`, 5 MB max, 3 rotations)

---

## 5. Global Type System & Conventions

### 5.1 File: `include/caidj/common.hpp`

```cpp
// ── Primitive type aliases ───────────────────────────────────
using Key    = int64_t;   // Join key value
using TID    = int64_t;   // Tuple identifier (row ID in relation)
using TxnID  = uint64_t;  // Transaction / timestamp counter
using NodeID = uint32_t;  // Simulated node index

// ── Sentinel values ──────────────────────────────────────────
constexpr TxnID INF_TS   = std::numeric_limits<TxnID>::max();
constexpr Key   NULL_KEY  = std::numeric_limits<Key>::min();
constexpr TID   NULL_TID  = -1;

// ── Operation tag ────────────────────────────────────────────
enum class OpType : uint8_t { INSERT, DELETE };

// ── A write operation submitted to the index ─────────────────
struct WriteOp {
    OpType op;
    Key    key;
    TID    tid;
};

// ── A single tuple in relation R or S ────────────────────────
struct Tuple {
    TID tid;
    Key key;
    // Payload fields (unused in join, kept for realism)
    int64_t val1;
    int64_t val2;
};

// ── Result of one probe ──────────────────────────────────────
struct ProbeResult {
    Key              key;
    std::vector<TID> matching_tids;  // TIDs in R matching this S.key
    bool             was_fast_path;  // BF-CSI: true if BF said "absent"
};

// ── Macro: non-copyable, non-movable base ───────────────────
#define CAIDJ_NONCOPYABLE(T)               \
    T(const T&)            = delete;       \
    T& operator=(const T&) = delete;       \
    T(T&&)                 = delete;       \
    T& operator=(T&&)      = delete

// ── Enum for selecting protocol ──────────────────────────────
enum class Protocol : uint8_t { NHJ, ECHI, MPI_MVCC, BF_CSI };

Protocol protocol_from_string(const std::string& s);
std::string protocol_to_string(Protocol p);
```

### 5.2 Coding Conventions

| Convention | Rule |
|------------|------|
| Header guards | `#pragma once` everywhere |
| Namespaces | All code in `namespace caidj`. Sub-namespaces: `caidj::index`, `caidj::util`, `caidj::bench`, `caidj::txn`, `caidj::join` |
| Memory | Prefer `std::unique_ptr` for owning pointers; raw pointers only for non-owning observers |
| Atomics | Always `std::atomic<T>` with explicit memory order; never implicit `seq_cst` unless correctness demands it |
| Locks | `std::mutex` + `std::unique_lock`; `std::shared_mutex` + `std::shared_lock` for RW patterns; never `new`-allocated mutexes |
| Thread safety | Every public method of index classes is documented as "thread-safe" or "not thread-safe" in its header |
| Errors | Use `std::expected<T, CaidjError>` (C++23) OR `std::optional<T>` + out-param for errors within hot paths |
| Naming | `snake_case` for variables/functions; `PascalCase` for classes/structs; `UPPER_SNAKE` for macros/constants |
| File encoding | UTF-8; Unix line endings |

---

## 6. Module 1 — Data Generator (`datagen`)

### 6.1 Purpose

Generates two synthetic relations R (LINEITEM-like, larger) and S (ORDERS-like, smaller) with integer join keys drawn from a Zipfian distribution.

### 6.2 Header: `include/caidj/datagen.hpp`

```
namespace caidj

class DataGen
  Constructor:
    DataGen(uint64_t seed, double zipf_alpha, int64_t domain_size)
    // domain_size: number of distinct key values possible

  Methods:
    Relation generate_R(int64_t num_tuples)
      // Returns vector<Tuple> with TID 0..num_tuples-1
      // Keys sampled from Zipf(alpha, domain_size)

    Relation generate_S(int64_t num_tuples)
      // Same distribution; TIDs offset by 10^9 to avoid collision

    WriteOp generate_write_op(OpType op)
      // Generates a random (op, key, tid) for concurrent write threads
      // key: Zipf-distributed; tid: random in [0, 2^31)

    static void save_csv(const Relation& rel, const std::string& path)
    static Relation load_csv(const std::string& path)

using Relation = std::vector<Tuple>
```

### 6.3 Implementation Notes

- **Zipfian sampler** (`util/zipf.hpp`): Use the rejection-inversion algorithm by Hörmann & Derflinger (1996). This generates samples in O(1) amortised time without building a full CDF table.
  - Constructor: `ZipfSampler(double alpha, int64_t n, uint64_t seed)`
  - Method: `int64_t next()` — returns a value in `[1, n]` following Zipf(α)
- `generate_R` assigns TIDs sequentially starting at 0.
- `generate_S` assigns TIDs sequentially starting at `1'000'000'000LL`.
- CSV format: `tid,key,val1,val2` (see §22).

### 6.4 Input / Output

| Parameter | Type | Default |
|-----------|------|---------|
| `seed` | `uint64_t` | 42 |
| `zipf_alpha` | `double` | 1.2 |
| `domain_size` | `int64_t` | 200,000 |
| R tuples | `int64_t` | 150,000 |
| S tuples | `int64_t` | 50,000 |

Output: `data/relation_R.csv`, `data/relation_S.csv`

---

## 7. Module 2 — Base Index Interface (`index`)

### 7.1 Header: `include/caidj/index/base_index.hpp`

```cpp
namespace caidj::index {

class BaseIndex {
public:
    virtual ~BaseIndex() = default;

    // Thread-safe: probe the index for key. Returns all matching TIDs.
    virtual std::vector<TID> probe(Key key) = 0;

    // Thread-safe: insert (key, tid) pair.
    virtual void insert(Key key, TID tid) = 0;

    // Thread-safe: remove (key, tid) pair.
    virtual void remove(Key key, TID tid) = 0;

    // NOT thread-safe: bulk-load all tuples in relation R before
    // concurrent operations begin. Must be called once before probe/insert/remove.
    virtual void bulk_load(const Relation& R) = 0;

    // Returns a snapshot of internal metrics (contention events, version counts, etc.)
    virtual IndexStats get_stats() const = 0;

    // Protocol tag
    virtual Protocol protocol() const noexcept = 0;
};

struct IndexStats {
    uint64_t probe_count       = 0;
    uint64_t probe_blocked     = 0;  // times a probe had to wait
    uint64_t write_count       = 0;
    uint64_t epoch_transitions = 0;  // ECHI only
    uint64_t gc_runs           = 0;  // MPI-MVCC only
    uint64_t bf_fast_path      = 0;  // BF-CSI: definite misses
    uint64_t bf_false_positive = 0;  // BF-CSI: BF said present but skip-list said absent
    uint64_t bf_rebuilds       = 0;  // BF-CSI: filter rebuilds
    double   version_chain_avg = 0;  // MPI-MVCC: avg chain length at snapshot
    uint64_t memory_bytes      = 0;  // self-reported heap usage estimate
};

} // namespace caidj::index
```

All four implementations (`NHJIndex`, `ECHIIndex`, `MPIMVCCIndex`, `BFCSIIndex`) inherit from `BaseIndex`. They are created via a factory function:

```cpp
// Factory in base_index.hpp:
std::unique_ptr<BaseIndex> make_index(Protocol p, const Config& cfg);
```

---

## 8. Module 3 — Naive Hash Join Baseline (`nhj`)

### 8.1 Data Structure

```
NHJIndex:
  std::unordered_map<Key, std::vector<TID>>  table_
  std::shared_mutex                           rw_mutex_
  std::atomic<uint64_t>                       probe_count_
  std::atomic<uint64_t>                       probe_blocked_
  std::atomic<uint64_t>                       write_count_
```

### 8.2 Algorithm

**`bulk_load(R)`** (called once, no lock needed):
```
FOR each tuple t in R:
    table_[t.key].push_back(t.tid)
```

**`probe(key)`**:
```
ACQUIRE shared_lock(rw_mutex_)   // blocks if writer holds exclusive lock
  probe_count_ += 1
  result = table_[key]           // O(1) hash lookup
RELEASE shared_lock
RETURN result
```
If the lock was contended (i.e., `try_lock_shared()` returned false), increment `probe_blocked_` before acquiring via blocking call.

**`insert(key, tid)`**:
```
ACQUIRE unique_lock(rw_mutex_)   // blocks ALL readers and writers
  table_[key].push_back(tid)
  write_count_ += 1
RELEASE unique_lock
```

**`remove(key, tid)`**:
```
ACQUIRE unique_lock(rw_mutex_)
  auto& v = table_[key]
  v.erase(std::remove(v.begin(), v.end(), tid), v.end())
  write_count_ += 1
RELEASE unique_lock
```

### 8.3 Thread Safety Contract

All public methods are thread-safe. The implementation uses `std::shared_mutex` to allow concurrent reads but exclusive writes, matching the academic "reader-writer lock" model.

---

## 9. Module 4 — ECHI Index (`echi`)

### 9.1 Conceptual Design

Time is divided into discrete **epochs**. During epoch `e`:
- Probe threads read from a frozen hash map `H[e]` — no lock required during read.
- Write threads append to a pending delta buffer `Delta[e]` — lock-free via mutex-protected deque.
- Epoch transition (e → e+1) merges `Delta[e]` into `H[e+1]`. Transition blocks until all active probes in epoch `e` complete.

### 9.2 Data Structures

```
ECHIIndex:
  // Frozen index (read-only during epoch)
  std::shared_ptr<std::unordered_map<Key, std::vector<TID>>>  current_map_
  // next_map_ being built during transition
  std::shared_ptr<std::unordered_map<Key, std::vector<TID>>>  next_map_

  // Pending writes for current epoch
  std::mutex                    delta_mutex_
  std::vector<WriteOp>          delta_buffer_

  // Epoch state
  std::atomic<uint64_t>         epoch_          // current epoch number
  std::atomic<int64_t>          active_readers_ // count of probes in current epoch
  std::mutex                    transition_mutex_
  std::condition_variable       readers_done_cv_

  // Config
  size_t   delta_threshold_     // max delta size before forced transition (θ)
  uint64_t epoch_interval_ms_   // time-based transition interval (δT)

  // Background epoch timer thread
  std::thread                   epoch_timer_thread_
  std::atomic<bool>             stop_flag_

  // Metrics
  std::atomic<uint64_t>         probe_count_
  std::atomic<uint64_t>         probe_blocked_
  std::atomic<uint64_t>         write_count_
  std::atomic<uint64_t>         epoch_transitions_
```

### 9.3 Algorithm — ECHI-Probe

```
INPUT:  key k
OUTPUT: vector<TID>

1. snap_map = std::atomic_load(&current_map_)  // acquire snapshot of shared_ptr
2. active_readers_.fetch_add(1, memory_order_acquire)
3. result = (*snap_map)[k]  // O(1) read, no lock
4. active_readers_.fetch_sub(1, memory_order_release)
5. IF active_readers_ == 0: notify readers_done_cv_
6. RETURN result
```

*Wait-free: step 3 never blocks.*

### 9.4 Algorithm — ECHI-Write

```
INPUT: WriteOp (op, k, t)

1. LOCK delta_mutex_
2. delta_buffer_.push_back({op, k, t})
3. should_transition = (delta_buffer_.size() >= delta_threshold_)
4. UNLOCK delta_mutex_

5. IF should_transition:
     trigger_epoch_transition()
```

### 9.5 Algorithm — Epoch Transition

```
FUNCTION trigger_epoch_transition():

1. LOCK transition_mutex_  (only one thread does this at a time)
2. WAIT ON readers_done_cv_ UNTIL active_readers_ == 0
3. LOCK delta_mutex_
4. local_delta = std::move(delta_buffer_)
5. UNLOCK delta_mutex_

6. next_map_ = std::make_shared<unordered_map>(*current_map_)
7. FOR each op in local_delta:
     IF op.op == INSERT:
         (*next_map_)[op.key].push_back(op.tid)
     ELSE:  // DELETE
         auto& v = (*next_map_)[op.key]
         v.erase(remove(v.begin(), v.end(), op.tid), v.end())

8. std::atomic_store(&current_map_, next_map_)
9. epoch_.fetch_add(1, memory_order_seq_cst)
10. epoch_transitions_ += 1
11. UNLOCK transition_mutex_
```

### 9.6 Background Timer Thread

```
THREAD epoch_timer_thread_:
  WHILE NOT stop_flag_:
    sleep(epoch_interval_ms_)
    trigger_epoch_transition()
```

The timer-triggered transition fires even if the delta is not full, ensuring bounded write staleness.

### 9.7 Configuration Parameters

| Parameter | Config key | Default |
|-----------|------------|---------|
| Delta threshold (θ) | `echi.delta_threshold` | 1000 |
| Epoch interval (δT) | `echi.epoch_interval_ms` | 100 |

---

## 10. Module 5 — MPI-MVCC Index (`mpimvcc`)

### 10.1 Conceptual Design

Each key in the index has a **version chain**: a singly-linked list of version nodes sorted by commit timestamp (newest first). A probe arriving at read timestamp `t_r` walks the chain and returns the first version node `v` satisfying `v.ts_commit <= t_r < v.ts_delete`. Writers create new version nodes without modifying any node that a reader might be accessing. A GC thread periodically prunes nodes whose `ts_delete` is below the minimum active read timestamp.

### 10.2 Data Structures

```cpp
struct VersionNode {
    std::unordered_set<TID> tids;     // set of tuple IDs for this version
    TxnID ts_commit;                  // when this version became visible
    std::atomic<TxnID> ts_delete;     // INF_TS if live; set by next write
    std::shared_ptr<VersionNode> next; // older version
};

MPIMVCCIndex:
  std::unordered_map<Key, std::shared_ptr<VersionNode>>  chains_
  std::shared_mutex                                        chains_rw_  // protects chains_ map itself
  std::unordered_map<Key, std::mutex>                      key_mutexes_ // per-key write mutex
  std::mutex                                               key_mutexes_map_mutex_ // protects key_mutexes_ map

  std::atomic<TxnID>                global_ts_          // monotonic timestamp counter
  std::atomic<TxnID>                min_active_read_ts_ // updated by probes

  // GC
  std::thread                        gc_thread_
  std::atomic<bool>                  stop_flag_
  uint64_t                           gc_interval_ms_     // default 500

  // Active read tracking (for min_active_read_ts_ computation)
  std::mutex                         active_reads_mutex_
  std::multiset<TxnID>               active_read_timestamps_

  // Metrics
  std::atomic<uint64_t>              probe_count_
  std::atomic<uint64_t>              write_count_
  std::atomic<uint64_t>              gc_runs_
  std::atomic<uint64_t>              gc_nodes_freed_
```

### 10.3 Algorithm — MPI-MVCC-Probe

```
INPUT:  key k
OUTPUT: vector<TID>

1. t_r = global_ts_.load(memory_order_acquire)
   // t_r is the read timestamp (snapshot point)

2. LOCK active_reads_mutex_
   active_read_timestamps_.insert(t_r)
   UPDATE min_active_read_ts_
   UNLOCK active_reads_mutex_

3. ACQUIRE shared_lock(chains_rw_)
4. it = chains_.find(k)
5. IF it == chains_.end(): goto DONE_EMPTY
6. v = it->second  // head of version chain (newest)
7. RELEASE shared_lock

8. WHILE v != nullptr:
     ts_c = v->ts_commit
     ts_d = v->ts_delete.load(memory_order_acquire)
     IF ts_c <= t_r AND t_r < ts_d:
         result = vector<TID>(v->tids.begin(), v->tids.end())
         GOTO DONE
     v = v->next

DONE_EMPTY:
   result = {}

DONE:
9. LOCK active_reads_mutex_
   active_read_timestamps_.erase(active_read_timestamps_.find(t_r))
   UPDATE min_active_read_ts_
   UNLOCK active_reads_mutex_

10. RETURN result
```

*Probe never acquires a write lock; it is non-blocking w.r.t. writers.*

### 10.4 Algorithm — MPI-MVCC-Write

```
INPUT: op (INSERT or DELETE), key k, tid t

1. ts_w = global_ts_.fetch_add(1, memory_order_seq_cst) + 1
   // ts_w is the commit timestamp for this write

2. GET or CREATE per-key mutex for k
3. LOCK per-key mutex

4. ACQUIRE shared_lock(chains_rw_)   // read chains_ map
5. head = chains_[k]                  // may be nullptr
6. RELEASE shared_lock

7. // Compute new tids set
   new_tids = (head != nullptr) ? head->tids : {}
   IF op == INSERT:  new_tids.insert(t)
   ELSE:             new_tids.erase(t)

8. v_new = make_shared<VersionNode>()
   v_new->tids      = new_tids
   v_new->ts_commit = ts_w
   v_new->ts_delete = INF_TS
   v_new->next      = head

9. IF head != nullptr:
     head->ts_delete.store(ts_w, memory_order_release)
     // Schedule old head for GC if ts_delete < min_active_read_ts_

10. ACQUIRE unique_lock(chains_rw_)   // write to chains_ map
    chains_[k] = v_new
    RELEASE unique_lock

11. UNLOCK per-key mutex
```

### 10.5 GC Thread Algorithm

```
THREAD gc_thread_:
  WHILE NOT stop_flag_:
    sleep(gc_interval_ms_)
    gc_runs_ += 1

    safe_ts = min_active_read_ts_.load()

    ACQUIRE shared_lock(chains_rw_)
    FOREACH (key, chain_head) in chains_:
      v = chain_head
      WHILE v != nullptr AND v->next != nullptr:
        // Attempt to prune v->next if it is fully superseded
        next = v->next
        IF next->ts_delete.load() < safe_ts:
          v->next = next->next   // unlink; shared_ptr ref drops
          gc_nodes_freed_ += 1
        v = v->next
    RELEASE shared_lock
```

### 10.6 Configuration Parameters

| Parameter | Config key | Default |
|-----------|------------|---------|
| GC interval | `mpimvcc.gc_interval_ms` | 500 |
| Version chain pre-alloc | `mpimvcc.initial_chain_capacity` | 8 |

---

## 11. Module 6 — BF-CSI Index (`bfcsi`)

### 11.1 Conceptual Design

Three-layer architecture:

```
Probe key k
    │
    ▼
[Layer 1] Bloom Filter B
    │
    ├── ABSENT (definite miss) ──→ return {} immediately   [fast path]
    │
    └── PRESENT (possible hit)
            │
            ▼
        [Layer 2] False-Positive Cache F (direct-mapped)
            │
            ├── key in F ──→ return {}  [known FP from recent traversal]
            │
            └── not in F
                    │
                    ▼
                [Layer 3] Concurrent Skip-list L
                    │
                    ├── found  ──→ return TIDs
                    │
                    └── not found ──→ insert k into F; return {}
```

### 11.2 Bloom Filter Sub-module (`util/bloom_filter.hpp`)

```
class BloomFilter
  Constructor: BloomFilter(size_t num_keys, double fpr)
    Computes:
      m = ceil(-num_keys * ln(fpr) / (ln(2)^2))  // bit count
      h = round((m / num_keys) * ln(2))           // hash function count
    Allocates: std::vector<uint8_t> bits_(ceil(m/8), 0)

  void   insert(Key k)
    FOR i in [0, h):
      bit_index = murmur3_seed(k, seed_i) % m
      SET bits_[bit_index / 8] |= (1 << (bit_index % 8))

  bool   possibly_present(Key k) const
    FOR i in [0, h):
      bit_index = murmur3_seed(k, seed_i) % m
      IF NOT SET bits_[bit_index / 8] bit: RETURN false
    RETURN true

  void   rebuild(const std::vector<Key>& live_keys)
    bits_.assign(bits_.size(), 0)
    FOR each k in live_keys: insert(k)

  size_t bit_count() const     // m
  size_t hash_count() const    // h
  size_t memory_bytes() const  // ceil(m/8)

  // Per-partition sharding:
  //   The BFCSIIndex holds P BloomFilter shards.
  //   Partition index = k % P
  //   Each shard has its own std::shared_mutex
```

Seed generation: `seed_i = 0xDEADBEEF + i * 0x9E3779B9` for i in [0, h).

### 11.3 Concurrent Skip-list Sub-module (`util/skiplist.hpp`)

```
MAX_LEVEL = 16
LEVEL_PROBABILITY = 0.5

struct SkipListNode {
    Key                                        key
    std::mutex                                 node_mutex
    std::vector<TID>                           tids       // guarded by node_mutex
    std::array<std::atomic<SkipListNode*>, MAX_LEVEL>  forward
    int                                        level
}

class ConcurrentSkipList
  // Sentinel nodes
  SkipListNode* head_  // key = INT64_MIN, level = MAX_LEVEL
  SkipListNode* tail_  // key = INT64_MAX, level = MAX_LEVEL
  std::atomic<int>   current_level_
  std::atomic<size_t> size_

  Methods:
    vector<TID> lookup(Key k)
    void insert(Key k, TID t)
    void remove(Key k, TID t)
    vector<Key> all_keys() const  // for BF rebuild; non-concurrent use only
    size_t size() const

  Internal:
    int random_level()
      // Standard geometric distribution with p=0.5
      // while (rand() < 0.5 && level < MAX_LEVEL): level++

    SkipListNode* find_predecessors(Key k, SkipListNode* preds[], SkipListNode* succs[])
      // Fills preds[i] = predecessor at level i
      //        succs[i] = successor at level i
      // Uses optimistic traversal; no locks during traversal
```

**Skip-list locking strategy:**
- Traversal is lock-free (reads `forward` atomically).
- `insert`: after finding preds/succs, lock preds[0]..preds[h] bottom-up, validate pointers didn't change, link new node.
- `remove`: lock node being deleted + its predecessor at level 0; unlink; unlock.
- This is the classic "hand-over-hand" approach adapted for skip-lists.

### 11.4 False-Positive Cache (`F`)

```
FPCache:
  static constexpr size_t CAPACITY = 4096  // power of 2
  std::array<Key, CAPACITY>  slots_         // initialised to NULL_KEY
  // Direct-mapped: slot = k & (CAPACITY - 1)
  // Insert: slots_[k & (CAPACITY-1)] = k
  // Query: return slots_[k & (CAPACITY-1)] == k
  // No locking needed: stale reads are safe (worst case: miss a cache hit)
```

### 11.5 BF-CSI Index Data Structure

```
BFCSIIndex:
  static constexpr int NUM_SHARDS = 16

  // Sharded Bloom Filter
  std::array<BloomFilter, NUM_SHARDS>     bf_shards_
  std::array<std::shared_mutex, NUM_SHARDS> bf_shard_mutexes_
  std::atomic<uint64_t>                   bf_delete_count_  // tracks deletions since last rebuild
  std::atomic<uint64_t>                   bf_total_keys_

  // Skip-list
  ConcurrentSkipList                      skiplist_

  // FP cache (lock-free, lossy)
  FPCache                                 fp_cache_

  // BF rebuild
  double                                  rebuild_threshold_  // ψ, default 0.20
  std::mutex                              rebuild_mutex_
  std::atomic<bool>                       rebuild_in_progress_

  // Metrics
  std::atomic<uint64_t>                   probe_count_
  std::atomic<uint64_t>                   bf_fast_path_
  std::atomic<uint64_t>                   bf_false_positive_
  std::atomic<uint64_t>                   bf_rebuilds_
  std::atomic<uint64_t>                   write_count_
```

### 11.6 Algorithm — BF-CSI Probe

```
INPUT:  key k
OUTPUT: vector<TID>

1. probe_count_ += 1
2. shard_id = k & (NUM_SHARDS - 1)   // assuming NUM_SHARDS is power of 2

3. ACQUIRE shared_lock(bf_shard_mutexes_[shard_id])
4. present = bf_shards_[shard_id].possibly_present(k)
   RELEASE shared_lock

5. IF NOT present:
     bf_fast_path_ += 1
     RETURN {}   // definite miss

6. IF fp_cache_.query(k):
     // Known false positive — skip skip-list traversal
     RETURN {}

7. tids = skiplist_.lookup(k)

8. IF tids.empty():
     bf_false_positive_ += 1
     fp_cache_.insert(k)
     RETURN {}

9. RETURN tids
```

### 11.7 Algorithm — BF-CSI Write (INSERT)

```
INPUT: key k, tid t

1. shard_id = k & (NUM_SHARDS - 1)
2. ACQUIRE unique_lock(bf_shard_mutexes_[shard_id])
   bf_shards_[shard_id].insert(k)
   RELEASE unique_lock

3. skiplist_.insert(k, t)
4. bf_total_keys_.fetch_add(1, memory_order_relaxed)
5. write_count_ += 1
```

### 11.8 Algorithm — BF-CSI Write (DELETE)

```
INPUT: key k, tid t

1. skiplist_.remove(k, t)
2. write_count_ += 1

3. IF skiplist_.lookup(k).empty():
     // key fully removed — mark as tombstoned
     // BF cannot delete; increment delete counter
     bf_delete_count_.fetch_add(1, memory_order_relaxed)

4. IF (bf_delete_count_ / bf_total_keys_) >= rebuild_threshold_:
     IF NOT rebuild_in_progress_.exchange(true):
         // Launch background rebuild thread
         std::thread([this]{ this->rebuild_bloom_filter(); }).detach()
```

### 11.9 Algorithm — BF Rebuild

```
FUNCTION rebuild_bloom_filter():
  LOCK rebuild_mutex_

  live_keys = skiplist_.all_keys()   // snapshot of all live keys

  // Rebuild each shard
  FOR shard_id in [0, NUM_SHARDS):
    ACQUIRE unique_lock(bf_shard_mutexes_[shard_id])
    bf_shards_[shard_id].rebuild(filter_by_shard(live_keys, shard_id))
    RELEASE unique_lock

  bf_delete_count_.store(0, memory_order_release)
  bf_total_keys_.store(live_keys.size(), memory_order_release)
  bf_rebuilds_ += 1
  rebuild_in_progress_.store(false, memory_order_release)
  UNLOCK rebuild_mutex_
```

During rebuild, stale BF state is used; the FP cache absorbs false positives.

### 11.10 Configuration Parameters

| Parameter | Config key | Default |
|-----------|------------|---------|
| Target FPR (ε) | `bfcsi.fpr` | 0.01 |
| Rebuild threshold (ψ) | `bfcsi.rebuild_threshold` | 0.20 |
| Num BF shards | `bfcsi.num_shards` | 16 |
| FP cache capacity | `bfcsi.fp_cache_capacity` | 4096 |
| Skip-list max level | `bfcsi.skiplist_max_level` | 16 |

---

## 12. Module 7 — Join Executor (`join`)

### 12.1 Purpose

The JoinExecutor spawns the configured number of **probe threads**, feeds them tuples from relation S, and collects results. It operates on a `BaseIndex*` and is protocol-agnostic.

### 12.2 Header: `include/caidj/join/join_executor.hpp`

```
namespace caidj::join

class JoinExecutor
  Constructor:
    JoinExecutor(BaseIndex* index, int num_probe_threads, const Relation& S)

  JoinStats execute()
    // Partitions S into num_probe_threads chunks
    // Spawns threads; each thread iterates its chunk and calls index->probe(t.key)
    // Collects total join result count and timing
    // Returns JoinStats

struct JoinStats {
    uint64_t total_probes       // = |S|
    uint64_t total_matches      // sum of all non-empty probe results
    uint64_t total_result_pairs // sum of |probe_result| across all probes
    double   wall_time_ms       // total elapsed ms (parallel)
    double   throughput_jps     // total_probes / (wall_time_ms / 1000)
    IndexStats index_stats      // snapshot from index->get_stats() after join
}
```

### 12.3 Algorithm — execute()

```
1. split S into num_probe_threads roughly-equal contiguous chunks
2. start_time = high_resolution_clock::now()

3. FOR each chunk (in parallel, using std::thread):
     FOR each tuple t in chunk:
         result = index->probe(t.key)
         local_matches += !result.empty()
         local_result_pairs += result.size()
         local_probes += 1
   // Join all threads

4. wall_time_ms = duration_cast(now - start_time)
5. Accumulate local counters into JoinStats
6. stats.index_stats = index->get_stats()
7. RETURN stats
```

---

## 13. Module 8 — Transaction Manager (`txn`)

### 13.1 Purpose

Simulates concurrent write transactions that continuously insert and delete tuples while the join executes. The TransactionManager runs `c` writer threads for the duration of one benchmark trial.

### 13.2 Header: `include/caidj/txn/txn_manager.hpp`

```
namespace caidj::txn

class TransactionManager
  Constructor:
    TransactionManager(BaseIndex* index, DataGen* gen, int num_write_threads,
                       uint64_t duration_ms, double insert_fraction = 0.8)
    // insert_fraction: fraction of ops that are INSERTs (rest are DELETEs)

  void start()   // spawns num_write_threads; returns immediately
  void stop()    // sets stop flag; joins all threads
  TxnStats get_stats() const

struct TxnStats {
    uint64_t total_writes
    uint64_t total_inserts
    uint64_t total_deletes
    double   write_rate_per_sec
}
```

### 13.3 Algorithm — Writer Thread

```
THREAD writer_thread_i:
  WHILE NOT stop_flag_ AND elapsed < duration_ms:
     op = (rand() < insert_fraction) ? INSERT : DELETE
     write_op = gen->generate_write_op(op)
     IF op == INSERT:
         index->insert(write_op.key, write_op.tid)
         total_inserts_[i] += 1
     ELSE:
         index->remove(write_op.key, write_op.tid)
         total_deletes_[i] += 1
```

---

## 14. Module 9 — Benchmark Harness (`bench`)

### 14.1 Purpose

The Benchmark class orchestrates a full experimental trial: generate data, build index, start writers, run join, stop writers, collect and report metrics.

### 14.2 Header: `include/caidj/bench/benchmark.hpp`

```
namespace caidj::bench

class Benchmark
  Constructor: Benchmark(const Config& cfg)

  BenchmarkResult run_single(Protocol p, int concurrency)
    // Runs one trial for protocol p at given writer concurrency

  std::vector<BenchmarkResult> run_all()
    // Iterates all (protocol x concurrency) pairs from cfg

  void save_results(const std::vector<BenchmarkResult>& results,
                    const std::string& output_path) const
    // Writes JSON + CSV

struct BenchmarkResult {
    Protocol    protocol
    int         concurrency          // number of writer threads
    int         run_id               // 1..num_runs
    double      join_latency_ms
    double      throughput_jps
    double      lock_contention_rate // probe_blocked / probe_count
    double      memory_overhead_mb
    double      build_time_ms
    IndexStats  index_stats
    TxnStats    txn_stats
    JoinStats   join_stats
}
```

### 14.3 Algorithm — run_single

```
FUNCTION run_single(p, concurrency):

1. gen = DataGen(cfg.seed, cfg.zipf_alpha, cfg.domain_size)
2. R = gen.generate_R(cfg.r_size)
3. S = gen.generate_S(cfg.s_size)

4. index = make_index(p, cfg)

5. t_build_start = now()
6. index->bulk_load(R)
7. build_time_ms = elapsed(t_build_start)

8. txn_mgr = TransactionManager(index.get(), &gen, concurrency,
                                 cfg.trial_duration_ms)
9. executor = JoinExecutor(index.get(), cfg.num_probe_threads, S)

10. txn_mgr.start()          // writers begin
11. join_stats = executor.execute()
12. txn_mgr.stop()           // writers halt

13. result.join_latency_ms       = join_stats.wall_time_ms
14. result.throughput_jps        = join_stats.throughput_jps
15. result.lock_contention_rate  = join_stats.index_stats.probe_blocked /
                                   max(1, join_stats.index_stats.probe_count)
16. result.memory_overhead_mb    = estimate_memory_mb(index.get(), p, cfg)
17. result.build_time_ms         = build_time_ms
18. RETURN result
```

### 14.4 Averaging Over Runs

`run_all()` runs each `(protocol, concurrency)` pair `cfg.num_runs` times and averages numeric fields of `BenchmarkResult`. Standard deviation is also computed and included in JSON output.

---

## 15. Module 10 — Metrics Collector (`metrics`)

### 15.1 Purpose

Provides a unified `MetricsCollector` singleton that aggregates per-thread counters without requiring all code to pass a metrics handle.

### 15.2 Data

```
class MetricsCollector (singleton via instance())
  std::unordered_map<std::string, std::atomic<uint64_t>>  counters_
  std::unordered_map<std::string, std::atomic<double>>     gauges_
  std::shared_mutex                                         map_mutex_

  void increment(const string& name, uint64_t by = 1)
  void gauge_set(const string& name, double value)
  uint64_t get_counter(const string& name) const
  double   get_gauge(const string& name) const
  void     reset_all()
  nlohmann::json to_json() const
```

Counters used across modules:

| Counter Name | Set by |
|--------------|--------|
| `probe_total` | BaseIndex::probe |
| `probe_blocked` | NHJ, ECHI on contention |
| `write_total` | BaseIndex::insert/remove |
| `epoch_transitions` | ECHIIndex |
| `gc_runs` | MPIMVCCIndex GC thread |
| `bf_fast_path` | BFCSIIndex probe |
| `bf_false_positive` | BFCSIIndex probe |
| `bf_rebuilds` | BFCSIIndex rebuild |

---

## 16. Module 11 — CLI Driver (`main`)

### 16.1 File: `src/main.cpp`

The main function parses CLI arguments, builds a `Config`, instantiates `Benchmark`, runs it, and writes results.

### 16.2 Argument Parsing

Use hand-rolled argument parsing (no external lib dependency). Format:

```
caidj [OPTIONS]

OPTIONS:
  --config <path>           Path to TOML config file (default: configs/default.toml)
  --protocol <name>         Run only this protocol: nhj|echi|mpimvcc|bfcsi|all
                            (default: all)
  --concurrency <c>         Run only at this writer count (default: run all from config)
  --r-size <n>              Override R relation size
  --s-size <n>              Override S relation size
  --seed <n>                Override random seed
  --output <path>           Output directory (default: results/)
  --runs <n>                Number of repetitions per (protocol, concurrency) pair
  --duration <ms>           Duration of one trial in ms (default: 30000)
  --probe-threads <n>       Number of join probe threads (default: 10)
  --generate-only           Only generate and save data; do not run benchmark
  --no-csv                  Skip CSV output
  --log-level <level>       trace|debug|info|warn|error (default: info)
  --help                    Print this message and exit
  --version                 Print version string and exit
```

### 16.3 main() Control Flow

```
main(argc, argv):
1. parse_args(argc, argv) → Config cfg
2. setup_logging(cfg.log_level)
3. LOG_INFO("CAIDJ v1.0.0 starting")
4. IF cfg.generate_only:
     DataGen gen(cfg.seed, cfg.zipf_alpha, cfg.domain_size)
     DataGen::save_csv(gen.generate_R(cfg.r_size), "data/relation_R.csv")
     DataGen::save_csv(gen.generate_S(cfg.s_size), "data/relation_S.csv")
     RETURN 0

5. Benchmark bench(cfg)
6. results = bench.run_all()
7. bench.save_results(results, cfg.output_dir)
8. print_summary_table(results)   // human-readable to stdout
9. RETURN 0
```

---

## 17. Module 12 — Configuration System (`config`)

### 17.1 Config Struct: `include/caidj/config.hpp`

```cpp
struct Config {
    // Data generation
    uint64_t  seed          = 42;
    double    zipf_alpha    = 1.2;
    int64_t   domain_size   = 200'000;
    int64_t   r_size        = 150'000;
    int64_t   s_size        = 50'000;

    // Benchmark control
    int       num_runs              = 3;
    uint64_t  trial_duration_ms     = 30'000;
    int       num_probe_threads     = 10;
    std::vector<int>  concurrency_levels = {1, 2, 4, 8, 16};
    std::vector<Protocol> protocols      = {Protocol::NHJ, Protocol::ECHI,
                                            Protocol::MPI_MVCC, Protocol::BF_CSI};

    // Output
    std::string output_dir  = "results/";
    bool        write_csv   = true;
    bool        write_json  = true;
    std::string log_level   = "info";

    // ECHI
    size_t   echi_delta_threshold  = 1000;
    uint64_t echi_epoch_interval_ms = 100;

    // MPI-MVCC
    uint64_t mpimvcc_gc_interval_ms = 500;

    // BF-CSI
    double   bfcsi_fpr               = 0.01;
    double   bfcsi_rebuild_threshold = 0.20;
    int      bfcsi_num_shards        = 16;
    size_t   bfcsi_fp_cache_capacity = 4096;

    // TXN
    double   insert_fraction = 0.80;  // 80% inserts, 20% deletes

    static Config from_toml(const std::string& path);
    static Config from_args(int argc, char** argv);
    void validate() const;  // throws std::invalid_argument on bad values
};
```

### 17.2 TOML Config File Format (`configs/default.toml`)

```toml
[data]
seed        = 42
zipf_alpha  = 1.2
domain_size = 200000
r_size      = 150000
s_size      = 50000

[bench]
num_runs           = 3
trial_duration_ms  = 30000
num_probe_threads  = 10
concurrency_levels = [1, 2, 4, 8, 16]
protocols          = ["nhj", "echi", "mpimvcc", "bfcsi"]

[output]
dir        = "results/"
write_csv  = true
write_json = true
log_level  = "info"

[echi]
delta_threshold   = 1000
epoch_interval_ms = 100

[mpimvcc]
gc_interval_ms = 500

[bfcsi]
fpr               = 0.01
rebuild_threshold = 0.20
num_shards        = 16
fp_cache_capacity = 4096

[txn]
insert_fraction = 0.80
```

---

## 18. Module 13 — Unit Tests (`tests`)

### 18.1 Framework

Google Test (fetched via CMake FetchContent). Each `.cpp` is compiled into a single `caidj_tests` binary.

### 18.2 Test Inventory

#### `test_bloom_filter.cpp`
| Test Name | Description |
|-----------|-------------|
| `BloomFilter_NeverFalseNegative` | Insert 10,000 keys; query all → must all return `true` |
| `BloomFilter_FPRWithinTolerance` | Insert N keys; query 100,000 non-inserted keys; FPR ≤ 2ε |
| `BloomFilter_RebuildRestoresFPR` | After deletion simulation, rebuild; FPR returns to ≤ 2ε |
| `BloomFilter_ThreadSafe_Concurrent` | 8 threads inserting 1,000 keys each; all keys queryable after |

#### `test_skiplist.cpp`
| Test Name | Description |
|-----------|-------------|
| `SkipList_InsertAndLookup` | Insert 1,000 (key, tid) pairs; lookup all; verify correctness |
| `SkipList_Delete` | Insert then delete; verify absent |
| `SkipList_DuplicateKeys` | Same key, multiple TIDs; lookup returns all |
| `SkipList_ConcurrentInserts` | 8 threads, 10,000 inserts each; final size == 80,000 |
| `SkipList_ConcurrentMixed` | 4 inserts + 4 deletes threads; no crash; consistent state |

#### `test_nhj.cpp`
| Test Name | Description |
|-----------|-------------|
| `NHJ_BulkLoad` | Load 1,000 tuples; probe all keys; all found |
| `NHJ_Insert` | Add new tuple; probe finds it |
| `NHJ_Delete` | Delete tuple; probe no longer finds it |
| `NHJ_ContentionDetected` | With concurrent writers, `probe_blocked > 0` |

#### `test_echi.cpp`
| Test Name | Description |
|-----------|-------------|
| `ECHI_BulkLoad` | Load + probe; correct results |
| `ECHI_EpochIsolation` | Write during epoch; probe in same epoch must NOT see write |
| `ECHI_WriteVisible_NextEpoch` | Write; force epoch transition; probe sees write |
| `ECHI_DeltaThresholdTrigger` | Insert δ_threshold writes; verify epoch counter increments |
| `ECHI_TimerTrigger` | Wait epoch_interval_ms * 2; verify epoch counter increments |
| `ECHI_ZeroContention_NoBlocking` | Probes never see `probe_blocked > 0` during quiet period |

#### `test_mpimvcc.cpp`
| Test Name | Description |
|-----------|-------------|
| `MPIMVCC_BulkLoad` | Load + probe; correct results |
| `MPIMVCC_SnapshotIsolation` | Write at ts=T; probe with ts<T must NOT see write |
| `MPIMVCC_SnapshotIsolation_Visible` | Write at ts=T; probe with ts>T sees write |
| `MPIMVCC_GCPrunesOldVersions` | After GC run, chains don't grow unboundedly |
| `MPIMVCC_ZeroProbeContention` | `probe_blocked == 0` under concurrent writes |

#### `test_bfcsi.cpp`
| Test Name | Description |
|-----------|-------------|
| `BFCSI_BulkLoad` | Load + probe; correct results |
| `BFCSI_FastPath` | Query key never inserted; BF says absent; `bf_fast_path > 0` |
| `BFCSI_FPCacheAbsorbsStale` | Probe a known FP; `bf_false_positive` increments once, not twice |
| `BFCSI_RebuildTriggered` | Delete > ψ fraction; verify `bf_rebuilds == 1` |
| `BFCSI_ConcurrentProbeWrite` | No crash or data race under 8 concurrent writers + 8 probers |

#### `test_datagen.cpp`
| Test Name | Description |
|-----------|-------------|
| `DataGen_Size` | `generate_R(n)` returns exactly n tuples |
| `DataGen_ZipfRange` | All keys in [1, domain_size] |
| `DataGen_Deterministic` | Same seed → same dataset |
| `DataGen_CSV_RoundTrip` | save_csv + load_csv → identical data |

#### `test_join_executor.cpp`
| Test Name | Description |
|-----------|-------------|
| `JoinExecutor_CorrectResultCount` | Known R and S; verify result count |
| `JoinExecutor_MultiThread` | Same result with 1, 2, 4, 8 probe threads |

#### `test_metrics.cpp`
| Test Name | Description |
|-----------|-------------|
| `Metrics_IncrementAtomic` | 100 threads increment; final value correct |
| `Metrics_Reset` | After reset, all counters == 0 |

### 18.3 Sanitizer Tests

Add a CMake preset `SanitizedTest` that builds with:
```
-fsanitize=thread -g -O1
```
Run `caidj_tests` under TSAN to catch data races. This is separate from ASAN/UBSAN Debug builds.

---

## 19. End-to-End Data Flow

```
┌─────────────────────────────────────────────────────────────────────────┐
│                          main()                                         │
│  parse_args() ──► Config                                                │
│       │                                                                 │
│       ▼                                                                 │
│  Benchmark::run_all()                                                   │
│       │                                                                 │
│       │  FOR each (protocol, concurrency):                              │
│       │                                                                 │
│       ▼                                                                 │
│  ┌──────────────────────────────────────────────────────────────────┐   │
│  │  run_single(protocol, concurrency)                               │   │
│  │                                                                  │   │
│  │  DataGen ──► generate_R() ──► Relation R (150K tuples)          │   │
│  │  DataGen ──► generate_S() ──► Relation S ( 50K tuples)          │   │
│  │                                                                  │   │
│  │  make_index(protocol) ──► BaseIndex*                             │   │
│  │  index->bulk_load(R)   [single-threaded setup]                   │   │
│  │                                                                  │   │
│  │  TransactionManager (c writer threads)  ──► index->insert()     │   │
│  │                                        ──► index->remove()      │   │
│  │          │ runs concurrently ▼                                   │   │
│  │  JoinExecutor  (10 probe threads)       ──► index->probe()      │   │
│  │          │ collects ▼                                            │   │
│  │  JoinStats { latency, throughput, matches }                      │   │
│  │                                                                  │   │
│  │  TransactionManager::stop()                                      │   │
│  │                                                                  │   │
│  │  BenchmarkResult { all metrics }                                 │   │
│  └──────────────────────────────────────────────────────────────────┘   │
│       │                                                                 │
│       ▼                                                                 │
│  save_results() ──► results/results.json                                │
│                 ──► results/results.csv                                 │
│  print_summary_table() ──► stdout                                       │
└─────────────────────────────────────────────────────────────────────────┘
```

---

## 20. Algorithm Pseudocode (C++ Level)

This section gives C++-level pseudocode (not actual code) that bridges the high-level design and actual implementation.

### 20.1 Epoch Timer Thread (ECHI)

```cpp
void ECHIIndex::epoch_timer_fn() {
    while (!stop_flag_.load(std::memory_order_acquire)) {
        std::this_thread::sleep_for(
            std::chrono::milliseconds(epoch_interval_ms_));
        trigger_epoch_transition();
    }
}
```

### 20.2 Atomic Shared Pointer Swap (ECHI)

The frozen map swap must be atomic so that probe threads either see the old or the new map, never a torn pointer:

```cpp
// Write side (within trigger_epoch_transition, after building next_map_):
std::atomic_store_explicit(&current_map_, next_map_,
                            std::memory_order_release);

// Read side (in probe):
auto snap = std::atomic_load_explicit(&current_map_,
                                       std::memory_order_acquire);
```

`std::shared_ptr` atomic operations are lock-based on most platforms but correct regardless.

### 20.3 MurmurHash3 Wrapper for BloomFilter

```cpp
uint64_t bloom_hash(Key k, uint32_t seed) {
    uint64_t out[2];
    MurmurHash3_x64_128(&k, sizeof(Key), seed, out);
    return out[0];
}

void BloomFilter::insert(Key k) {
    for (uint32_t i = 0; i < h_; ++i) {
        size_t idx = bloom_hash(k, base_seed_ + i) % m_;
        bits_[idx >> 3] |= (1u << (idx & 7));
    }
}
```

### 20.4 Skip-list Node Insertion (CAS skeleton)

```
// After finding preds[] and succs[]:
new_node = allocate SkipListNode(key, level)
new_node->forward[0].store(succs[0])

// Lock preds[0..level-1] in deterministic order
for (int i = 0; i <= level; ++i) lock preds[i]->node_mutex

// Validate: preds still point to succs (no concurrent modification)
for (int i = 0; i <= level; ++i):
    if preds[i]->forward[i].load() != succs[i]: RETRY

// Link new_node
for (int i = 0; i <= level; ++i):
    new_node->forward[i].store(succs[i])
    preds[i]->forward[i].store(new_node)

// Unlock in reverse order
for (int i = level; i >= 0; --i) unlock preds[i]->node_mutex
```

### 20.5 Memory Usage Estimation

```cpp
size_t estimate_memory_bytes(BaseIndex* idx, Protocol p, const Config& cfg) {
    // NHJ: sizeof(unordered_map entry) * r_size  ≈ 48 * r_size bytes
    // ECHI: 2 * map_size + delta_buffer (bounded by delta_threshold)
    // MPI-MVCC: r_size * avg_chain_length * sizeof(VersionNode)
    //   sizeof(VersionNode) ≈ 64 + avg_tids_per_key * 8
    // BF-CSI: bloom_filter_bytes + skiplist_bytes + fp_cache_bytes
    //   bloom_filter_bytes = ceil(-r_size * ln(fpr) / (ln2^2)) / 8
    //   skiplist_bytes ≈ r_size * MAX_LEVEL * 8
    //   fp_cache_bytes = fp_cache_capacity * 8
    return idx->get_stats().memory_bytes;
}
```

Each index's `get_stats()` must compute and return a `memory_bytes` estimate using the formulae above.

---

## 21. CLI Reference

### 21.1 Quick Examples

```bash
# Run full benchmark with default config
./caidj

# Run only MPI-MVCC at concurrency 8
./caidj --protocol mpimvcc --concurrency 8

# Run with custom config and 5 repetitions
./caidj --config configs/high_concurrency.toml --runs 5

# Just generate the dataset (no benchmark)
./caidj --generate-only --r-size 500000 --s-size 100000 --seed 1337

# Benchmark all protocols, shorter trial, verbose logging
./caidj --duration 10000 --log-level debug --output /tmp/caidj_results/

# Run only NHJ and ECHI at concurrency 1, 4, 16
./caidj --protocol nhj --concurrency 1
./caidj --protocol echi --concurrency 1
# (run twice more with --concurrency 4 and --concurrency 16)
```

### 21.2 Expected stdout (summary table)

```
╔══════════════════════════════════════════════════════════════════════════╗
║                    CAIDJ Benchmark Results                              ║
╠══════════╦═════════════╦══════════════╦═══════════╦══════════════════════╣
║ Protocol ║ Concurrency ║ Latency (ms) ║ TPS       ║ Contention (%)       ║
╠══════════╬═════════════╬══════════════╬═══════════╬══════════════════════╣
║ NHJ      ║     1       ║     1243     ║   805.1   ║       2.1            ║
║ NHJ      ║     2       ║     1891     ║   529.1   ║       8.4            ║
...
║ MPI-MVCC ║    16       ║     3421     ║   292.0   ║       5.8            ║
╚══════════╩═════════════╩══════════════╩═══════════╩══════════════════════╝
Results saved to results/results.json and results/results.csv
```

---

## 22. Input / Output File Formats

### 22.1 Relation CSV (`data/relation_R.csv`, `data/relation_S.csv`)

```
tid,key,val1,val2
0,4271,18234,9912
1,813,44012,3301
...
```

- `tid`: `int64_t`, unique within relation
- `key`: `int64_t`, Zipf-distributed in [1, domain_size]
- `val1`, `val2`: `int64_t`, random (payload, not used in join)

### 22.2 JSON Results (`results/results.json`)

```json
{
  "version": "1.0.0",
  "config": { /* full Config struct */ },
  "results": [
    {
      "protocol": "MPI-MVCC",
      "concurrency": 8,
      "run_id": 1,
      "join_latency_ms": 2134.7,
      "throughput_jps": 469.0,
      "lock_contention_rate": 0.034,
      "memory_overhead_mb": 128.4,
      "build_time_ms": 1923.0,
      "index_stats": {
        "probe_count": 50000,
        "probe_blocked": 1700,
        "write_count": 41200,
        "epoch_transitions": 0,
        "gc_runs": 4,
        "bf_fast_path": 0,
        "bf_false_positive": 0,
        "bf_rebuilds": 0,
        "version_chain_avg": 2.3,
        "memory_bytes": 134611000
      },
      "txn_stats": {
        "total_writes": 41200,
        "total_inserts": 32960,
        "total_deletes": 8240,
        "write_rate_per_sec": 1373.3
      }
    }
    /* ... more entries ... */
  ],
  "aggregated": [
    {
      "protocol": "MPI-MVCC",
      "concurrency": 8,
      "latency_mean_ms": 2145.2,
      "latency_stddev_ms": 18.4,
      "throughput_mean_jps": 466.1,
      "contention_mean": 0.034
    }
    /* ... */
  ]
}
```

### 22.3 CSV Results (`results/results.csv`)

```
protocol,concurrency,run_id,join_latency_ms,throughput_jps,lock_contention_rate,memory_overhead_mb,build_time_ms,probe_count,probe_blocked,write_count,gc_runs,bf_fast_path,bf_false_positive,bf_rebuilds
NHJ,1,1,1243.1,805.1,0.021,0.0,1201.3,50000,1050,0,0,0,0,0
...
```

### 22.4 Log File (`logs/caidj.log`)

```
[14:23:01.432] [info] [main] CAIDJ v1.0.0 starting
[14:23:01.433] [info] [bench] Generating R (150000 tuples) and S (50000 tuples)
[14:23:02.118] [info] [bench] Running protocol=MPI-MVCC concurrency=8 run=1/3
[14:23:32.892] [info] [bench] Trial complete: latency=2134.7ms throughput=469.0 jps
[14:23:32.893] [debug] [mpimvcc] GC ran 4 times; freed 18324 version nodes
...
```

---

## 23. Threading Model & Synchronisation Primitives

### 23.1 Thread Inventory

| Thread | Count | Owner | Lifetime |
|--------|-------|-------|----------|
| Main thread | 1 | `main()` | Process lifetime |
| Probe threads | `cfg.num_probe_threads` (default 10) | `JoinExecutor` | Per trial |
| Writer threads | `c` (variable) | `TransactionManager` | Per trial |
| Epoch timer thread | 1 | `ECHIIndex` | Index lifetime |
| GC thread | 1 | `MPIMVCCIndex` | Index lifetime |
| BF rebuild thread | 0 or 1 | `BFCSIIndex` | Detached, short-lived |
| Test threads | variable | GoogleTest | Per test |

### 23.2 Primitive Usage Matrix

| Primitive | Used in | Purpose |
|-----------|---------|---------|
| `std::shared_mutex` + `std::shared_lock` | NHJ, MPI-MVCC chains map | RW-lock pattern |
| `std::mutex` + `std::unique_lock` | ECHI delta, MPI-MVCC per-key, skip-list nodes | Exclusive section |
| `std::atomic<uint64_t>` | All indexes (counters) | Lock-free counting |
| `std::atomic<TxnID>` | MPI-MVCC ts_delete | Lock-free version expiry |
| `std::atomic<int64_t>` | ECHI active_readers_ | Reader epoch tracking |
| `std::atomic<bool>` | All stop flags, BF rebuild flag | Control flags |
| `std::atomic<std::shared_ptr<T>>` | ECHI current_map_ | Atomic shared_ptr swap |
| `std::condition_variable` | ECHI transition (wait for readers_done) | Epoch drain |
| `std::thread` | Writers, probe threads, GC, epoch timer | OS thread creation |

### 23.3 Memory Order Quick Reference

| Location | Order Used | Reason |
|----------|-----------|--------|
| `active_readers_.fetch_add` | `acquire` | Establishes happens-before with map read |
| `active_readers_.fetch_sub` | `release` | Publishes decrement to transition thread |
| `current_map_` atomic store | `release` | New map visible to future probe acquires |
| `current_map_` atomic load | `acquire` | See store before reading map |
| Counter increments | `relaxed` | No ordering needed for statistics |
| `stop_flag_` loads | `acquire` | See preceding stores to stopped state |
| `stop_flag_` stores | `release` | Publish stop state to all threads |

---

## 24. Memory Layout & Sizing Guidelines

### 24.1 Per-Protocol Memory Budget at Default Config

| Protocol | Component | Size Formula | At r_size=150K |
|----------|-----------|-------------|----------------|
| NHJ | Hash map entries | `48 * r_size` bytes | ~7.2 MB |
| ECHI | Frozen map × 2 + delta | `2 * 48 * r_size + 40 * delta_threshold` | ~14.4 MB + 40 KB |
| MPI-MVCC | Version chains | `(48 + 8*avg_tids) * r_size * avg_chain_len` | ~128 MB (est.) |
| BF-CSI | Bloom filter bits | `ceil(-r * ln(ε) / ln(2)^2 / 8)` | ~24 MB at ε=0.01 |
| BF-CSI | Skip-list nodes | `(sizeof(SkipListNode)) * r_size` | ~6.4 MB |
| BF-CSI | FP cache | `8 * fp_cache_capacity` | ~32 KB |

`sizeof(SkipListNode)` ≈ `8 (key) + 8 (mutex) + 24 (vector) + 16*8 (forward array) + 4 (level) + padding` = ~180 bytes. At 150K keys this is ~27 MB.

### 24.2 Version Chain Growth Model (MPI-MVCC)

At `W` writes/sec sustained for `T` seconds with GC interval `G_ms`:
- Max nodes per key (hot key): `W * G_ms / 1000 / distinct_keys`
- GC reclaims after each GC run; steady-state chain length ≈ `W * G_ms / 1000 / distinct_keys`
- At W=2000, G_ms=500, distinct_keys=200000: ≈ `2000 * 0.5 / 200000 = 0.005` extra nodes per key per GC cycle — negligible. Hot keys (Zipf top 1%) get ~100× more traffic, so ≈ 0.5 nodes per GC cycle per hot key.

---

## 25. Error Handling Strategy

### 25.1 Error Taxonomy

| Error Class | Examples | Handling |
|-------------|----------|---------|
| Configuration error | Invalid TOML, bad numeric range | `throw std::invalid_argument` from `Config::validate()`; caught in `main()`, printed, exit(1) |
| Resource exhaustion | `std::bad_alloc` | Propagate; top-level `catch(std::exception&)` in `main()` |
| Logic error (bug) | Null pointer dereference, violated invariant | `assert()` in debug; UBSAN in CI |
| Data generation | CSV file not writable | Return `bool` success; log error; exit(1) |
| Index operation | Key not found (normal) | Return empty vector; NOT an error |

### 25.2 `CaidjError` Type

```cpp
enum class CaidjError {
    OK = 0,
    CONFIG_INVALID,
    FILE_NOT_FOUND,
    FILE_WRITE_ERROR,
    THREAD_SPAWN_ERROR,
};

struct CaidjResult {
    CaidjError code;
    std::string message;
    bool ok() const { return code == CaidjError::OK; }
};
```

Factory functions (`make_index`, `DataGen::load_csv`) return `CaidjResult` + out-parameter for the created object.

---

## 26. Logging

### 26.1 Logger Setup (`util/logger.hpp`)

```cpp
namespace caidj::util {

void init_logger(const std::string& log_level,
                 const std::string& log_file = "logs/caidj.log");

// Named loggers by module
spdlog::logger* get_logger(const std::string& name);

} // namespace caidj::util

// Convenience macros (include logger.hpp to use):
#define LOG_TRACE(logger, ...)  SPDLOG_LOGGER_TRACE(logger, __VA_ARGS__)
#define LOG_DEBUG(logger, ...)  SPDLOG_LOGGER_DEBUG(logger, __VA_ARGS__)
#define LOG_INFO(logger, ...)   SPDLOG_LOGGER_INFO(logger, __VA_ARGS__)
#define LOG_WARN(logger, ...)   SPDLOG_LOGGER_WARN(logger, __VA_ARGS__)
#define LOG_ERROR(logger, ...)  SPDLOG_LOGGER_ERROR(logger, __VA_ARGS__)
```

### 26.2 Logger Names by Module

| Module | Logger name |
|--------|-------------|
| main | `"main"` |
| Benchmark | `"bench"` |
| DataGen | `"datagen"` |
| ECHIIndex | `"echi"` |
| MPIMVCCIndex | `"mpimvcc"` |
| BFCSIIndex | `"bfcsi"` |
| NHJIndex | `"nhj"` |
| JoinExecutor | `"join"` |
| TransactionManager | `"txn"` |

### 26.3 Key Log Events

```
[info]  [bench]    Starting run: protocol=ECHI concurrency=4 run=2/3
[debug] [echi]     Epoch transition: epoch=7 delta_size=1000
[debug] [mpimvcc]  GC run: freed=842 nodes safe_ts=10234
[debug] [bfcsi]    BF rebuild #3 triggered: delete_ratio=0.214
[info]  [bench]    Trial done: latency=1876ms throughput=533jps contention=11.3%
[warn]  [bfcsi]    Observed FPR=2.31% exceeds target 2*eps=2.0%
```

---

## 27. Build, Run, and Test Commands

### 27.1 Prerequisites

```bash
# Ubuntu / Debian
sudo apt-get install -y \
    cmake ninja-build \
    gcc-13 g++-13 \
    libatomic1

# Arch Linux
sudo pacman -S cmake ninja gcc

# macOS (Homebrew)
brew install cmake ninja gcc@13
```

All C++ dependencies (GoogleTest, nlohmann/json, toml11, spdlog) are fetched automatically by CMake via `FetchContent`. No manual installation required.

### 27.2 Build Commands

```bash
# Clone and enter repo
git clone <repo_url> caidj
cd caidj

# Configure (Release build — recommended for benchmarking)
cmake -B build/release -S . \
      -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_CXX_COMPILER=g++-13 \
      -G Ninja

# Build
cmake --build build/release -j$(nproc)

# Configure (Debug + AddressSanitizer — for development)
cmake -B build/debug -S . \
      -DCMAKE_BUILD_TYPE=Debug \
      -DCMAKE_CXX_COMPILER=g++-13 \
      -G Ninja

cmake --build build/debug -j$(nproc)
```

### 27.3 Run Commands

```bash
# --- Full benchmark (all protocols, all concurrency levels) ---
./build/release/caidj

# --- Specific protocol ---
./build/release/caidj --protocol mpimvcc

# --- Single concurrency level ---
./build/release/caidj --protocol echi --concurrency 4

# --- Custom config ---
./build/release/caidj --config configs/high_concurrency.toml

# --- Generate dataset only ---
./build/release/caidj --generate-only

# --- Short smoke test (5s trial, 1 run) ---
./build/release/caidj --duration 5000 --runs 1

# --- Debug run with verbose logging ---
./build/debug/caidj --duration 5000 --runs 1 --log-level debug
```

### 27.4 Test Commands

```bash
# Build tests
cmake --build build/debug --target caidj_tests -j$(nproc)

# Run all tests
./build/debug/caidj_tests

# Run specific test suite
./build/debug/caidj_tests --gtest_filter="ECHI*"

# Run with thread sanitizer
cmake -B build/tsan -S . \
      -DCMAKE_BUILD_TYPE=Debug \
      -DCMAKE_CXX_FLAGS="-fsanitize=thread -g -O1" \
      -G Ninja
cmake --build build/tsan --target caidj_tests -j$(nproc)
./build/tsan/caidj_tests

# Run all tests and show XML report
./build/debug/caidj_tests --gtest_output=xml:test_results.xml
```

### 27.5 Result Visualisation (Optional Python Script)

```bash
# Requires: pip install matplotlib pandas
python3 scripts/plot_results.py results/results.json

# Produces:
#   results/latency_vs_concurrency.png
#   results/throughput_vs_concurrency.png
#   results/contention_vs_concurrency.png
#   results/memory_overhead.png
```

### 27.6 CI Check Script (`scripts/run_all.sh`)

```bash
#!/usr/bin/env bash
set -euo pipefail

BINARY="./build/release/caidj"

for PROTOCOL in nhj echi mpimvcc bfcsi; do
  for CONC in 1 2 4 8 16; do
    echo "=== $PROTOCOL concurrency=$CONC ==="
    $BINARY --protocol "$PROTOCOL" \
            --concurrency "$CONC" \
            --duration 30000 \
            --runs 3 \
            --output "results/${PROTOCOL}_c${CONC}/"
  done
done

echo "All runs complete."
```

---

## 28. Known Design Constraints & Trade-offs

### 28.1 Python GIL vs C++ True Parallelism

The original Python simulator suffered from the GIL, which prevented true CPU parallelism. The C++ implementation uses `std::thread` with no equivalent constraint. This means:
- Absolute latency numbers will differ from the paper's Python results.
- Relative rankings between protocols should be preserved (and likely more pronounced).
- The TSAN build is important to catch races that Python's GIL was masking.

### 28.2 `std::shared_ptr` Atomic Operations

`std::atomic_store` / `std::atomic_load` on `std::shared_ptr` uses an internal spinlock on most compilers before C++20. C++20 introduces `std::atomic<std::shared_ptr<T>>` as a first-class atomic. The implementation should use `std::atomic<std::shared_ptr<T>>` if the compiler supports it (GCC 12+, Clang 14+); otherwise fall back to `std::atomic_store_explicit` + `std::atomic_load_explicit`.

### 28.3 Per-Key Mutexes in MPI-MVCC

Storing `std::unordered_map<Key, std::mutex>` is problematic because `std::mutex` is not copyable/movable. Use `std::unordered_map<Key, std::unique_ptr<std::mutex>>` instead. The map itself is protected by `key_mutexes_map_mutex_` for insertions of new keys; once a key's mutex is inserted, it is never removed (key mutexes are not garbage collected).

### 28.4 Skip-list Memory Reclamation

The lock-based skip-list uses `std::unique_ptr<SkipListNode>` for ownership; when a node is unlinked, its destructor runs when no thread holds a reference. Because we use locks (not CAS-based lock-free), there is no ABA problem and no need for hazard pointers or epoch-based reclamation.

### 28.5 Zipfian Sampler Accuracy

The rejection-inversion sampler is accurate for `α > 1`. For `α ≤ 1` (which includes the uniform case at α=0), use the alias method or Walker's method. The design only needs `α = 1.2` (per the paper), so the rejection-inversion sampler suffices.

### 28.6 BF-CSI Shard Count Must Be Power of 2

The shard selection formula `k & (NUM_SHARDS - 1)` requires `NUM_SHARDS` to be a power of 2. `Config::validate()` must enforce this.

---

## 29. Glossary

| Term | Definition |
|------|------------|
| **BaseIndex** | Abstract C++ class that all four index implementations inherit from |
| **BF-CSI** | Bloom Filter Augmented Concurrent Skip-list Index; one of the three CAIDJ protocols |
| **bulk_load** | Single-threaded initial population of the index from relation R before concurrent operations begin |
| **CAS** | Compare-And-Swap; an atomic hardware instruction used for lock-free synchronisation |
| **ECHI** | Epoch-Based Concurrent Hash Index; one of the three CAIDJ protocols |
| **Epoch** | A discrete time interval during which the frozen index hash map is immutable |
| **FPR** | False Positive Rate of a Bloom filter |
| **GC** | Garbage Collection; in MPI-MVCC, the background process that prunes obsolete version nodes |
| **Hot key** | A key that receives disproportionately many accesses under Zipfian distribution |
| **MPI-MVCC** | Multi-Version Partition Index with MVCC; one of the three CAIDJ protocols |
| **MVCC** | Multi-Version Concurrency Control; readers access a committed snapshot; writers create new versions |
| **NHJ** | Naive Distributed Hash Join; the baseline using a single global reader-writer lock |
| **Probe** | A read operation on the index: look up all TIDs matching a given key |
| **Probe thread** | A thread in JoinExecutor that iterates over S tuples and calls `index->probe()` |
| **Relation** | A vector of Tuple structs (R = LINEITEM-like, S = ORDERS-like) |
| **Skip-list** | A probabilistic data structure supporting O(log n) search, insert, delete |
| **TID** | Tuple Identifier; a unique `int64_t` row ID within a relation |
| **Trial** | One complete run of: bulk_load + start_writers + run_join + stop_writers |
| **Version chain** | In MPI-MVCC, the linked list of historical committed versions for one key |
| **Writer thread** | A thread in TransactionManager that continuously calls `index->insert()` or `index->remove()` |
| **Zipfian distribution** | A power-law probability distribution parameterized by α; models realistic key skew |
