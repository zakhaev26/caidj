#ifndef CONCURRENT_INDEX_HPP
#define CONCURRENT_INDEX_HPP

#include <cstring>
#include <array>
#include <memory>
#include "types.hpp"
#include "epoch_reclamation.hpp"

class ARTIndex {
public:
    static constexpr uint8_t NODE4 = 1;
    static constexpr uint8_t NODE16 = 2;
    static constexpr uint8_t NODE48 = 3;
    static constexpr uint8_t NODE256 = 4;
    
    struct Node {
        uint8_t type;
        uint8_t prefix_len;
        uint8_t size;
        std::atomic<uint64_t> version;
        std::atomic<Node*> child;
        
        Node(uint8_t t, uint8_t p = 0) : type(t), prefix_len(p), size(0), version(0), child(nullptr) {}
        
        virtual ~Node() = default;
    };
    
    struct KeyValue {
        Key key;
        Value value;
        std::atomic<KeyValue*> next;
        
        KeyValue(Key k, Value v) : key(k), value(v), next(nullptr) {}
    };
    
    struct Node4 : public Node {
        static constexpr uint8_t MAX_KEYS = 4;
        alignas(CACHE_LINE_SIZE) std::array<std::atomic<KeyValue*>, MAX_KEYS> values;
        alignas(CACHE_LINE_SIZE) std::array<uint8_t, MAX_KEYS> keys;
        
        Node4() : Node(NODE4) {}
    };
    
    struct Node16 : public Node {
        static constexpr uint8_t MAX_KEYS = 16;
        alignas(CACHE_LINE_SIZE) std::array<std::atomic<KeyValue*>, MAX_KEYS> values;
        alignas(CACHE_LINE_SIZE) std::array<uint8_t, MAX_KEYS> keys;
        
        Node16() : Node(NODE16) {}
    };
    
    struct Node48 : public Node {
        static constexpr uint16_t MAX_KEYS = 256;
        alignas(CACHE_LINE_SIZE) std::array<int8_t, MAX_KEYS> child_index;
        alignas(CACHE_LINE_SIZE) std::array<std::atomic<KeyValue*>, 48> values;
        alignas(CACHE_LINE_SIZE) std::array<uint8_t, 48> keys;
        
        Node48() : Node(NODE48) {
            child_index.fill(-1);
        }
    };
    
    struct Node256 : public Node {
        static constexpr uint16_t MAX_KEYS = 256;
        alignas(CACHE_LINE_SIZE) std::array<std::atomic<KeyValue*>, MAX_KEYS> values;
        
        Node256() : Node(NODE256) {}
    };
    
private:
    alignas(CACHE_LINE_SIZE) std::atomic<Node*> root_;
    std::atomic<uint64_t> size_{0};
    
    static constexpr uint64_t WRITE_LOCK_BIT = (1ULL << 63);
    
    bool isLocked(uint64_t version) const {
        return (version & WRITE_LOCK_BIT) != 0;
    }
    
    uint64_t lockVersion(uint64_t v) {
        return v | WRITE_LOCK_BIT;
    }
    
    uint64_t unlockVersion(uint64_t v) {
        return v & ~WRITE_LOCK_BIT;
    }
    
    uint64_t incrementVersion(uint64_t v) {
        return ((v & WRITE_LOCK_BIT) | ((v + 1) & ~WRITE_LOCK_BIT));
    }
    
    uint8_t getKeyByte(Key key, uint8_t depth) const {
        return (key >> (56 - depth * 8)) & 0xFF;
    }
    
    KeyValue* loadValue(std::atomic<KeyValue*>& addr) {
        return addr.load(std::memory_order_acquire);
    }
    
    bool casValue(std::atomic<KeyValue*>& addr, KeyValue* expected, KeyValue* desired) {
        return addr.compare_exchange_strong(expected, desired, 
            std::memory_order_release, std::memory_order_acquire);
    }
    
    Node* loadChild(std::atomic<Node*>& addr) {
        return addr.load(std::memory_order_acquire);
    }
    
    bool casChild(std::atomic<Node*>& addr, Node* expected, Node* desired) {
        return addr.compare_exchange_strong(expected, desired,
            std::memory_order_release, std::memory_order_acquire);
    }
    
