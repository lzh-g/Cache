#pragma once

#include <cstring>
#include <list>
#include <memory>
#include <mutex>
#include <unordered_map>

#include "CachePolicy.h"

namespace Cache
{
    // 前向声明
    template <typename Key, typename Value>
    class LRUCache;

    // LRU结点类
    template <typename Key, typename Value>
    class LRUNode
    {
    public:
        LRUNode(Key key, Value value)
            : key_(key), value_(value), accessCount_(1), prev_(nullptr), next_(nullptr)
        {
        }
        ~LRUNode() {};

        // 提供必要的访问器
        Key getKey() const { return key_; }
        Value getValue() const { return value_; }
        void setValue(const Value &value) { value_ = value; }
        size_t getAccessCount() const { return accessCount_; }
        void incrementAccessCount() { ++accessCount_; }

        friend class LRUCache<Key, Value>;

    private:
        Key key_;
        Value value_;
        size_t accessCount_; // 访问次数
        std::shared_ptr<LRUNode<Key, Value>> prev_;
        std::shared_ptr<LRUNode<Key, Value>> next_;
    };

    // LRU缓存类，规定从队头->队尾，访问时间越晚
    template <typename Key, typename Value>
    class LRUCache : public CachePolicy<Key, Value>
    {
    public:
        using LRUNodeType = LRUNode<Key, Value>;
        using NodePtr = std::shared_ptr<LRUNodeType>;
        using NodeMap = std::unordered_map<Key, NodePtr>;

        LRUCache(int capacity)
            : capacity_(capacity)
        {
            initializeList();
        }
        ~LRUCache() override = default;

        // 添加缓存
        void put(Key key, Value value) override
        {
            if (capacity_ <= 0)
            {
                return;
            }

            std::lock_guard<std::mutex> lock(mutex_);
            auto it = nodeMap_.find(key);
            if (it != nodeMap_.end())
            {
                // 如果命中，则更新value，并调用get方法，代表该数据刚被访问
                updateExistingNode(it->second, value);
                return;
            }

            addNewNode(key, value);
        }

        bool get(Key key, Value &value) override
        {
            std::lock_guard<std::mutex> lock(mutex_);
            auto it = nodeMap_.find(key);
            if (it != nodeMap_.end())
            {
                // 缓存命中，将命中结点标记为最近访问，即移动尾头节点
                moveToMostRecent(it->second);
                value = it->second->getValue();
                return true;
            }
            return false;
        }

        Value get(Key key) override
        {
            Value value{};
            get(key, value);
            return value;
        }

        // 删除指定元素
        void remove(Key key)
        {
            std::lock_guard<std::mutex> lock(mutex_);
            auto it = nodeMap_.find(key);
            if (it != nodeMap_.end())
            {
                removeNode(it->second);
                nodeMap_.erase(it);
            }
        }

    private:
        void initializeList()
        {
            // 创建首尾虚拟结点
            dummyHead_ = std::make_shared<LRUNodeType>(Key(), Value());
            dummyTail_ = std::make_shared<LRUNodeType>(Key(), Value());
            dummyHead_->next_ = dummyTail_;
            dummyTail_->prev_ = dummyHead_;
        }

        void updateExistingNode(NodePtr node, const Value &value)
        {
            node->setValue(value);
            moveToMostRecent(node);
        }

        void addNewNode(const Key &key, const Value &value)
        {
            if (nodeMap_.size() >= capacity_)
            {
                evictLeastRecent();
            }

            NodePtr newNode = std::make_shared<LRUNodeType>(key, value);
            insertNode(newNode);
            nodeMap_[key] = newNode;
        }

        // 将该结点移动到最新的位置
        void moveToMostRecent(NodePtr node)
        {
            removeNode(node);
            insertNode(node);
        }

        void removeNode(NodePtr node)
        {
            node->prev_->next_ = node->next_;
            node->next_->prev_ = node->prev_;
        }

        // 从尾部插入结点
        void insertNode(NodePtr node)
        {
            node->next_ = dummyTail_;
            node->prev_ = dummyTail_->prev_;
            dummyTail_->prev_->next_ = node;
            dummyTail_->prev_ = node;
        }

