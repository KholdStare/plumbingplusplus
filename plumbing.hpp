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
        
        void enqueue(T const& e)
        {
            std::unique_lock<std::mutex> lock(mutex_);
            while(!writeHeadroom())
            {
                readyForWrite_.wait(lock);
            }

            fifo_[write_] = e;
            incrementWrite();

            readyForRead_.notify_one();
        }

        // TODO: have to look out for trying to enqueue after closing the pipe
        // perhaps throw exception?
        void close()
        {
            std::unique_lock<std::mutex> lock(mutex_);
            while(!writeHeadroom())
            {
                readyForWrite_.wait(lock);
            }

            fifo_[write_] = boost::optional<T>(boost::none);
            incrementWrite();

            readyForRead_.notify_all();
        }

        /**************************************
         *  Facilities for reading from pipe  *
         **************************************/
        
        bool isOpen()
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
        // the isOpen() check, one reads an actual value, while the other
        // might try to read when the pipe is already closed.
        T dequeue()
        {
            std::unique_lock<std::mutex> lock(mutex_);
            while(!readHeadroom())
            {
                readyForRead_.wait(lock);
            }

            // TODO std::move
            T const& e = *fifo_[read_];
            incrementRead();

            readyForWrite_.notify_one();

            return std::move(e);
        }
    };

    template <typename T>
    class Sink
    {
        std::shared_ptr<Pipe<T>> pipe_;

    public:
        typedef T value_type;
        typedef T* pointer;
        typedef T& reference;
        typedef Sink<T> iterator;
        typedef std::input_iterator_tag iterator_category;
        typedef void difference_type; // TODO: is this ok?

        Sink(Sink<T> const& other) = default;
        Sink(Sink<T>&& other)
            : pipe_(other.pipe_)
        { } // TODO: look into shared_ptr move

        Sink(std::shared_ptr<Pipe<T>> const& pipe)
            : pipe_(pipe)
        { }

        /**
         * Default constructor, creates an "end" iterator
         */
        Sink() : pipe_(nullptr) { }

        iterator& begin() { return *this; }
        iterator  end()   { return iterator(); }

        iterator& operator ++ ()    { return *this; } ///< noop
        iterator& operator ++ (int) { return *this; } ///< noop

        /**
         * To fulfill the input_iterator category, both returns the 
         * the next element and advances the inner iterator
         */
        value_type operator * ()    { return pipe_->dequeue(); }

        bool operator == (iterator& other)
        {
            Pipe<T>* a = this->pipe_.get();
            Pipe<T>* b = other.pipe_.get();
            
            if (a == b)
            {
                return true;
            }

            if (!a)
            {
                std::swap(a, b);
            }

            // an "end" iterator is:
            // - either the default constructed iterator (pipe_ is nullptr)
            // - or has reached the end of iteration (isOpen() returns false)
            return !(b || a->isOpen());
        }

        bool operator != (iterator& other) { return !(*this == other); }
    };

    namespace detail
    {

        /**
         * A type trait to be used for perfect forwarding to a lambda
         * in an async call. A copy is inevitable if passing by lvalue
         * reference, so the receiving type is engineered so forwarding
         * incurs the least amount of copies/moves.
         *
         * See http://stackoverflow.com/questions/13813838/perfect-forwarding-to-async-lambda
         * for details on the problem. This is a temporary solution.
         * TODO: figure out a "good" solution.
         */
        template <typename T> struct async_forward;

        template <typename T>
        struct async_forward
        {
            typedef T&& type;
        };

        template <typename T>
        struct async_forward<T&>
        {
            typedef T type;
        };

        //========================================================

        /**
         * A traits struct to figure out the final return type of a pipeline
         * of functions, given a starting type T and one or more callable types.
         *
         * Essentially carries out a fold, applying the functions in order on T,
         * to obtain the final type
         */
        template <typename InputIterable, typename... Funcs>
        struct connect_traits;

        /**
         * The base case of a single type.
         */
        template <typename InputIterable>
        struct connect_traits<InputIterable>
        {
            typedef decltype( *std::declval<InputIterable>().begin() ) return_type;
            typedef Sink<return_type> monadic_type;
        };

        /**
         * Specialization for void return: a future.
         */
        template <>
        struct connect_traits<Sink<void>>
        {
            typedef void return_type;
            typedef std::future<void> monadic_type;
        };
        
        /**
         * The recursive case, where the first return_type is figured out, and
         * passed along.
         */
        template <typename InputIterable, typename Func, typename... Funcs>
        struct connect_traits<InputIterable, Func, Funcs...>
        {
        private:
            // figure out the return type of the function
            typedef decltype( std::declval<Func>()( *std::declval<InputIterable>().begin() )) T;

            // fold
            typedef connect_traits<Sink<T>, Funcs...> helper_type;
        public:
            typedef typename helper_type::return_type return_type;
            typedef typename helper_type::monadic_type monadic_type;
        };

        //========================================================

        template <typename Output>
        struct connect_impl
        {

            template <typename InputIterable, typename Func>
            static Sink<Output> connect(InputIterable&& input, Func func)
            {
                std::shared_ptr<Pipe<Output>> pipe(new Pipe<Output>(2)); // TODO make this customizable

                typedef typename detail::async_forward<InputIterable>::type async_type;

                // start processing thread
                std::thread processingThread(
                        [pipe, func](async_type input) mutable
                        {
                            for (auto it = input.begin(), end = input.end(); it != end; ++it)
                            {
                                auto&& result = func(*it);
                                pipe->enqueue(result);
                            }
                            pipe->close();
                        },
                        std::forward<InputIterable>(input)
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
                typedef typename detail::async_forward<InputIterable>::type async_type;

                // start processing thread
                return std::async(std::launch::async,
                        [func](async_type input) mutable
                        {
                            // using iterators directly, and not range-based for,
                            // because want to pass *iterator directly to function,
                            // without first binding it to an lvalue.
                            for (auto it = input.begin(), end = input.end(); it != end; ++it)
                            {
                                func(*it);
                            }
                        },
                        std::forward<InputIterable>(input)
                );
            }
        };

    }

    // TODO: make connect a member function of sink
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
                InputIterable,
                Func
        >::monadic_type
    {
        typedef typename detail::connect_traits<
                    InputIterable,
                    Func
                >::return_type return_type;

        return detail::connect_impl<return_type>::connect(std::forward<InputIterable>(input), func);
    }

    // TODO: forward funcs too
    template <typename InputIterable, typename Func, typename Func2, typename... Funcs>
    auto connect(InputIterable&& input, Func func, Func2 func2, Funcs... funcs)
    ->  typename detail::connect_traits<
            InputIterable,
            Func,
            Func2,
            Funcs...
        >::monadic_type
    {
        typedef typename detail::connect_traits<
                    InputIterable,
                    Func
                >::return_type return_type;

        return connect( connect(std::forward<InputIterable>(input), func), func2, funcs... );
    }

    /**
     * @note: Perhaps this operator is too generically templated, and would "poison"
     * the code it is imported into?
     */
    template <typename InputIterable, typename Func>
    inline auto operator >> (InputIterable&& input, Func func)
    -> decltype(connect(std::forward<InputIterable>(input), func))
    {
        return connect(std::forward<InputIterable>(input), func);
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