    int compareKeys(uint8_t a, uint8_t b) const {
        return (a < b) ? -1 : (a > b) ? 1 : 0;
    }
    
    int compareKeys(const uint8_t* a, size_t alen, const uint8_t* b, size_t blen) const {
        size_t min_len = std::min(alen, blen);
        for (size_t i = 0; i < min_len; ++i) {
            if (a[i] != b[i]) {
                return (a[i] < b[i]) ? -1 : 1;
            }
        }
        return (alen < blen) ? -1 : (alen > blen) ? 1 : 0;
    }
    
    bool isPrefixMatch(Node* node, Key key, uint8_t depth) const {
        if (node->prefix_len == 0) return true;
        
        uint8_t prefix[48];
        extractPrefix(node, prefix);
        
        for (uint8_t i = 0; i < node->prefix_len; ++i) {
            uint8_t expected = getKeyByte(key, depth + i);
            if (prefix[i] != expected) return false;
        }
        return true;
    }
    
    void extractPrefix(Node* node, uint8_t* out) const {
        // Prefix is stored in the node after the key array
        // For simplicity, we store it inline
        memset(out, 0, 48);
    }
    
    KeyValue* findValue(Node* node, Key key, uint8_t depth) {
        if (!node) return nullptr;
        
        EpochGuard guard;
        
        uint8_t key_byte = getKeyByte(key, depth);
        
        switch (node->type) {
            case NODE4: {
                auto* n = static_cast<Node4*>(node);
                for (uint8_t i = 0; i < n->size; ++i) {
                    if (n->keys[i] == key_byte) {
                        return loadValue(n->values[i]);
                    }
                }
                break;
            }
            case NODE16: {
                auto* n = static_cast<Node16*>(node);
                for (uint8_t i = 0; i < n->size; ++i) {
                    if (n->keys[i] == key_byte) {
                        return loadValue(n->values[i]);
                    }
                }
                break;
            }
            case NODE48: {
                auto* n = static_cast<Node48*>(node);
                int8_t idx = n->child_index[key_byte];
                if (idx >= 0) {
                    return loadValue(n->values[idx]);
                }
                break;
            }
            case NODE256: {
                auto* n = static_cast<Node256*>(node);
                return loadValue(n->values[key_byte]);
            }
        }
        
        return nullptr;
    }
    
    bool insertIntoNode4(Node4* node, uint8_t key_byte, KeyValue* value) {
        uint8_t idx = node->size;
        for (uint8_t i = 0; i < node->size; ++i) {
            if (node->keys[i] > key_byte) {
                idx = i;
                break;
            }
        }
        
        for (uint8_t i = node->size; i > idx; --i) {
            node->keys[i] = node->keys[i - 1];
            node->values[i] = node->values[i - 1].load();
        }
        
        node->keys[idx] = key_byte;
        node->values[idx] = value;
        node->size++;
        
        return true;
    }
    
    bool insertIntoNode16(Node16* node, uint8_t key_byte, KeyValue* value) {
        uint8_t idx = node->size;
        for (uint8_t i = 0; i < node->size; ++i) {
            if (node->keys[i] > key_byte) {
                idx = i;
                break;
            }
        }
        
        for (uint8_t i = node->size; i > idx; --i) {
            node->keys[i] = node->keys[i - 1];
            node->values[i] = node->values[i - 1].load();
        }
        
        node->keys[idx] = key_byte;
        node->values[idx] = value;
        node->size++;
        
        return true;
    }
    
    Node* growNode4To16(Node4* old_node) {
        auto* new_node = new Node16();
        new_node->prefix_len = old_node->prefix_len;
        new_node->size = old_node->size;
        
        for (uint8_t i = 0; i < old_node->size; ++i) {
            new_node->keys[i] = old_node->keys[i];
            new_node->values[i] = old_node->values[i].load();
        }
        
        EpochReclamation::getInstance().retire(old_node);
        return new_node;
    }
    
    Node* growNode16To48(Node16* old_node) {
        auto* new_node = new Node48();
        new_node->prefix_len = old_node->prefix_len;
        new_node->size = old_node->size;
        
        for (uint8_t i = 0; i < old_node->size; ++i) {
            new_node->keys[i] = old_node->keys[i];
            new_node->values[i] = old_node->values[i].load();
            new_node->child_index[old_node->keys[i]] = i;
        }
        
        EpochReclamation::getInstance().retire(old_node);
        return new_node;
    }
    
