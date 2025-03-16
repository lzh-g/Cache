#pragma once

#include <cmath>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <vector>

#include "CachePolicy.h"

namespace Cache
{

    // 前向声明
    template <typename Key, typename Value>
    class LFUCache;

    // 访问频率队列类
    template <typename Key, typename Value>
    class FreqList
    {
    private:
        // LFU结点结构体
        struct Node
        {
            int freq; // 访问频次
            Key key;
            Value value;
            std::shared_ptr<Node> pre; // 上一结点
            std::shared_ptr<Node> next;

            Node() : freq(1), pre(nullptr), next(nullptr) {}
            Node(Key key, Value value) : freq(1), key(key), value(value), pre(nullptr), next(nullptr) {}
        };

        using NodePtr = std::shared_ptr<Node>;
        int freq_;     // 访问频率
        NodePtr head_; // 虚拟头节点
        NodePtr tail_; // 虚拟尾结点

    public:
        explicit FreqList(int n) : freq_(n)
        {
            head_ = std::make_shared<Node>();
            tail_ = std::make_shared<Node>();
            head_->next = tail_;
            tail_->pre = head_;
        }

        bool isEmpty() const
        {
            return head_->next == tail_;
        }

        // 添加结点到尾部
        void addNode(NodePtr node)
        {
            if (!node || !head_ || !tail_)
            {
                return;
            }
            node->pre = tail_->pre;
            node->next = tail_;
            tail_->pre->next = node;
            tail_->pre = node;
        }

        void removeNode(NodePtr node)
        {
            if (!node || !head_ || !tail_)
            {
                return;
            }
            if (!node->pre || !node->next)
            {
                return;
            }
            node->pre->next = node->next;
            node->next->pre = node->pre;
            node->pre = nullptr;
            node->next = nullptr;
        }

        NodePtr getFirstNode() const
        {
            return head_->next;
        }

        friend class LFUCache<Key, Value>;
    };

    template <typename Key, typename Value>
    class LFUCache : public CachePolicy<Key, Value>
    {
    public:
        using Node = typename FreqList<Key, Value>::Node;
        using NodePtr = std::shared_ptr<Node>;
        using NodeMap = std::unordered_map<Key, NodePtr>;

        LFUCache(int capacity, int maxAverageNum = 10)
            : capacity_(capacity), minFreq_(INT8_MAX), maxAverageNum_(maxAverageNum), curAverageNum_(0), curTotalNum_(0)
        {
        }

        ~LFUCache() override = default;

        void put(Key key, Value value) override
        {
            if (capacity_ == 0)
            {
                return;
            }
            std::lock_guard<std::mutex> lock(mutex_);
            auto it = nodeMap_.find(key);
            if (it != nodeMap_.end())
            {
                // 更新value值
                it->second->value = value;
                // 命中则直接修改，并获取一次以更新访问频率
                getInternal(it->second, value);
                return;
            }

            putInternal(key, value);
        }

        // value作为传出参数
        bool get(Key key, Value &value) override
        {
            std::lock_guard<std::mutex> lock(mutex_);
            auto it = nodeMap_.find(key);
            if (it != nodeMap_.end())
            {
                getInternal(it->second, value);
                return true;
            }
            return false;
        }

        Value get(Key key) override
        {
            Value value;
            get(key, value);
            return value;
        }

        // 清空缓存，回收资源
        void purge()
        {
            nodeMap_.clear();
            freqToFreqList_.clear();
        }

    private:
        // 添加缓存
        void putInternal(Key key, Value value);
        // 获取缓存
        void getInternal(NodePtr node, Value &value);
        // 移除缓存中过期数据
        void kickOut();
        // 从频率列表中移除结点
        void removeFromFreqList(NodePtr node);
        // 添加到频率列表
        void addToFreqList(NodePtr node);
        // 增加平均访问频率
        void addFreqNum();
        // 减少平均访问频率
        void decreaseFreqNum(int num);
        // 处理当前平均访问频率超过上限的情况
        void handleOverMaxAverageNum();
        // 更新最小访问频次
        void updateMinFreq();

    private:
        // 缓存容量
        int capacity_;
        // 最小访问频次
        int minFreq_;
        // 最大平均访问频次
        int maxAverageNum_;
        // 当前平均访问频次
        int curAverageNum_;
        // 当前访问所有缓存总次数
        int curTotalNum_;
        // 互斥锁
        std::mutex mutex_;
        // key->缓存结点
        NodeMap nodeMap_;
        // 访问频次到该频次链表的映射
        std::unordered_map<int, FreqList<Key, Value> *> freqToFreqList_;
    };

    template <typename Key, typename Value>
    inline void LFUCache<Key, Value>::putInternal(Key key, Value value)
    {
        // 若不在缓存中，判断缓存是否已满
        if (nodeMap_.size() == capacity_)
        {
            // 若缓存已满，则删除最不常访问结点，更新当前的平均访问频次和总访问频次
            kickOut();
        }

        // 创建新结点，将新结点添加进来，更新最小访问频次
        NodePtr node = std::make_shared<Node>(key, value);
        nodeMap_[key] = node;
        addToFreqList(node);
        addFreqNum();
        minFreq_ = std::min(minFreq_, 1);
    }

