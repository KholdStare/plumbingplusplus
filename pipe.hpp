#ifndef PIPE_HPP_JBCZ12AY
#define PIPE_HPP_JBCZ12AY

//      Copyright Alexander Kondratskiy 2012 - 2013.
// Distributed under the Boost Software License, Version 1.0.
//    (See accompanying file LICENSE_1_0.txt or copy at
//          http://www.boost.org/LICENSE_1_0.txt)
//
#include <cassert>

#include <vector>
#include <mutex>
#include <condition_variable>
#include <boost/optional.hpp>

namespace Plumbing
{
    template <typename T>
    class Pipe
    {
        // TODO: dynamically resize fifo according to demand?

        // TODO: perhaps create a different queue which is "infinite" (a linked list),
        //       but provides a way to "stall" it on a predicate (e.g. memory usage)

        std::vector<boost::optional<T>> fifo_;
        int write_;
        int read_;
        std::mutex mutex_;
        std::condition_variable readyForWrite_;
        std::condition_variable readyForRead_;
        bool open_;

        /**
         * Return the number of free slots available for writing
         */
        inline int writeHeadroom()
        {
            return (write_ < read_
                   ? read_ - write_
                   : (read_ + fifo_.size()) - write_) - 1;
        }

        /**
         * Return the number of free slots available for reading
         */
        inline int readHeadroom()
        {
            return read_ <= write_
                   ? write_ - read_
                   : (write_ + fifo_.size()) - read_;
        }

        inline void incrementWrite()
        {
            write_ = (write_ + 1) % fifo_.size();
        }

        inline void incrementRead()
        {
            read_ = (read_ + 1) % fifo_.size();
        }

    public:
        Pipe(std::size_t fifoSize = 10)
            : fifo_(fifoSize),
              write_(0),
              read_(0),
              open_(true)
        {
            assert (fifoSize >= 2);
        }

        Pipe(Pipe<T> const& other) = delete;

        Pipe(Pipe<T>&& other) :
            fifo_(std::move(other.fifo_)),
            write_(std::move(other.write_)),
            read_(std::move(other.read_)),
            mutex_(),
            readyForWrite_(),
            readyForRead_(),
            open_(std::move(other.open_))
        {
            other.open_ = false;
        }

        Pipe<T>& operator = (Pipe<T>&& other)
        {
            fifo_ = std::move(other.fifo_);
            write_ = std::move(other.write_);
            read_ = std::move(other.read_);
            open_ = std::move(other.open_);
            other.open_ = false;

            return *this;
        }

        /************************************
         *  Facilities for writing to pipe  *
         ************************************/
        
        /**
         * Enqueue a value of type T into the pipe, if possible.
         *
         * @return whether the operation succeeded.
         */
        template <typename U>
        bool enqueue(U&& e) noexcept
        {
            // inexpensive check before acquiring lock
            if (!open_) { return false; }

            std::unique_lock<std::mutex> lock(mutex_);
            while(!writeHeadroom())
            {
                readyForWrite_.wait(lock);
                // check if we can write again,
                // since it could have changed
                if (!open_) { return false; }
            }

            // perform actual write
            fifo_[write_] = std::forward<U>(e);
            incrementWrite();

            readyForRead_.notify_one();

            return true;
        }

        /**
         * Closes the pipe for writing.
         *
         * The read side will attempt to read any remaining values in the pipe.
         *
         * @note: could deadlock if used from the read side:
         *       - no space to write, because read side not clear.
         *       - read side tries to close, when there is no space to write
         *       - waits indefinitely for room to write.
         *
         * @note: DO NOT USE FROM read side. Use forceClose() instead, if you
         * do not intend to read anymore and are "abandoning ship".
         */
        void close() noexcept
        {
            if (!open_) {
                return;
            } // noop if already closed

            std::unique_lock<std::mutex> lock(mutex_);
            while(!writeHeadroom())
            {
                readyForWrite_.wait(lock);
                // noop if already closed
                if (!open_) { return; } 
            }

            // perform actual write
            fifo_[write_] = boost::optional<T>(boost::none);
            incrementWrite(); // give space to read

            readyForRead_.notify_all();
            // also notify writers, as they cannot write anymore
            readyForWrite_.notify_all(); 

            open_ = false; // hint for write side
        }

        /**************************************
         *  Facilities for reading from pipe  *
         **************************************/
        
        /**
         * @return whether values are available for reading from the pipe.
         */
        bool hasNext()
        {
            std::unique_lock<std::mutex> lock(mutex_);
            while(!readHeadroom())
            {
                readyForRead_.wait(lock);
            }

            return fifo_[read_];
        }

        /**
         * Close the pipe, ignoring write headroom.
         *
         * This means that READS COULD BE CORRUPTED. Use only if there is no
         * point in continuing reading from this pipe (e.g. exception occurred
         * in context, so we abandon ship).
         *
         * To summarize: only use from read side in case of an "emergency".
         */
        void forceClose() noexcept
        {
            if (!open_) {
                return;
            } // noop if already closed

            std::unique_lock<std::mutex> lock(mutex_);

            fifo_[write_] = boost::optional<T>(boost::none);
            incrementWrite(); // give space to read

            readyForRead_.notify_all();
            // also notify writers, as they cannot write anymore
            readyForWrite_.notify_all(); 

            open_ = false;
        }

        // TODO boost::optional should be return type so that the
        // check for open and getting the value is an atomic operation.
        // Currently if two threads try reading from a pipe, both can pass
        // the hasNext() check, one reads an actual value, while the other
        // might try to read when the pipe is already closed.
        T dequeue()
        {
            std::unique_lock<std::mutex> lock(mutex_);
            while(!readHeadroom())
            {
                readyForRead_.wait(lock);
            }

            T const& e = *fifo_[read_];
            incrementRead();

            readyForWrite_.notify_one();

            return std::move(e);
        }
    };

}

#endif /* end of include guard: PIPE_HPP_JBCZ12AY */
