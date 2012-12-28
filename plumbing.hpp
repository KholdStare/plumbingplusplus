#ifndef PLUMBING_H_IEGJRLCP
#define PLUMBING_H_IEGJRLCP

//      Copyright Alexander Kondratskiy 2012 - 2013.
// Distributed under the Boost Software License, Version 1.0.
//    (See accompanying file LICENSE_1_0.txt or copy at
//          http://www.boost.org/LICENSE_1_0.txt)

#include <cassert>

#include "expected.hpp"

#include <vector>
#include <mutex>
#include <condition_variable>

#include <iterator>
#include <memory>
#include <functional>
#include <type_traits>
#include <thread>
#include <future>

#include <utility> // std::forward

#include <boost/optional.hpp>
#include <boost/none.hpp>

namespace Plumbing
{
    template <typename T>
    class Pipe
    {
        // TODO: dynamically resize fifo according to demand?

        // TODO: perhaps create a different queue which is "infinite" (a linked list),
        //       but provides a way to "stall" it on a predicate (e.g. memory usage)

        // TODO: think about exeption safety

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
        Pipe(std::size_t fifoSize = 2)
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
        
        // TODO: make nothrow
        /**
         * Enqueue a value of type T into the pipe, if possible.
         *
         * @return whether the operation succeeded.
         */
        bool enqueue(T const& e) noexcept
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
            fifo_[write_] = e;
            incrementWrite();

            readyForRead_.notify_one();

            return true;
        }

        // TODO: make nothrow
        /**
         * Closes the pipe for writing.
         *
         * The read side will attempt to read any remaining values in the pipe.
         *
         * @note: could deadlock from read side:
         *       - no space to write, because read side not clear.
         *       - read side tries to close, when there is no space to write
         *
         * @note: DO NOT USE FROM read side. Use forceClose() instead, if you
         * do not intend to read any more, and are "abandoning ship".
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

    //========================================================

    template <typename InputIterable> class ISource;

    template <typename InputIterable>
    class ISource
    {
        typedef InputIterable wrapped_type;

    public:
        typedef typename wrapped_type::const_iterator const_iterator;
        typedef typename const_iterator::value_type value_type;

        /**
         * Construct ISource from an InputIterable lvalue.
         */
        ISource(wrapped_type const& iterable)
            : current(iterable.begin()),
              end(iterable.end())
        { }

        // TODO: somehow have a static assert preventing the use
        // of an rvalue constructor

        // default constructors
        ISource(ISource const& other) = default;
        ISource(ISource&& other) = default;

        // need 3 methods
        bool hasNext() const noexcept 
        {
            return current != end;
        }

        value_type next() {
            return *current++;
        }
        
        void close() const { } // noop

    private:

