#ifndef BW_TREE_HPP
#define BW_TREE_HPP

#include <atomic>
#include <unordered_map>
#include <vector>
#include <cstring>
#include "types.hpp"
#include "epoch_reclamation.hpp"

constexpr uint64_t DELTA_CHAIN_THRESHOLD = 8;

enum class DeltaType : uint8_t {
    INSERT = 1,
    DELETE = 2,
    UPDATE = 3,
    SPLIT = 4,
    CONSOLIDATE = 5
};

struct DeltaRecord {
    DeltaType type;
    uint64_t logical_page_id;
    DeltaRecord* next;
    uint64_t version;
    alignas(16) unsigned char data[48];
    
    template<typename T>
    T* dataAs() { return reinterpret_cast<T*>(data); }
    
    template<typename T>
    const T* dataAs() const { return reinterpret_cast<const T*>(data); }
};

struct DeltaInsert : public DeltaRecord {
    Key key;
    Value value;
    
    DeltaInsert(Key k, Value v) {
        type = DeltaType::INSERT;
        key = k;
        value = v;
    }
};

struct DeltaDelete : public DeltaRecord {
    Key key;
    
    DeltaDelete(Key k) {
        type = DeltaType::DELETE;
        key = k;
    }
};

struct DeltaUpdate : public DeltaRecord {
    Key key;
    Value new_value;
    
    DeltaUpdate(Key k, Value v) {
        type = DeltaType::UPDATE;
        key = k;
        new_value = v;
    }
};

struct alignas(64) BwTreeNode {
    static constexpr uint32_t MAX_KEYS = 64;
    
    uint64_t logical_id;
    uint32_t level;
    uint32_t size;
    bool is_leaf;
    std::atomic<uint64_t> version;
    std::atomic<DeltaRecord*> delta_head;
    std::atomic<BwTreeNode*> next_split;
    
    alignas(64) Key keys[MAX_KEYS];
    alignas(64) Value values[MAX_KEYS];
    
    BwTreeNode(uint64_t id, uint32_t lvl = 0, bool leaf = true) 
        : logical_id(id), level(lvl), size(0), is_leaf(leaf), 
          version(0), delta_head(nullptr), next_split(nullptr) {}
    
    void addKey(Key k, Value v) {
        if (size >= MAX_KEYS) return;
        
        uint32_t idx = 0;
        while (idx < size && keys[idx] < k) idx++;
        
        for (uint32_t i = size; i > idx; i--) {
            keys[i] = keys[i-1];
            values[i] = values[i-1];
        }
        keys[idx] = k;
        values[idx] = v;
        size++;
    }
    
    bool findKey(Key k, Value& v) const {
        for (uint32_t i = 0; i < size; i++) {
            if (keys[i] == k) {
                v = values[i];
                return true;
            }
        }
        return false;
    }
    
    bool removeKey(Key k) {
        for (uint32_t i = 0; i < size; i++) {
            if (keys[i] == k) {
                for (uint32_t j = i; j < size - 1; j++) {
                    keys[j] = keys[j+1];
                    values[j] = values[j+1];
                }
                size--;
                return true;
            }
        }
        return false;
    }
};

class BwTree {
private:
    static constexpr uint64_t INVALID_PAGE_ID = 0;
    static constexpr uint64_t ROOT_PAGE_ID = 1;
    static constexpr uint64_t PAGE_ID_INCREMENT = 1;
    
    struct MappingTableEntry {
        BwTreeNode* node_ptr;
        bool locked;
        
        MappingTableEntry() : node_ptr(nullptr), locked(false) {}
        MappingTableEntry(BwTreeNode* ptr, bool l) : node_ptr(ptr), locked(l) {}
    };
    
    alignas(64) std::unordered_map<uint64_t, MappingTableEntry> mapping_table_;
    std::atomic<uint64_t> next_page_id_{ROOT_PAGE_ID + 1};
    alignas(64) std::atomic<BwTreeNode*> root_{nullptr};
    std::atomic<uint64_t> size_{0};
    
