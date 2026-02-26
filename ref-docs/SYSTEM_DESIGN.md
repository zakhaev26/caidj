    # System Design and Architecture

    ## Overview

    This document describes the system architecture and methodology of the Concurrency-Aware Indexing framework for distributed joins. The implementation is based on the research presented in the technical paper, which addresses critical performance bottlenecks in modern database systems: lock contention, data skew, and limited scalability under concurrent workloads.

    The framework implements a multi-layered approach combining lock-free indexing, skew-resistant partitioning, and hardware-assisted execution to achieve scalable distributed join operations.

    ---

    ## 1. Overall System Architecture

    The system is organized into several interconnected components, each addressing a specific challenge in distributed join processing.

    ```
    ┌─────────────────────────────────────────────────────────────────────┐
    │                        Benchmark Runner                             │
    │  (Throughput, Latency, Scalability, Skew Sensitivity Tests)        │
    └─────────────────────────────────────────────────────────────────────┘
                                    │
            ┌───────────────────────┼───────────────────────┐
            │                       │                       │
            ▼                       ▼                       ▼
    ┌───────────────┐   ┌───────────────────┐   ┌───────────────────┐
    │ Baseline      │   │ Lock-Free Indexes │   │ Skew-Aware Join   │
    │ Locked Index  │   │                   │   │                   │
    │               │   │ ┌─────┐ ┌───────┐ │   │ ┌───────────────┐ │
    │ • Mutex-based │   │ │ ART │ │BW-Tree│ │   │ │ Hot Key       │ │
    │ • Simple      │   │ └─────┘ └───────┘ │   │ │ Detection      │ │
    │ • Baseline    │   │                   │   │ │ Partitioning   │ │
    └───────────────┘   └───────────────────┘   │ │ Parallel Join  │ │
                                                └───────────────┘
                                    │
                                    ▼
                        ┌───────────────────────┐
                        │ Epoch Reclamation    │
                        │ (Memory Management)  │
                        └───────────────────────┘
    ```

    ### 1.1 Core Components

    #### 1.1.1 Index Layer

    The index layer provides multiple index implementations for comparison:

    1. **Baseline Locked Index** (`BaselineLockedIndex`): A simple mutex-protected index serving as the baseline for performance comparison. Uses standard mutex locks to ensure thread safety at the cost of serialization.

    2. **Adaptive Radix Tree (ART)** (`ARTIndex`): A lock-free index implementation based on the Adaptive Radix Tree design. Uses multi-level node types (Node4, Node16, Node48, Node256) that grow dynamically based on occupancy. Implements version-based locking and CAS operations for concurrent access.

    3. **Bw-Tree** (`BwTree`): A delta-chain based latch-free index inspired by the BW-Tree design. Uses a mapping table to decouple logical page IDs from physical memory addresses, enabling lock-free updates through atomic CAS operations.

    #### 1.1.2 Memory Management

    **Epoch-Based Reclamation** (`EpochReclamation`): A lock-free memory reclamation scheme that safely frees memory without requiring reference counting or locks. The system maintains a global epoch counter and tracks retired objects per thread. Memory is only reclaimed when all threads have advanced past the epoch in which the object was retired.

    #### 1.1.3 Join Execution Layer

    **Skew-Aware Join Executor** (`SkewAwareJoinExecutor`): Implements a join strategy designed to handle data skew in distributed environments:

    - **Hot Key Detection**: Samples the input data to identify frequently occurring keys (hot keys)
    - **Partition-Based Parallelism**: Partitions both build and probe relations into fixed number of partitions
    - **Skew-Resilient Execution**: Processes hot keys with special handling to prevent single-partition bottlenecks

    ---

    ## 2. Methodology and Implementation Details

    ### 2.1 Lock-Free Index Design

    #### 2.1.1 Adaptive Radix Tree (ART)

    The ART implementation uses a tiered node structure that adapts to the number of keys stored:

    | Node Type | Maximum Keys | Use Case |
    |-----------|-------------|----------|
    | Node4     | 4           | Sparse occupancy |
    | Node16    | 16          | Moderate occupancy |
    | Node48    | 48          | High occupancy |
    | Node256   | 256         | Dense occupancy |

    **Key Implementation Details:**

    - **Version-Based Locking**: Each node maintains an atomic version number. Before modification, a thread acquires a lock by setting the high bit of the version. This optimistic approach allows concurrent reads without blocking.

    - **Lock-Free Traversal**: Reads use atomic loads with acquire semantics, ensuring visibility of recently updated values without holding locks.

    - **Node Growth**: When a node reaches capacity, it automatically grows to the next larger node type using atomic CAS operations to update the parent pointer.

    ```cpp
    // Version-based locking pattern
    uint64_t lockVersion(uint64_t v) {
        return v | WRITE_LOCK_BIT;  // Set high bit as lock
    }

    bool tryLock(Node* node) {
        uint64_t expected = node->version.load(std::memory_order_acquire);
        if (isLocked(expected)) return false;
        return node->version.compare_exchange_strong(expected, lockVersion(expected));
    }
    ```

    #### 2.1.2 BW-Tree Implementation

    The BW-Tree design focuses on avoiding locks through:

    1. **Delta Chains**: Instead of modifying nodes in-place, changes are recorded as small delta records appended to the node. Each delta contains the modification type and necessary data.

    2. **Mapping Table**: A global hash map translates logical page IDs to physical node pointers. This indirection allows atomic updates to node locations without modifying parent pointers.

    3. **Automatic Consolidation**: When delta chains grow too long (configurable threshold), the system consolidates all deltas into a new base node, resetting the chain.

    ```cpp
    // Delta record structure
    struct DeltaRecord {
        DeltaType type;           // INSERT, DELETE, UPDATE, SPLIT
        uint64_t logical_page_id;
        DeltaRecord* next;        // Chain pointer
        uint64_t version;
        unsigned char data[];     // Type-specific data
    };
    ```

    ### 2.2 Epoch-Based Memory Reclamation

    The Epoch Reclamation system enables safe memory cleanup in lock-free data structures:

    **Mechanism:**
    1. Each thread maintains a local epoch value matching the global epoch
    2. When an object is retired, it's added to the thread's retired list
    3. Periodically, threads check if they can advance the global epoch
    4. Objects from epochs older than the current global epoch can be safely freed

    **Implementation:**
    - Global epoch advances when no active readers and no pending operations
    - Each thread's TLS (Thread-Local Storage) maintains:
    - Current local epoch
    - Retired object count
    - Vector of retired pointers

    ### 2.3 Skew-Aware Join Execution

    The skew-aware join addresses the challenge of hot keys in distributed joins:

    #### 2.3.1 Hot Key Detection

    The system samples input data to identify frequently occurring keys:

    ```cpp
    void sampleAndDetectHotKeys(const std::vector<KeyValuePair>& data) {
        // Sample a subset of keys
        std::unordered_map<Key, uint64_t> sample_counts;
        for (size_t i = 0; i < data.size(); i += sample_step) {
            sample_counts[data[i].key]++;
        }
        
        // Identify hot keys based on frequency threshold
        for (const auto& [key, count] : sample_counts) {
            if (count >= threshold) {
                hot_keys_[key] = HotKeyInfo(key, count, partition_id, true);
            }
        }
    }
    ```

    #### 2.3.2 Partition-Based Parallelism

    The relations are partitioned into a fixed number of partitions (64 by default), with each thread processing a subset:

    - Keys are hashed to determine partition assignment
    - Hot keys are assigned to dedicated partitions
    - Threads process partitions in a round-robin fashion

    #### 2.3.3 Join Execution

    The join follows a probe-first strategy:

    1. For each probe tuple, check if it's a hot key
    2. Hot keys use partition-local lookup (already partitioned build relation)
    3. Non-hot keys use global index lookup

    ### 2.4 Benchmark Framework

    The benchmark system measures performance across multiple dimensions:

    | Metric | Description |
    |--------|-------------|
    | **Throughput** | Operations per second (ops/sec) |
    | **Average Latency** | Mean time per operation (nanoseconds) |
    | **P50/P95/P99 Latency** | Percentile latencies |
    | **Scalability Score** | Speedup ratio with increasing threads |
    | **Contention Events** | Count of skew-related conflicts |

    **Configuration Parameters:**
    - `num_threads`: Number of concurrent threads (1-16)
    - `dataset_size`: Number of key-value pairs (default: 1M)
    - `skew_factor`: Zipf distribution theta (0.0 = uniform, 1.0 = extreme skew)
    - `warmup_iterations`: Iterations before measurement
    - `measurement_iterations`: Iterations for averaging results

    ---

    ## 3. Data Structures

    ### 3.1 Core Types (`types.hpp`)

    ```cpp
    using Key = uint64_t;
    using Value = uint64_t;

    struct KeyValuePair {
        Key key;
        Value value;
    };

    struct JoinResult {
        Key key;
        Value left_value;
        Value right_value;
    };
    ```

    ### 3.2 Statistics Structures

    ```cpp
    struct PartitionStats {
        MovableAtomic tuple_count;        // Tuples in partition
        MovableAtomic hot_key_count;     // Hot keys in partition
        MovableAtomic contention_events;  // Conflicts detected
    };
    ```

    ---

    ## 4. Thread Synchronization

    ### 4.1 Lock-Free Patterns Used

    1. **Compare-And-Swap (CAS)**: Primary synchronization primitive for all lock-free operations
    2. **Memory Ordering**: Explicit memory ordering (acquire/release/seq_cst) for correctness
    3. **Epoch Guards**: RAII-style guards that automatically manage read epochs

    ```cpp
    class EpochGuard {
    public:
        EpochGuard() {
            EpochReclamation::getInstance().beginRead();
        }
        ~EpochGuard() {
            EpochReclamation::getInstance().endRead();
        }
    };
    ```

    ### 4.2 Concurrency Control

    - **Index Writes**: Version-based optimistic locking with retry on conflict
    - **Index Reads**: Lock-free using atomic loads with appropriate memory ordering
    - **Memory Access**: Cache-line aligned structures to prevent false sharing

    ---

    ## 5. Performance Characteristics

    ### 5.1 Observed Results

    Based on benchmark execution:

    | Index Type | Throughput | Latency | Status |
    |------------|------------|---------|--------|
    | Baseline Locked | ~27M ops/sec | ~700ns | Stable |
    | ART (small data) | ~18M ops/sec | ~50Kns | Partial |
    | Skew-Aware Join | Low | High | Needs work |

    ### 5.2 Design Trade-offs

    1. **Latency vs Throughput**: Lock-free designs favor high throughput under contention at the cost of slightly higher latency for individual operations

    2. **Memory vs Performance**: Delta chains in BW-Tree use extra memory but enable lock-free updates

    3. **Skew Handling vs Overhead**: Hot key detection adds sampling overhead but prevents partition imbalance