    Node* growNode48To256(Node48* old_node) {
        auto* new_node = new Node256();
        new_node->prefix_len = old_node->prefix_len;
        new_node->size = old_node->size;
        
        for (uint8_t i = 0; i < old_node->size; ++i) {
            new_node->values[old_node->keys[i]] = old_node->values[i].load();
        }
        
        EpochReclamation::getInstance().retire(old_node);
        return new_node;
    }
    
public:
    ARTIndex() {
        root_.store(nullptr, std::memory_order_relaxed);
    }
    
    ~ARTIndex() {
        // Cleanup would require traversing and deleting all nodes
        // For simplicity, we rely on epoch reclamation
    }
    
    IndexOpResult insert(Key key, Value value) {
        KeyValue* new_kv = new KeyValue(key, value);
        
        Node* old_root = loadChild(root_);
        
        if (!old_root) {
            auto* new_node = new Node4();
            new_node->keys[0] = getKeyByte(key, 0);
            new_node->values[0] = new_kv;
            new_node->size = 1;
            
            if (root_.compare_exchange_strong(old_root, new_node,
                std::memory_order_release, std::memory_order_relaxed)) {
                size_.fetch_add(1, std::memory_order_relaxed);
                return IndexOpResult::SUCCESS;
            }
            
            EpochReclamation::getInstance().retire(new_kv);
            EpochReclamation::getInstance().retire(new_node);
        }
        
        Node* node = old_root;
        uint8_t depth = 0;
        
        while (true) {
            if (!node) {
                return IndexOpResult::RETRY_NEEDED;
            }
            
            uint64_t version = node->version.load(std::memory_order_acquire);
            if (isLocked(version)) {
                continue;
            }
            
            uint8_t key_byte = getKeyByte(key, depth);
            
            switch (node->type) {
                case NODE4: {
                    auto* n = static_cast<Node4*>(node);
                    
                    for (uint8_t i = 0; i < n->size; ++i) {
                        if (n->keys[i] == key_byte) {
                            KeyValue* expected = loadValue(n->values[i]);
                            if (expected && expected->key == key) {
                                return IndexOpResult::EXISTS;
                            }
                            
                            if (casValue(n->values[i], expected, new_kv)) {
                                return IndexOpResult::SUCCESS;
                            }
                            continue;
                        }
                    }
                    
                    if (n->size < Node4::MAX_KEYS) {
                        uint64_t lock_v = lockVersion(version);
                        if (!node->version.compare_exchange_weak(version, lock_v,
                            std::memory_order_acquire, std::memory_order_relaxed)) {
                            continue;
                        }
                        
                        if (insertIntoNode4(n, key_byte, new_kv)) {
                            node->version.store(incrementVersion(lock_v), 
                                std::memory_order_release);
                            size_.fetch_add(1, std::memory_order_relaxed);
                            return IndexOpResult::SUCCESS;
                        }
                    } else {
                        uint64_t lock_v = lockVersion(version);
                        if (!node->version.compare_exchange_weak(version, lock_v,
                            std::memory_order_acquire, std::memory_order_relaxed)) {
                            continue;
                        }
                        
                        Node* new_node = growNode4To16(n);
                        node = new_node;
                        if (!casChild(root_, old_root, new_node)) {
                            // Another thread already replaced, continue with new
                        }
                        old_root = new_node;
                        continue;
                    }
                    break;
                }
                    
                case NODE16: {
                    auto* n = static_cast<Node16*>(node);
                    
                    for (uint8_t i = 0; i < n->size; ++i) {
                        if (n->keys[i] == key_byte) {
                            KeyValue* expected = loadValue(n->values[i]);
                            if (casValue(n->values[i], expected, new_kv)) {
                                return IndexOpResult::SUCCESS;
                            }
                            continue;
                        }
                    }
                    
                    if (n->size < Node16::MAX_KEYS) {
                        uint64_t lock_v = lockVersion(version);
                        if (!node->version.compare_exchange_weak(version, lock_v,
                            std::memory_order_acquire, std::memory_order_relaxed)) {
                            continue;
                        }
                        
                        if (insertIntoNode16(n, key_byte, new_kv)) {
                            node->version.store(incrementVersion(lock_v),
                                std::memory_order_release);
                            size_.fetch_add(1, std::memory_order_relaxed);
                            return IndexOpResult::SUCCESS;
                        }
                    } else {
                        uint64_t lock_v = lockVersion(version);
                        if (!node->version.compare_exchange_weak(version, lock_v,
                            std::memory_order_acquire, std::memory_order_relaxed)) {
                            continue;
                        }
                        
                        Node* new_node = growNode16To48(n);
                        node = new_node;
                        if (!casChild(root_, old_root, new_node)) {
                            old_root = new_node;
                        }
                        continue;
                    }
                    break;
                }
                    
                case NODE48: {
                    auto* n = static_cast<Node48*>(node);
                    int8_t idx = n->child_index[key_byte];
                    
                    if (idx >= 0) {
                        KeyValue* expected = loadValue(n->values[idx]);
                        if (casValue(n->values[idx], expected, new_kv)) {
                            return IndexOpResult::SUCCESS;
                        }
                        continue;
                    }
                    
                    if (n->size < 48) {
                        uint64_t lock_v = lockVersion(version);
                        if (!node->version.compare_exchange_weak(version, lock_v,
                            std::memory_order_acquire, std::memory_order_relaxed)) {
                            continue;
                        }
                        
                        idx = n->size;
                        n->keys[idx] = key_byte;
                        n->values[idx] = new_kv;
                        n->child_index[key_byte] = idx;
                        n->size++;
                        
                        node->version.store(incrementVersion(lock_v),
                            std::memory_order_release);
                        size_.fetch_add(1, std::memory_order_relaxed);
                        return IndexOpResult::SUCCESS;
                    } else {
                        uint64_t lock_v = lockVersion(version);
                        if (!node->version.compare_exchange_weak(version, lock_v,
                            std::memory_order_acquire, std::memory_order_relaxed)) {
                            continue;
                        }
                        
                        Node* new_node = growNode48To256(n);
                        node = new_node;
                        if (!casChild(root_, old_root, new_node)) {
                            old_root = new_node;
                        }
                        continue;
                    }
                }
                    
                case NODE256: {
                    auto* n = static_cast<Node256*>(node);
                    
                    KeyValue* expected = loadValue(n->values[key_byte]);
                    if (expected) {
                        if (casValue(n->values[key_byte], expected, new_kv)) {
                            return IndexOpResult::SUCCESS;
                        }
                        continue;
                    }
                    
                    uint64_t lock_v = lockVersion(version);
                    if (!node->version.compare_exchange_weak(version, lock_v,
                        std::memory_order_acquire, std::memory_order_relaxed)) {
                        continue;
                    }
                    
                    n->values[key_byte] = new_kv;
                    n->size++;
                    
                    node->version.store(incrementVersion(lock_v),
                        std::memory_order_release);
                    size_.fetch_add(1, std::memory_order_relaxed);
                    return IndexOpResult::SUCCESS;
                }
            }
            
            depth++;
        }
    }
    