        const_iterator current;
        const_iterator end;
    };

    /**
     * Specialization for Pipe T
     */
    template <typename T>
    class ISource< std::shared_ptr<Pipe<T>> >
    {
        
    public:
        typedef T value_type;
        typedef std::shared_ptr<Pipe<value_type>> pipe_type;

        ISource(pipe_type const& pipe)
            : pipe_(pipe)
        { }

        ISource(pipe_type&& pipe)
            : pipe_(std::move(pipe))
        { }

        // default constructors
        ISource(ISource const& other) = default;
        ISource(ISource&& other) = default;

        bool hasNext() const noexcept 
        {
            return pipe_->hasNext();
        }

        value_type next() {
            return pipe_->dequeue();
        }
        
        void close() const {
            pipe_->forceClose();
        }

    private:

        pipe_type pipe_;
    };

    //========================================================

    // TODO: create a "Source" class to encapsulate an input to a pipe, so ">>"
    // can be used safely
    /**
     * The Source encapsulates the input into a pipeline. It acts as a thing
     * functionality wrapper around an iterable, but allows passing of extra
     * information along with it (like a monad).  It also allows a selective
     * entry point for the combinators below, like (>>) and connect, so that
     * the API accepts specific types, and not everything under the sun.
     */
    template <typename InputIterable> class Source;

    template <typename InputIterable>
    class Source
    {
        typedef InputIterable wrapped_type;

        boost::optional<ISource<InputIterable>> impl_;

    public:
        typedef Source<wrapped_type> const_iterator;
        typedef typename ISource<wrapped_type>::value_type value_type;
        typedef value_type* pointer;
        typedef value_type& reference;
        typedef std::input_iterator_tag iterator_category;
        typedef void difference_type;

        Source()
            : impl_()
        { }

        Source(Source const& other)
            : impl_(other.impl_)
        { }

        Source(Source&& other)
            : impl_(std::move(other.impl_))
        { }

        Source(InputIterable& iterable)
            : impl_(iterable)
        { }

        Source(InputIterable&& iterable)
            : impl_(std::move(iterable))
        { }

        // TODO: make static const end iterator

        const_iterator& begin() { return *this; }
        const_iterator  end()   { return const_iterator(); }
        const_iterator& cbegin() { return *this; }
        const_iterator  cend()   { return const_iterator(); }

        const_iterator& operator ++ ()    { return *this; } ///< noop
        const_iterator& operator ++ (int) { return *this; } ///< noop

        /**
         * To fullfil the input_const_iterator category, both returns the 
         * the next element and advances the inner const_iterator
         */
        value_type operator * ()    { return impl_->next(); }

        bool operator == (const_iterator const& other) const
        {
            if (this == &other)
            {
                return true;
            }

            auto* a = &this->impl_;
            auto* b = &other.impl_;

            if (!(*a))
            {
                std::swap(a, b);
            }
        
            // a is surely not empty at this point
            assert(!!(*a));

            // an "end" const_iterator is:
            // - either the default constructed const_iterator (optional is empty)
            // - or has reached the end of iteration (hasNext() returns false)
            return (!(*b)) && !(*a)->hasNext();
        }

        bool operator != (const_iterator const& other) const { return !(*this == other); }

        // TODO: ?
        ISource<wrapped_type>& impl() { return *impl_; }

    };

    //========================================================

    template <typename T>
    struct is_source : std::false_type { };

    template <typename T>
    struct is_source<Source<T>> : std::true_type { };

    //========================================================

    template <typename InputIterable>
    Source<typename std::remove_reference<InputIterable>::type>
    makeSource(InputIterable&& iterable)
    {
        return Source<typename std::remove_reference<InputIterable>::type>(iterable);
    }

    //========================================================

    /**
     * Type alias that encapsulates the output of Pipe<Expected<T>
     */
    template <typename T>
    using Sink = Source< std::shared_ptr<Pipe<Expected<T>>> >;

    //========================================================

    namespace detail
    {

        /**
         * A wrapper for better forwarding to async lambdas. Prevents a copy,
         * if T is an lvalue reference (passes a pointer instead).
         *
         * See http://stackoverflow.com/questions/13813838/perfect-forwarding-to-async-lambda
         * for details on the problem.
         */
        template <typename T> struct async_forwarder;

        template <typename T>
        class async_forwarder
        {
            T val_;

        public:
            async_forwarder(T&& t) : val_(std::move(t)) { }

            async_forwarder(async_forwarder const& other) = delete;
            async_forwarder(async_forwarder&& other)
                : val_(std::move(other.val_)) { }

            operator T&& ()       { return std::move(val_); }
            operator T&& () const { return std::move(val_); }
        };

        template <typename T>
        class async_forwarder<T&>
        {
            T& val_;

        public:
            async_forwarder(T& t) : val_(t) { }

            async_forwarder(async_forwarder const& other) = delete;
            async_forwarder(async_forwarder&& other)
                : val_(other.val_) { }

            operator T&       ()       { return val_; }
            operator T const& () const { return val_; }
        };

        //========================================================

        /**
         * A traits struct to figure out the final return type of a pipeline
         * of functions, given a starting type T and one or more callable types.
         *
         * Essentially carries out a fold, applying the functions in order on T,
         * to obtain the final type
         */
        template <typename T, typename... Funcs>
        struct connect_traits;

        /**
         * The base case of a single type.
         */
        template <typename T>
        struct connect_traits<T>
        {
            typedef T return_type;
            typedef Sink<return_type> monadic_type;
        };

        /**
         * Specialization for void return: a future.
         */
        template <>
        struct connect_traits<void>
        {
            typedef void return_type;
            typedef std::future<void> monadic_type;
        };
        
        /**
         * The recursive case, where the first return_type is figured out, and
         * passed along.
         */
        template <typename T, typename Func, typename... Funcs>
        struct connect_traits<T, Func, Funcs...>
        {
        private:
            // figure out the return type of the function
            typedef decltype( std::declval<Func>()( std::declval<T&>() )) T2;

            // fold
            typedef connect_traits<T2, Funcs...> helper_type;
        public:
            typedef typename helper_type::return_type return_type;
            typedef typename helper_type::monadic_type monadic_type;
        };

        //========================================================

        template <typename Output>
        struct connect_impl
        {

            template <typename InputIterable, typename Func>
            static Sink<Output> connect(InputIterable&& source, Func func, size_t capacity = 3)
            {
                auto pipe = std::make_shared<Pipe<Expected<Output>>>(capacity);

                // start processing thread
                std::thread processingThread(
                        [pipe, func](InputIterable&& source) mutable
                        {
                            for (auto&& e : source) {
                                pipe->enqueue(func(e));
                            }
                            pipe->close();
                            //while (source.hasNext())
                            //{
                                //// TODO: use scope guard
                                //try {
                                    //if ( pipe->enqueue(func(source.dequeue())) )
                                    //{
                                        //break;
                                    //}
                                //}
                                //catch (...) {
                                    //pipe->enqueue(Expected<Output>::fromException());
                                    //source->close();
                                //}
                            //}
                            //pipe->close();
                        },
                        detail::async_forwarder<InputIterable>(std::forward<InputIterable>(source))
                );

                processingThread.detach(); // TODO: shouldn't detach?

                return Sink<Output>(pipe);
            }
        };

        template <>
        struct connect_impl<void>
        {

            /**
             * Specialization for functions returning void.
             */
            template <typename InputIterable, typename Func>
            static std::future<void> connect(InputIterable&& input, Func func)
            {
                // start processing thread
                return std::async(std::launch::async,
                        [func](InputIterable&& input) mutable
                        {
                            for (auto&& e : input) {
                                func(e);
                            }
                        },
                        detail::async_forwarder<InputIterable>(std::forward<InputIterable>(input))
                );
            }
        };

    }

    /**
     * @note need to specialize based on output of the function passed in,
     * so need another layer of indirection to connect_impl.
     *
     * @note functions passed in have to take arguments either by T&& or Tconst&,
     * in other words input arguments must be able to bind to rvalues.
     */
    template <typename InputIterable, typename Func>
    auto connect(InputIterable&& input, Func func)
    ->  typename detail::connect_traits<
                typename std::remove_reference<InputIterable>::type::const_iterator::value_type,
                Func
        >::monadic_type
    {


        typedef typename detail::connect_traits<
                    typename std::remove_reference<InputIterable>::type::const_iterator::value_type,
                    Func
                >::return_type return_type;

        return detail::connect_impl<return_type>::connect(std::forward<InputIterable>(input), func);
    }

    // TODO: forward funcs too
    template <typename InputIterable, typename Func, typename Func2, typename... Funcs>
    auto connect(InputIterable&& input, Func func, Func2 func2, Funcs... funcs)
    ->  typename detail::connect_traits<
            typename std::remove_reference<InputIterable>::type::const_iterator::value_type,
            Func,
            Func2,
            Funcs...
        >::monadic_type
    {
        typedef typename detail::connect_traits<
                    typename std::remove_reference<InputIterable>::type::const_iterator::value_type,
                    Func
                >::return_type return_type;


        return connect( connect(std::forward<InputIterable>(input), func), func2, funcs... );
    }

    template <typename S, typename Func>
    inline auto operator >> (S&& input, Func func)
    -> decltype(connect(std::forward<S>(input), func))
    {
        static_assert( is_source<S>::value,
                "Cannot chain filters to a non-Source type. "
                "Make sure left argument of (>>) is/returns a Source." );

        return connect(std::forward<S>(input), func);
    }

    // TODO: enforce const& for func, so callable objects passed in do not
    // rely on mutable state (?) in case the same function is reused on
    // two separate threads. If not feasible, then passing by value may be
    // the only way

    // TODO: create "bind" that aggregates functions right to left, producing a
    // pipeline. When the pipeline is bound to an iterable, all threads etc.
    // are instantiated as necessary
    // Haskell's >>= ?
    
}

#endif /* end of include guard: PLUMBING_H_IEGJRLCP */