    template <typename Key, typename Value>
    inline void LFUCache<Key, Value>::getInternal(NodePtr node, Value &value)
    {
        // 找到后将其从低访问频次的链表中删除，并添加到+1的访问频次链表中
        // 访问频次+1，返回value值
        value = node->value;
        // 从原有访问频次的链表中删除结点
        removeFromFreqList(node);
        node->freq++;
        addToFreqList(node);
        // 若当前node访问频次等于minFreq+1，且其前驱链表为空，则说明freqToFreqList_[node->freq - 1]链表因node迁移已空，需要更新最小访问频次
        if (node->freq - 1 == minFreq_ && freqToFreqList_[node->freq - 1]->isEmpty())
        {
            minFreq_++;
        }
        // 总访问频次和当前平均访问频次都随之增加
        addFreqNum();
    }

    template <typename Key, typename Value>
    inline void LFUCache<Key, Value>::kickOut()
    {
        NodePtr node = freqToFreqList_[minFreq_]->getFirstNode();
        removeFromFreqList(node);
        nodeMap_.erase(node->key);
        decreaseFreqNum(node->freq);
    }

    template <typename Key, typename Value>
    inline void LFUCache<Key, Value>::removeFromFreqList(NodePtr node)
    {
        if (!node)
        {
            return;
        }
        auto freq = node->freq;
        freqToFreqList_[freq]->removeNode(node);
    }

    template <typename Key, typename Value>
    inline void LFUCache<Key, Value>::addToFreqList(NodePtr node)
    {
        if (!node)
        {
            return;
        }
        // 添加进入相应频次链表前判断该频次链表是否存在
        auto freq = node->freq;
        if (freqToFreqList_.find(freq) == freqToFreqList_.end())
        {
            freqToFreqList_[freq] = new FreqList<Key, Value>(freq);
        }
        freqToFreqList_[freq]->addNode(node);
    }

    template <typename Key, typename Value>
    inline void LFUCache<Key, Value>::addFreqNum()
    {
        curTotalNum_++;
        if (nodeMap_.empty())
        {
            curAverageNum_ = 0;
        }
        else
        {
            curAverageNum_ = curTotalNum_ / nodeMap_.size();
        }
        if (curAverageNum_ > maxAverageNum_)
        {
            handleOverMaxAverageNum();
        }
    }

    template <typename Key, typename Value>
    inline void LFUCache<Key, Value>::decreaseFreqNum(int num)
    {
        // 减少平均访问频次和总访问频次
        curTotalNum_ -= num;
        if (nodeMap_.empty())
        {
            curAverageNum_ = 0;
        }
        else
        {
            curAverageNum_ = curTotalNum_ / nodeMap_.size();
        }
    }

    template <typename Key, typename Value>
    inline void LFUCache<Key, Value>::handleOverMaxAverageNum()
    {
        if (nodeMap_.empty())
        {
            return;
        }

        // 当前平均访问频次超过了最大平均访问频次，所有结点访问频次-(maxAverageNum_ / 2)
        for (auto it = nodeMap_.begin(); it != nodeMap_.end(); it++)
        {
            // 检查结点是否为空
            if (!it->second)
            {
                continue;
            }
            NodePtr node = it->second;

            // 先从当前频率列表中移除
            removeFromFreqList(node);

            // 减少频率
            node->freq -= maxAverageNum_ / 2;
            if (node->freq < 1)
            {
                node->freq = 1;
            }
            // 添加到新的频次列表
            addToFreqList(node);
        }
        // 更新最小频次
        updateMinFreq();
    }

    template <typename Key, typename Value>
    inline void LFUCache<Key, Value>::updateMinFreq()
    {
        minFreq_ = INT8_MAX;
        for (const auto &pair : freqToFreqList_)
        {
            if (pair.second && !pair.second->isEmpty())
            {
                minFreq_ = std::min(minFreq_, pair.first);
            }
        }
        // 初始无数据
        if (minFreq_ == INT8_MAX)
        {
            minFreq_ = 1;
        }
    }

    // HashLRU缓存类
    template <typename Key, typename Value>
    class HashLRUCache
    {
    public:
        HashLRUCache(size_t capacity, int sliceNum, int maxAverageNum = 10)
            : sliceNum_(sliceNum > 0 ? sliceNum : std::thread::hardware_concurrency()), capacity_(capacity)
        {
            size_t sliceSize = std::ceil(capacity_ / static_cast<double>(sliceNum_));
            for (int i = 0; i < sliceNum_; i++)
            {
                LFUSliceCaches_.emplace_back(new LFUCache<Key, Value>(sliceSize, maxAverageNum));
            }
        }

        void put(Key key, Value value)
        {
            size_t sliceIndex = Hash(key) % sliceNum_;
            return LFUSliceCaches_[sliceIndex]->put(key, value);
        }

        bool get(Key key, Value &value)
        {
            size_t sliceIndex = Hash(key) % sliceNum_;
            return LFUSliceCaches_[sliceIndex]->get(key, value);
        }

        Value get(Key key)
        {
            Value value;
            get(key, value);
            return value;
        }

        void purge()
        {
            for (auto &LFUSliceCache : LFUSliceCaches_)
            {
                LFUSliceCache->pruge();
            }
        }

    private:
        size_t Hash(Key key)
        {
            std::hash<Key> hashFunc;
            return hashFunc(key);
        }

    private:
        size_t capacity_;                                                   // 缓存总容量
        int sliceNum_;                                                      // 缓存分片数量
        std::vector<std::unique_ptr<LFUCache<Key, Value>>> LFUSliceCaches_; // 缓存LRU分片容器
    };

} // namespace Cache