    std::optional<Value> find(Key key) {
        Node* node = loadChild(root_);
        
        if (!node) {
            return std::nullopt;
        }
        
        uint8_t depth = 0;
        
        while (node) {
            EpochGuard guard;
            
            if (!isPrefixMatch(node, key, depth)) {
                return std::nullopt;
            }
            
            KeyValue* kv = findValue(node, key, depth);
            
            if (kv && kv->key == key) {
                return kv->value;
            }
            
            depth++;
            node = nullptr; // Would need child traversal logic
        }
        
        return std::nullopt;
    }
    
    IndexOpResult upsert(Key key, Value value) {
        KeyValue* new_kv = new KeyValue(key, value);
        
        // First try to find existing
        Node* old_root = loadChild(root_);
        
        if (!old_root) {
            auto* new_node = new Node4();
            new_node->keys[0] = getKeyByte(key, 0);
            new_node->values[0] = new_kv;
            new_node->size = 1;
            
            if (root_.compare_exchange_strong(old_root, new_node,
                std::memory_order_release, std::memory_order_relaxed)) {
                size_.fetch_add(1, std::memory_order_relaxed);
                return IndexOpResult::SUCCESS;
            }
            
            EpochReclamation::getInstance().retire(new_kv);
        }
        
        // Simplified upsert - just insert with overwrite semantics
        return insert(key, value);
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
