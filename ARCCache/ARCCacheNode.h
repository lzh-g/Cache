#pragma once

#include <memory>

namespace Cache
{
    template <typename Key, typename Value>
    class ARCNode
    {
    public:
        ARCNode() : accessCount_(1), prev_(nullptr), next_(nullptr) {}

        ARCNode(Key key, Value value)
            : key_(key), value_(value), accessCount_(1), prev_(nullptr), next_(nullptr) {}

        // Getters
        Key getKey() const { return key_; }
        Value getValue() const { return value_; }
        size_t getAccessCount() const { return accessCount_; }

        // Setters
        void setValue(const Value &value) { value_ = value; }
        void incrementAccessCount() { ++accessCount_; }

        template <typename K, typename V>
        friend class ARCLRUPart;
        template <typename K, typename V>
        friend class
            ARCLFUPart;

    private:
        Key key_;
        Value value_;
        size_t accessCount_;
        std::shared_ptr<ARCNode> prev_;
        std::shared_ptr<ARCNode> next_;
    };
}