#pragma once

#include "../CachePolicy.h"
#include "ARCLFUPart.h"
#include "ARCLRUPart.h"
#include <memory>

namespace Cache
{
    template <typename Key, typename Value>
    class ARCCache : public CachePolicy<Key, Value>
    {
    public:
        explicit ARCCache(size_t capacity = 10, size_t transformThreshold = 2) : capacity_(capacity), transformThreshold_(transformThreshold), lruPart_(std::make_unique<ARCLRUPart<Key, Value>>(capacity, transformThreshold)), lfuPart_(std::make_unique<ARCLFUPart<Key, Value>>(capacity, transformThreshold))
        {
        }

        ~ARCCache() override = default;

        void put(Key key, Value value) override
        {
            // 判断写入的结点是否位于幽灵缓存中
            bool inGhost = checkGhostCaches(key);

            if (!inGhost)
            {
                // 若不在幽灵缓存中，同时在LRU分片和LFU分片中存放
                if (lruPart_->put(key, value))
                {
                    lfuPart_->put(key, value);
                }
            }
            else
            {
                lruPart_->put(key, value);
            }
        }

        bool get(Key key, Value &value) override
        {
            checkGhostCaches(key);

            bool shouldTransform = false;
            if (lruPart_->get(key, value, shouldTransform))
            {
                if (shouldTransform)
                {
                    lfuPart_->put(key, value);
                }
                return true;
            }
            return lfuPart_->get(key, value);
        }

        Value get(Key key) override
        {
            Value value{};
            get(key, value);
            return value;
        }

    private:
        bool checkGhostCaches(Key key)
        {
            bool inGhost = false;
            if (lruPart_->checkGhost(key))
            {
                // LRU幽灵命中，表示当前LRU缓存小，应该变大
                if (lfuPart_->decreaseCapacity())
                {
                    lruPart_->increaseCapacity();
                }
                inGhost = true;
            }
            else if (lfuPart_->checkGhost(key))
            {
                if (lruPart_->decreaseCapacity())
                {
                    lfuPart_->increaseCapacity();
                }
                inGhost = true;
            }
            return inGhost;
        }

    private:
        size_t capacity_;
        size_t transformThreshold_;
        std::unique_ptr<ARCLRUPart<Key, Value>> lruPart_;
        std::unique_ptr<ARCLFUPart<Key, Value>> lfuPart_;
    };
}