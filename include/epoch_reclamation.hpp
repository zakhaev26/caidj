#ifndef EPOCH_RECLAMATION_HPP
#define EPOCH_RECLAMATION_HPP

#include <atomic>
#include <vector>
#include <thread>
#include <array>
#include <algorithm>
#include <cassert>
#include "types.hpp"

class EpochReclamation {
public:
    static constexpr uint64_t EPOCH_MASK = 0x3;
    static constexpr uint64_t GLOBAL_EPOCH_INC = 0x4;
    static constexpr uint64_t MAX_PENDING = 1024;
    
private:
    struct alignas(CACHE_LINE_SIZE) ThreadLocal {
        std::atomic<uint64_t> epoch{0};
        std::atomic<uint64_t*> retired_list{nullptr};
        size_t retired_count{0};
        uint64_t local_epoch{0};
        std::vector<void*> retired_objects;
        
        ThreadLocal() {
            retired_objects.reserve(256);
        }
    };
    
    alignas(CACHE_LINE_SIZE) std::atomic<uint64_t> global_epoch_{0};
    alignas(CACHE_LINE_SIZE) std::atomic<uint64_t> thread_count_{0};
    alignas(CACHE_LINE_SIZE) std::atomic<uint64_t> active_readers_{0};
    
    static inline thread_local ThreadLocal tls_;
    static inline EpochReclamation* instance_;
    
    std::vector<std::thread> worker_threads_;
    bool running_{false};
    
public:
    static EpochReclamation& getInstance() {
        if (!instance_) {
            instance_ = new EpochReclamation();
        }
        return *instance_;
    }
    
    void initialize() {
        running_ = true;
    }
    
    void shutdown() {
        running_ = false;
        for (auto& t : worker_threads_) {
            if (t.joinable()) {
                t.join();
            }
        }
    }
    
    void beginRead() {
        tls_.local_epoch = global_epoch_.load(std::memory_order_relaxed) & ~EPOCH_MASK;
        std::atomic_fetch_add_explicit(&active_readers_, 1ULL, std::memory_order_acq_rel);
    }
    
    void endRead() {
        std::atomic_fetch_sub_explicit(&active_readers_, 1ULL, std::memory_order_acq_rel);
        
        uint64_t global = global_epoch_.load(std::memory_order_relaxed);
        if ((global & ~EPOCH_MASK) != (tls_.local_epoch & ~EPOCH_MASK)) {
            tls_.local_epoch = global & ~EPOCH_MASK;
            tryAdvanceEpoch(global);
        }
    }
    
    void retire(void* ptr) {
        if (!ptr) return;
        
        tls_.retired_objects.push_back(ptr);
        
        if (tls_.retired_objects.size() >= 64) {
            tryReclaim();
        }
    }
    
    template<typename T>
    void retire(T* ptr) {
        retire(static_cast<void*>(ptr));
    }
    
    void tryReclaim() {
        if (tls_.retired_objects.empty()) return;
        
        uint64_t global = global_epoch_.load(std::memory_order_relaxed);
        uint64_t epoch = global & ~EPOCH_MASK;
        
        if ((global & EPOCH_MASK) == 0 && 
            active_readers_.load(std::memory_order_acquire) == 0) {
            
            if (global_epoch_.compare_exchange_weak(global, 
                (global + GLOBAL_EPOCH_INC) & ~EPOCH_MASK,
                std::memory_order_release, std::memory_order_relaxed)) {
                
                for (auto obj : tls_.retired_objects) {
                    void* ptr = obj;
                    if (ptr) {
                        ::operator delete(ptr);
                    }
                }
                tls_.retired_objects.clear();
            }
        }
    }
    
private:
    void tryAdvanceEpoch(uint64_t global) {
        if ((global & EPOCH_MASK) == 0 && 
            active_readers_.load(std::memory_order_acquire) == 0) {
            
            uint64_t new_epoch = (global + GLOBAL_EPOCH_INC) & ~EPOCH_MASK;
            global_epoch_.compare_exchange_weak(global, new_epoch,
                std::memory_order_release, std::memory_order_relaxed);
        }
    }
};

class EpochGuard {
public:
    EpochGuard() {
        EpochReclamation::getInstance().beginRead();
    }
    
    ~EpochGuard() {
        EpochReclamation::getInstance().endRead();
    }
    
    EpochGuard(const EpochGuard&) = delete;
    EpochGuard& operator=(const EpochGuard&) = delete;
};

#endif
