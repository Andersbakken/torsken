#ifndef CIRCULAR_BUFFER_H
#define CIRCULAR_BUFFER_H

#include <assert.h>

template <size_t Size, typename T> class CircularBuffer
{
public:
    template <typename BufferPointer> class iterator_base
    {
    public:
        inline iterator_base(BufferPointer buffer, int index) : mBuffer(buffer), mIndex(index)
        {
        }
        inline iterator_base(const iterator_base &other) : mBuffer(other.mBuffer), mIndex(other.mIndex)
        {
        }
        template <typename P> inline iterator_base(const iterator_base<P> &other) : mBuffer(other.buffer()), mIndex(other.index())
        {
        }

        inline bool operator==(const iterator_base &other) const
        {
            return mBuffer == other.mBuffer && mIndex == other.mIndex;
        }
        inline bool operator!=(const iterator_base &other) const
        {
            return !(*this == other);
        }
        inline iterator_base &operator++()
        {
            ++mIndex;
            return *this;
        }

        inline const T &operator*() const
        {
            return mBuffer->at(mIndex);
        }
        inline const T *operator->() const
        {
            return &operator*();
        }

        inline size_t index() const
        {
            return mIndex;
        }
        inline BufferPointer buffer() const
        {
            return mBuffer;
        }

    protected:
        BufferPointer mBuffer;
        size_t mIndex;
    };

    class iterator : public iterator_base<CircularBuffer *>
    {
        inline iterator(CircularBuffer *buffer, size_t index) : iterator_base<CircularBuffer *>(buffer, index)
        {
        }

    public:
        inline T &operator*()
        {
            return iterator_base<CircularBuffer *>::mBuffer->at(iterator_base<CircularBuffer *>::mIndex);
        }
        inline T *operator->()
        {
            return &operator*();
        }

    private:
        friend class CircularBuffer<Size, T>;
    };

    typedef iterator_base<const CircularBuffer *> const_iterator;

    inline CircularBuffer() : mFirst(0), mLast(0), mCount(0)
    {
    }
    inline CircularBuffer<Size, T> &operator=(const CircularBuffer<Size, T> &other) = delete;
    inline CircularBuffer<Size, T> &operator=(CircularBuffer<Size, T> &&other) = delete;

    inline void append(const T &t)
    {
        assert(mCount <= Size);
        if (mCount) {
            // coverity[divide_by_zero]
            mLast = (mLast + 1) % Size;
            if (mLast == mFirst)
                mFirst = (mFirst + 1) % Size;
            else
                ++mCount;
        } else {
            mFirst = mLast = 0;
            mCount = 1;
        }
        mArray[mLast] = t;
    }

    inline const_iterator begin() const
    {
        return const_iterator(this, 0);
    }
    inline const_iterator end() const
    {
        return const_iterator(this, mCount);
    }
    inline const_iterator cbegin() const
    {
        return begin();
    }
    inline const_iterator cend() const
    {
        return end();
    }

    inline iterator begin()
    {
        return iterator(this, 0);
    }
    inline iterator end()
    {
        return iterator(this, mCount);
    }

    inline const T &operator[](size_t idx) const
    {
        assert(idx < mCount);
        return mArray[(mFirst + idx) % Size];
    }
    inline T &operator[](size_t idx)
    {
        assert(idx < mCount);
        return mArray[(mFirst + idx) % Size];
    }

    inline const T &at(size_t idx) const
    {
        return operator[](idx);
    }
    inline T &at(size_t idx)
    {
        return operator[](idx);
    }

    inline const T &first() const
    {
        assert(mCount);
        return mArray[mFirst];
    }
    inline const T &last() const
    {
        assert(mCount);
        return mArray[mLast];
    }
    inline T &first()
    {
        assert(mCount);
        return mArray[mFirst];
    }
    inline T &last()
    {
        assert(mCount);
        return mArray[mLast];
    }

    inline T pop_front()
    {
        const size_t front = mFirst;
        const T ret = mArray[front];
        mFirst = (mFirst + 1) % Size;
        --mCount;
        return ret;
    }
    inline T pop_back()
    {
        const size_t back = mLast;
        const T ret = mArray[back];
        if (mLast) {
            --mLast;
        } else {
            mLast = Size - 1;
        }
        --mCount;
        return ret;
    }

    inline size_t capacity() const
    {
        return Size;
    }
    inline size_t count() const
    {
        return mCount;
    }
    inline size_t size() const
    {
        return mCount;
    }
    inline bool isEmpty() const
    {
        return !mCount;
    }
    inline bool empty() const
    {
        return isEmpty();
    }

private:
    T mArray[Size];
    size_t mFirst, mLast, mCount;
};

#endif /* CIRCULAR_BUFFER_H */