    BwTreeNode* allocateNode(uint64_t id, uint32_t level = 0, bool leaf = true) {
        auto* node = new BwTreeNode(id, level, leaf);
        mapping_table_.emplace(id, MappingTableEntry{node, false});
        return node;
    }
    
    BwTreeNode* getNode(uint64_t id) {
        auto it = mapping_table_.find(id);
        if (it == mapping_table_.end()) return nullptr;
        return it->second.node_ptr;
    }
    
    bool lockNode(uint64_t id) {
        auto it = mapping_table_.find(id);
        if (it == mapping_table_.end()) return false;
        
        if (it->second.locked) return false;
        it->second.locked = true;
        return true;
    }
    
    void unlockNode(uint64_t id) {
        auto it = mapping_table_.find(id);
        if (it != mapping_table_.end()) {
            it->second.locked = false;
        }
    }
    
    DeltaRecord* applyDeltas(BwTreeNode* node) {
        DeltaRecord* delta = node->delta_head.load(std::memory_order_acquire);
        
        while (delta) {
            switch (delta->type) {
                case DeltaType::INSERT: {
                    auto* ins = static_cast<DeltaInsert*>(delta);
                    node->addKey(ins->key, ins->value);
                    break;
                }
                case DeltaType::DELETE: {
                    auto* del = static_cast<DeltaDelete*>(delta);
                    node->removeKey(del->key);
                    break;
                }
                case DeltaType::UPDATE: {
                    auto* upd = static_cast<DeltaUpdate*>(delta);
                    for (uint32_t i = 0; i < node->size; i++) {
                        if (node->keys[i] == upd->key) {
                            node->values[i] = upd->new_value;
                            break;
                        }
                    }
                    break;
                }
                default:
                    break;
            }
            delta = delta->next;
        }
        return delta;
    }
    
    uint32_t countDeltaChain(BwTreeNode* node) {
        uint32_t count = 0;
        DeltaRecord* delta = node->delta_head.load(std::memory_order_acquire);
        while (delta) {
            count++;
            delta = delta->next;
        }
        return count;
    }
    
    void consolidateNode(BwTreeNode* node) {
        if (!node || !node->is_leaf) return;
        
        DeltaRecord* delta = node->delta_head.load(std::memory_order_acquire);
        if (!delta) return;
        
        BwTreeNode* new_node = new BwTreeNode(node->logical_id, node->level, node->is_leaf);
        
        DeltaRecord* d = delta;
        while (d) {
            if (d->type == DeltaType::INSERT) {
                auto* ins = static_cast<DeltaInsert*>(d);
                new_node->addKey(ins->key, ins->value);
            } else if (d->type == DeltaType::DELETE) {
                auto* del = static_cast<DeltaDelete*>(d);
                new_node->removeKey(del->key);
            } else if (d->type == DeltaType::UPDATE) {
                auto* upd = static_cast<DeltaUpdate*>(d);
                for (uint32_t i = 0; i < new_node->size; i++) {
                    if (new_node->keys[i] == upd->key) {
                        new_node->values[i] = upd->new_value;
                        break;
                    }
                }
            }
            d = d->next;
        }
        
        new_node->delta_head.store(nullptr, std::memory_order_release);
        
        auto it = mapping_table_.find(node->logical_id);
        if (it != mapping_table_.end()) {
            it->second.node_ptr = new_node;
        } else {
            mapping_table_.emplace(node->logical_id, MappingTableEntry{new_node, false});
        }
        
        EpochReclamation::getInstance().retire(node);
    }
    
    bool addDelta(BwTreeNode* node, DeltaRecord* delta) {
        DeltaRecord* expected = node->delta_head.load(std::memory_order_acquire);
        
        do {
            delta->next = expected;
        } while (!node->delta_head.compare_exchange_weak(expected, delta,
                std::memory_order_release, std::memory_order_acquire));
        
        if (countDeltaChain(node) >= DELTA_CHAIN_THRESHOLD) {
            consolidateNode(node);
        }
        
        return true;
    }
    