        // 将最近最少访问结点移除
        void evictLeastRecent()
        {
            NodePtr leastRecent = dummyHead_->next_;
            removeNode(leastRecent);
            nodeMap_.erase(leastRecent->getKey());
        }

    private:
        int capacity_;    // 缓存容量
        NodeMap nodeMap_; // key -> Node
        std::mutex mutex_;
        NodePtr dummyHead_; // 虚拟头结点
        NodePtr dummyTail_; // 虚拟尾结点
    };

    // LRU优化：LRU-K缓存类，继承LRU缓存类进行优化
    // 增加一个和LRU缓存队列一样的数据访问历史队列，Value为访问次数
    template <typename Key, typename Value>
    class LRUKCache : public LRUCache<Key, Value>
    {
    public:
        // 调用基类构造
        LRUKCache(int capacity, int historyCapacity, int k)
            : LRUCache<Key, Value>(capacity), historyList_(std::make_unique<LRUCache<Key, size_t>>(historyCapacity)), k_(k)
        {
        }

        Value get(Key key)
        {
            // 获取该数据访问次数
            int historyCount = historyList_->get(key);
            // 如果访问到数据，则更新历史访问记录节点值count++
            historyList_->put(key, ++historyCount);

            // 从缓存中获取数据，不一定能获取到，因为可能不在缓存中
            return LRUCache<Key, Value>::get(key);
        }

        void put(Key key, Value value)
        {
            // 先判断是否存在于缓存中，若存在则直接覆盖，否则不直接添加到缓存
            if (LRUCache<Key, Value>::get(key) != "")
            {
                LRUCache<Key, Value>::put(key, value);
            }

            // 若数据历史访问次数达到上限，则加入缓存
            int historyCount = historyList_->get(key);
            historyList_->put(key, ++historyCount);

            if (historyCount >= k_)
            {
                // 移除历史访问记录
                historyList_->remove(key);
                // 加入缓存
                LRUCache<Key, Value>::put(key, value);
            }
        }

    private:
        int k_;                                              // 进入缓存队列的评判标准
        std::unique_ptr<LRUCache<Key, size_t>> historyList_; // 访问数据历史记录(Value为访问次数)
    };

    // HashLRU缓存类，优化LRU锁粒度过大的问题
    // 将LRU分片，引入哈希算法将数据分散到各个LRUCache上
    template <typename Key, typename Value>
    class HashLRUCaches
    {
    public:
        // 若切片数量大于0，则分片为切片数，否则，分片为CPU核心数
        HashLRUCaches(size_t capacity, int sliceNum)
            : capacity_(capacity), sliceNum_(sliceNum > 0 ? sliceNum : std::thread::hardware_concurrency())
        {
            size_t sliceSize = std::ceil(capacity / static_cast<double>(sliceNum_)); // 获得每个分片的大小
            for (int i = 0; i < sliceNum_; i++)
            {
                LRUSliceCaches_.emplace_back(new LRUCache<Key, Value>(sliceSize));
            }
        }

        void put(Key key, Value value)
        {
            // 获取key的hash值，计算出对应的分片索引
            size_t sliceIndex = Hash(key) % sliceNum_;
            return LRUSliceCaches_[sliceIndex]->put(key, value);
        }

        bool get(Key key, Value &value)
        {
            // 获取key的hash值，计算出对应的分片索引
            size_t sliceIndex = Hash(key) % sliceNum_;
            return LRUSliceCaches_[sliceIndex]->get(key, value);
        }

        Value get(Key key)
        {
            Value value{};
            get(key, value);
            return value;
        }

    private:
        // 将key转换为对应的hash值
        size_t Hash(Key key)
        {
            std::hash<Key> hashFunc;
            return hashFunc(key);
        }

    private:
        size_t capacity_;                                                   // 总容量
        int sliceNum_;                                                      // LRU分片数量
        std::vector<std::unique_ptr<LRUCache<Key, Value>>> LRUSliceCaches_; // 切片LRU缓存
    };
}