#ifndef PLUMBING_H_IEGJRLCP
#define PLUMBING_H_IEGJRLCP

//      Copyright Alexander Kondratskiy 2012 - 2013.
// Distributed under the Boost Software License, Version 1.0.
//    (See accompanying file LICENSE_1_0.txt or copy at
//          http://www.boost.org/LICENSE_1_0.txt)

#include <cassert>

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
        bool enqueue(T const& e)
        {
            // inexpensive check before acquiring lock
            if (!open_) { return false; }

            std::unique_lock<std::mutex> lock(mutex_);
            while(!writeHeadroom())
            {
                readyForWrite_.wait(lock);
            }

            // check if we can write again, since it could have changed
            if (!open_) { return false; }

            fifo_[write_] = e;
            incrementWrite();

            readyForRead_.notify_one();

            return true;
        }

        // TODO: make nothrow
        void close()
        {
            std::unique_lock<std::mutex> lock(mutex_);
            while(!writeHeadroom())
            {
                readyForWrite_.wait(lock);
            }

            fifo_[write_] = boost::optional<T>(boost::none);
            incrementWrite(); // give space to read

            readyForRead_.notify_all();

            open_ = false; // hint for write side
        }

        /**************************************
         *  Facilities for reading from pipe  *
         **************************************/
        
        bool hasNext()
        {
            std::unique_lock<std::mutex> lock(mutex_);
            while(!readHeadroom())
            {
                readyForRead_.wait(lock);
            }

            return fifo_[read_];
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

    // Sink details
    namespace detail
    {

        template <typename InputIterable>
        struct sink_traits
        {
            typedef InputIterable type;
            typedef typename InputIterable::iterator iterator;
            typedef typename std::iterator_traits<iterator>::value_type value_type;
        };

        template <typename T>
        class SinkImplBase
        {
        public:
            typedef T value_type;

            virtual ~SinkImplBase () { }

            virtual bool hasNext() = 0;
            virtual T next() = 0;
        
        };

        /**
         * @note: InputIterable means has .begin() and .end()
         * that return InputIterator
         */
        template <typename InputIterable>
        class SinkImpl : public SinkImplBase<typename sink_traits<InputIterable>::value_type>
        {
            typedef SinkImpl<InputIterable> type;
            typedef typename sink_traits<InputIterable>::iterator iterator;
            typedef typename sink_traits<InputIterable>::value_type value_type;
            iterator current_;
            iterator end_;

        public:

            SinkImpl(InputIterable& iterable)
                : current_(iterable.begin()),
                  end_(iterable.end())
            { }

            ~SinkImpl() { }

            bool hasNext()
            {
                return current_ != end_;
            }

            value_type next()
            {
                return *current_++;
            }
        };

        /**
         * Template specialization of sink_traits for Pipes
         */
        template <>
        template <typename T>
        struct sink_traits< std::shared_ptr<Pipe<T>> >
        {
            typedef std::shared_ptr<Pipe<T>> type;
            typedef void iterator;
            typedef T value_type;
        };

        /**
         * Template specialization for Pipes
         */
        template <>
        template <typename T>
        class SinkImpl<std::shared_ptr<Pipe<T>>> : public SinkImplBase<T>
        {
            typedef std::shared_ptr<Pipe<T>> pipe_type;
            typedef SinkImpl<pipe_type> type;
            typedef T value_type;
            pipe_type pipe_;

        public:

            SinkImpl(pipe_type const& pipe)
                : pipe_(pipe)
            { }

            ~SinkImpl() { }

            bool hasNext()
            {
                return pipe_->hasNext();
            }

            value_type next()
            {
                return pipe_->dequeue();
            }
        };
    }

    template <typename T>
    class Sink
    {
        std::shared_ptr<detail::SinkImplBase<T>> pimpl;

    public:
        typedef T value_type;
        typedef T* pointer;
        typedef T& reference;
        typedef Sink<T> iterator;
        typedef std::input_iterator_tag iterator_category;
        typedef void difference_type;

        Sink(Sink<T> const& other) = default;
        Sink(Sink<T>&& other) = default;

        /**
         * Main constructor that uses type erasure to encapsulate an iterable
         * object, with a single type of iterator.
         *
         * @note use std::enable_if and SFINAE to disable the constructor
         * for itself, otherwise this constructor gets interpreted as the copy
         * constructor, and we get into an infinite loop of creating a new Sink
         * from itself.
         */
        template <
            typename InputIterable,
            typename std::enable_if<
                !std::is_same<InputIterable, Sink<T>>::value, int
            >::type = 0
        >
        Sink(InputIterable& iterable)
            : pimpl(new detail::SinkImpl<InputIterable>(iterable))
        { }

        /**
         * Default constructor, creates an "end" iterator
         */
        Sink() : pimpl(nullptr) { }

        iterator& begin() { return *this; }
        iterator  end()   { return iterator(); }

        iterator& operator ++ ()    { return *this; } ///< noop
        iterator& operator ++ (int) { return *this; } ///< noop

        /**
         * To fullfil the input_iterator category, both returns the 
         * the next element and advances the inner iterator
         */
        value_type operator * ()    { return pimpl->next(); }

        bool operator == (iterator& other)
        {
            detail::SinkImplBase<T>* a = this->pimpl.get();
            detail::SinkImplBase<T>* b = other.pimpl.get();
            
            if (a == b)
            {
                return true;
            }

            if (!a)
            {
                std::swap(a, b);
            }
        
            // a is surely not null at this point.
            assert(a);

            // an "end" iterator is:
            // - either the default constructed iterator (pimpl is nullptr)
            // - or has reached the end of iteration (hasNext() returns false)
            return !(b || a->hasNext());
        }

        bool operator != (iterator& other) { return !(*this == other); }
    };

    /**
     * The Source encapsulates the input into a pipeline. It acts as a thing
     * functionality wrapper around an iterable, but allows passing of extra
     * information along with it (like a monad).  It also allows a selective
     * entry point for the combinators below, like (>>) and connect, so that
     * the API accepts specific types, and not everything under the sun.
     */
    template <typename InputIterable>
    class Source;

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
            static Sink<Output> connect(InputIterable&& input, Func func, size_t capacity = 3)
            {
                std::shared_ptr<Pipe<Output>> pipe(new Pipe<Output>(capacity));

                // start processing thread
                std::thread processingThread(
                        [pipe, func](InputIterable&& input) mutable
                        {
                            for (auto&& e : input) {
                                pipe->enqueue(func(e));
                            }
                            pipe->close();
                        },
                        detail::async_forwarder<InputIterable>(std::forward<InputIterable>(input))
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
                typename std::remove_reference<InputIterable>::type::iterator::value_type,
                Func
        >::monadic_type
    {


        typedef typename detail::connect_traits<
                    typename std::remove_reference<InputIterable>::type::iterator::value_type,
                    Func
                >::return_type return_type;

        return detail::connect_impl<return_type>::connect(std::forward<InputIterable>(input), func);
    }

    // TODO: forward funcs too
    template <typename InputIterable, typename Func, typename Func2, typename... Funcs>
    auto connect(InputIterable&& input, Func func, Func2 func2, Funcs... funcs)
    ->  typename detail::connect_traits<
            typename std::remove_reference<InputIterable>::type::iterator::value_type,
            Func,
            Func2,
            Funcs...
        >::monadic_type
    {
        typedef typename detail::connect_traits<
                    typename std::remove_reference<InputIterable>::type::iterator::value_type,
                    Func
                >::return_type return_type;

        return connect( connect(std::forward<InputIterable>(input), func), func2, funcs... );
    }

    /**
     * @note: Perhaps this operator is too generically templated, and would "poison"
     * the code it is imported into?
     */
    template <typename In, typename Func>
    inline auto operator >> (Sink<In>& input, Func func)
    -> decltype(connect(input, func))
    {
        return connect(input, func);
    }

    template <typename In, typename Func>
    inline auto operator >> (Sink<In>&& input, Func func)
    -> decltype(connect(std::move(input), func))
    {
        return connect(std::move(input), func);
    }

    template <typename InputIterable>
    Sink<typename detail::sink_traits<typename std::remove_reference<InputIterable>::type>::value_type>
    MakeSink(InputIterable&& iterable)
    {
        return Sink<
            typename detail::sink_traits<
                typename std::remove_reference<InputIterable>::type
            >::value_type
        >(iterable);
    }

    // TODO: enforce const& for func, so callable objects passed in do not
    // rely on mutable state (?) in case the same function is reused on
    // two separate threads. If not feasible, then passing by value may be
    // the only way

    // TODO: create "bind" that aggregates functions right to left, producing a
    // pipeline. When the pipeline is bound to an iterable, all threads etc.
    // are instantiated as necessary
    // Haskell's >>= ?
    
    // TODO: create a "Pump" class to encapsulate an input to a pipe, so ">>"
    // can be used safely

}

#endif /* end of include guard: PLUMBING_H_IEGJRLCP */
