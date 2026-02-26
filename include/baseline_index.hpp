#ifndef BASELINE_INDEX_HPP
#define BASELINE_INDEX_HPP

#include <unordered_map>
#include <mutex>
#include <shared_mutex>
#include <optional>
#include "types.hpp"

class BaselineLockedIndex {
private:
    std::unordered_map<Key, Value> data_;
    mutable std::shared_mutex mutex_;
    std::atomic<uint64_t> size_{0};
    
public:
    BaselineLockedIndex() : data_(), mutex_(), size_(0) {
        data_.reserve(1024 * 1024);
    }
    
    ~BaselineLockedIndex() = default;
    
    IndexOpResult insert(Key key, Value value) {
        std::unique_lock<std::shared_mutex> lock(mutex_);
        
        auto it = data_.find(key);
        if (it != data_.end()) {
            return IndexOpResult::EXISTS;
        }
        
        data_.emplace(key, value);
        size_.fetch_add(1, std::memory_order_relaxed);
        return IndexOpResult::SUCCESS;
    }
    
    std::optional<Value> find(Key key) {
        std::shared_lock<std::shared_mutex> lock(mutex_);
        
        auto it = data_.find(key);
        if (it != data_.end()) {
            return it->second;
        }
        return std::nullopt;
    }
    
    IndexOpResult upsert(Key key, Value value) {
        std::unique_lock<std::shared_mutex> lock(mutex_);
        
        auto it = data_.find(key);
        if (it != data_.end()) {
            it->second = value;
            return IndexOpResult::SUCCESS;
        }
        
        data_.emplace(key, value);
        size_.fetch_add(1, std::memory_order_relaxed);
        return IndexOpResult::SUCCESS;
    }
    
    IndexOpResult erase(Key key) {
        std::unique_lock<std::shared_mutex> lock(mutex_);
        
        auto it = data_.find(key);
        if (it == data_.end()) {
            return IndexOpResult::NOT_FOUND;
        }
        
        data_.erase(it);
        size_.fetch_sub(1, std::memory_order_relaxed);
        return IndexOpResult::SUCCESS;
    }
    
    uint64_t size() const {
        std::shared_lock<std::shared_mutex> lock(mutex_);
        return data_.size();
    }
    
    void clear() {
        std::unique_lock<std::shared_mutex> lock(mutex_);
        data_.clear();
        size_.store(0, std::memory_order_relaxed);
    }
};

class BaselineCoarseBTree {
private:
    struct alignas(CACHE_LINE_SIZE) Node {
        bool is_leaf;
        std::vector<Key> keys;
        std::vector<Value> values;
        std::vector<Node*> children;
        std::mutex node_mutex;
        
        Node(bool leaf = true) : is_leaf(leaf) {}
    };
    
    Node* root_;
    std::mutex tree_mutex_;
    size_t size_;
    
    Node* findLeaf(Node* node, Key key) const {
        if (!node) return nullptr;
        
        std::lock_guard<std::mutex> lock(node->node_mutex);
        
        if (node->is_leaf) {
            return node;
        }
        
        size_t i = 0;
        while (i < node->keys.size() && key >= node->keys[i]) {
            i++;
        }
        
        if (i < node->children.size()) {
            return findLeaf(node->children[i], key);
        }
        return nullptr;
    }
    
public:
    BaselineCoarseBTree() : root_(nullptr), size_(0) {
        root_ = new Node(true);
    }
    
    ~BaselineCoarseBTree() {
        delete root_;
    }
    
    IndexOpResult insert(Key key, Value value) {
        std::lock_guard<std::mutex> global_lock(tree_mutex_);
        
        if (!root_) {
            root_ = new Node(true);
        }
        
        Node* leaf = findLeaf(root_, key);
        if (!leaf) {
            leaf = root_;
        }
        
        std::lock_guard<std::mutex> leaf_lock(leaf->node_mutex);
        
        for (size_t i = 0; i < leaf->keys.size(); ++i) {
            if (leaf->keys[i] == key) {
                return IndexOpResult::EXISTS;
            }
        }
        
        size_t pos = 0;
        while (pos < leaf->keys.size() && leaf->keys[pos] < key) {
            pos++;
        }
        
        leaf->keys.insert(leaf->keys.begin() + pos, key);
        leaf->values.insert(leaf->values.begin() + pos, value);
        size_++;
        
        return IndexOpResult::SUCCESS;
    }
    
    std::optional<Value> find(Key key) {
        std::lock_guard<std::mutex> global_lock(tree_mutex_);
        
        if (!root_) {
            return std::nullopt;
        }
        
        Node* leaf = findLeaf(root_, key);
        if (!leaf) {
            return std::nullopt;
        }
        
        std::lock_guard<std::mutex> leaf_lock(leaf->node_mutex);
        
        for (size_t i = 0; i < leaf->keys.size(); ++i) {
            if (leaf->keys[i] == key) {
                return leaf->values[i];
            }
        }
        
        return std::nullopt;
    }
    
    IndexOpResult upsert(Key key, Value value) {
        std::lock_guard<std::mutex> global_lock(tree_mutex_);
        
        if (!root_) {
            root_ = new Node(true);
        }
        
        Node* leaf = findLeaf(root_, key);
        if (!leaf) {
            leaf = root_;
        }
        
        std::lock_guard<std::mutex> leaf_lock(leaf->node_mutex);
        
        for (size_t i = 0; i < leaf->keys.size(); ++i) {
            if (leaf->keys[i] == key) {
                leaf->values[i] = value;
                return IndexOpResult::SUCCESS;
            }
        }
        
        size_t pos = 0;
        while (pos < leaf->keys.size() && leaf->keys[pos] < key) {
            pos++;
        }
        
        leaf->keys.insert(leaf->keys.begin() + pos, key);
        leaf->values.insert(leaf->values.begin() + pos, value);
        size_++;
        
        return IndexOpResult::SUCCESS;
    }
    
    uint64_t size() {
        std::lock_guard<std::mutex> lock(tree_mutex_);
        return size_;
    }
    
    void clear() {
        std::lock_guard<std::mutex> lock(tree_mutex_);
        delete root_;
        root_ = new Node(true);
        size_ = 0;
    }
};

#endif