    BwTreeNode* findLeaf(Key key) {
        BwTreeNode* node = root_.load(std::memory_order_acquire);
        
        while (node && !node->is_leaf) {
            uint32_t idx = 0;
            while (idx < node->size && key > node->keys[idx]) idx++;
            
            if (idx < node->size) {
                node = getNode(reinterpret_cast<uint64_t>(node->values[idx]));
            } else if (node->next_split.load(std::memory_order_acquire)) {
                node = node->next_split.load(std::memory_order_acquire);
            } else {
                break;
            }
        }
        
        return node;
    }
    
    void installRoot(BwTreeNode* new_root) {
        BwTreeNode* expected = nullptr;
        while (!root_.compare_exchange_weak(expected, new_root,
                std::memory_order_release, std::memory_order_relaxed)) {
            expected = nullptr;
        }
    }

public:
    BwTree() {
        BwTreeNode* root = allocateNode(ROOT_PAGE_ID, 0, true);
        root_.store(root, std::memory_order_release);
    }
    
    ~BwTree() {
        for (auto& [id, entry] : mapping_table_) {
            BwTreeNode* node = entry.node_ptr;
            if (node) {
                DeltaRecord* delta = node->delta_head.load(std::memory_order_acquire);
                while (delta) {
                    DeltaRecord* next = delta->next;
                    delete delta;
                    delta = next;
                }
                delete node;
            }
        }
    }
    
    IndexOpResult insert(Key key, Value value) {
        EpochGuard guard;
        
        BwTreeNode* leaf = findLeaf(key);
        if (!leaf) return IndexOpResult::RETRY_NEEDED;
        
        Value existing;
        if (leaf->findKey(key, existing)) {
            return IndexOpResult::EXISTS;
        }
        
        auto* delta = new DeltaInsert(key, value);
        if (!addDelta(leaf, delta)) {
            delete delta;
            return IndexOpResult::RETRY_NEEDED;
        }
        
        size_.fetch_add(1, std::memory_order_relaxed);
        return IndexOpResult::SUCCESS;
    }
    
    std::optional<Value> find(Key key) {
        EpochGuard guard;
        
        BwTreeNode* leaf = findLeaf(key);
        if (!leaf) return std::nullopt;
        
        DeltaRecord* delta = leaf->delta_head.load(std::memory_order_acquire);
        
        while (delta) {
            if (delta->type == DeltaType::INSERT && 
                static_cast<DeltaInsert*>(delta)->key == key) {
                return static_cast<DeltaInsert*>(delta)->value;
            } else if (delta->type == DeltaType::DELETE &&
                       static_cast<DeltaDelete*>(delta)->key == key) {
                return std::nullopt;
            } else if (delta->type == DeltaType::UPDATE) {
                auto* upd = static_cast<DeltaUpdate*>(delta);
                if (upd->key == key) {
                    return upd->new_value;
                }
            }
            delta = delta->next;
        }
        
        Value val;
        if (leaf->findKey(key, val)) {
            return val;
        }
        
        return std::nullopt;
    }
    
    IndexOpResult upsert(Key key, Value value) {
        EpochGuard guard;
        
        BwTreeNode* leaf = findLeaf(key);
        if (!leaf) return IndexOpResult::RETRY_NEEDED;
        
        auto* delta = new DeltaUpdate(key, value);
        if (!addDelta(leaf, delta)) {
            delete delta;
            return IndexOpResult::RETRY_NEEDED;
        }
        
        return IndexOpResult::SUCCESS;
    }
    
    uint64_t size() const {
        return size_.load(std::memory_order_relaxed);
    }
    
    void clear() {
        root_.store(nullptr, std::memory_order_release);
        size_.store(0, std::memory_order_relaxed);
    }
};

#endif
