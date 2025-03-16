#pragma once

#include "ARCCacheNode.h"
#include <unordered_map>
#include <mutex>

namespace Cache
{
    template <typename Key, typename Value>
    class ARCLRUPart
    {
    public:
        using NodeType = ARCNode<Key, Value>;
        using NodePtr = std::shared_ptr<NodeType>;
        using NodeMap = std::unordered_map<Key, NodePtr>;

        explicit ARCLRUPart(size_t capacity, size_t transformThreshold) : capacity_(capacity), transformThreshold_(transformThreshold)
        {
            initializeList();
        }

        bool put(Key key, Value value)
        {
            if (capacity_ == 0)
            {
                return false;
            }

            std::lock_guard<std::mutex> lock(mutex_);
            auto it = mainCache_.find(key);
            // 若数据已在缓存中，则更新数据，否则添加新数据
            if (it != mainCache_.end())
            {
                return updateExistingNode(it->second, value);
            }
            return addNewNode(key, value);
        }

        bool get(Key &key, Value &value, bool &shouldTransform)
        {
            std::lock_guard<std::mutex> lock(mutex_);
            auto it = mainCache_.find(key);
            if (it != mainCache_.end())
            {
                shouldTransform = updateNodeAccess(it->second);
                value = it->second->getValue();
                return true;
            }
            return false;
        }

        bool checkGhost(Key key)
        {
            auto it = ghostCache_.find(key);
            if (it != ghostCache_.end())
            {
                removeFromGhost(it->second);
                ghostCache_.erase(it);
                return true;
            }
            return false;
        }

        void increaseCapacity()
        {
            ++capacity_;
        }

        bool decreaseCapacity()
        {
            if (capacity_ <= 0)
            {
                return false;
            }
            if (mainCache_.size() == capacity_)
            {
                evictLeastRecent();
            }
            --capacity_;
            return true;
        }

    private:
        void initializeList()
        {
            mainHead_ = std::make_shared<NodeType>();
            mainTail_ = std::make_shared<NodeType>();
            mainHead_->next_ = mainTail_;
            mainTail_->prev_ = mainHead_;

            ghostHead_ = std::make_shared<NodeType>();
            ghostTail_ = std::make_shared<NodeType>();
            ghostHead_->next_ = ghostTail_;
            ghostTail_->prev_ = ghostHead_;
        }

        bool updateExistingNode(NodePtr node, const Value &value)
        {
            node->setValue(value);
            moveToFront(node);
            return true;
        }

        bool addNewNode(const Key &key, const Value &value)
        {
            if (mainCache_.size() >= capacity_)
            {
                // 淘汰最近最少访问
                evictLeastRecent();
            }

            NodePtr newNode = std::make_shared<NodeType>(key, value);
            mainCache_[key] = newNode;
            addToFront(newNode);
            return true;
        }

        bool updateNodeAccess(NodePtr node)
        {
            moveToFront(node);
            node->incrementAccessCount();
            return node->getAccessCount() >= transformThreshold_;
        }

        void moveToFront(NodePtr node)
        {
            // 从链表中移除
            node->prev_->next_ = node->next_;
            node->next_->prev_ = node->prev_;

            // 添加到头部
            addToFront(node);
        }

        void addToFront(NodePtr node)
        {
            node->next_ = mainHead_->next_;
            node->prev_ = mainHead_;
            mainHead_->next_->prev_ = node;
            mainHead_->next_ = node;
        }

        void evictLeastRecent()
        {
            NodePtr leastRencent = mainTail_->prev_;
            if (leastRencent == mainHead_)
            {
                return;
            }
            removeFromMain(leastRencent);

            // 添加到幽灵缓存中
            if (ghostCache_.size() >= ghostCapacity_)
            {
                // 淘汰掉幽灵缓存中最老的元素
                removeOldestGhost();
            }
            addToGhost(leastRencent);

            // 从主缓存映射中删除
            mainCache_.erase(leastRencent->getKey());
        }

        void removeFromMain(NodePtr node)
        {
            node->prev_->next_ = node->next_;
            node->next_->prev_ = node->prev_;
        }

        void removeFromGhost(NodePtr node)
        {
            node->prev_->next_ = node->next_;
            node->next_->prev_ = node->prev_;
        }

        void addToGhost(NodePtr node)
        {
            // 重置结点访问计数
            node->accessCount_ = 1;

            // 添加到幽灵缓存头部
            node->next_ = mainHead_->next_;
            node->prev_ = mainHead_;

            mainHead_->next_->prev_ = node;
            mainHead_->next_ = node;

            // 添加到幽灵缓存映射
            ghostCache_[node->getKey()] = node;
        }

        void removeOldestGhost()
        {
            NodePtr oldestGhost = ghostTail_->prev_;
            if (oldestGhost == ghostHead_)
            {
                return;
            }
            removeFromGhost(oldestGhost);
            ghostCache_.erase(oldestGhost->getKey());
        }

    private:
        size_t capacity_;
        size_t ghostCapacity_;
        size_t transformThreshold_; // 转换门槛值
        std::mutex mutex_;

        NodeMap mainCache_; // key -> ARCNode
        NodeMap ghostCache_;

        // 主链表
        NodePtr mainHead_;
        NodePtr mainTail_;

        // 淘汰链表
        NodePtr ghostHead_;
        NodePtr ghostTail_;
    };
}