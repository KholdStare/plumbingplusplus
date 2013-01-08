#ifndef PLUMBING_H_IEGJRLCP
#define PLUMBING_H_IEGJRLCP

//      Copyright Alexander Kondratskiy 2012 - 2013.
// Distributed under the Boost Software License, Version 1.0.
//    (See accompanying file LICENSE_1_0.txt or copy at
//          http://www.boost.org/LICENSE_1_0.txt)

#include "expected.hpp"
#include "pipe.hpp"
#include "source.hpp"

#include <cassert>

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

    //========================================================

    template <typename T>
    class PipeSink
    {
    public:
        typedef T value_type;
        typedef std::shared_ptr<Pipe<value_type>> pipe_type; // TODO: could be just a reference?
        typedef value_type return_type;

        typedef PipeSink<T> iterator;
        typedef value_type* pointer;
        typedef value_type& reference;
        typedef std::output_iterator_tag iterator_category;
        typedef void difference_type;

        PipeSink()
            : pipe_()
        { }

        PipeSink(pipe_type const& pipe)
            : pipe_(pipe)
        { }

        PipeSink(pipe_type&& pipe)
            : pipe_(std::move(pipe))
        { }

        PipeSink(PipeSink&&) = default;
        PipeSink(PipeSink const&) = default;

        iterator& begin() { return *this; }
        iterator  end()   { return iterator(); }

        iterator& operator ++ ()    { return *this; } ///< noop
        iterator& operator ++ (int) { return *this; } ///< noop

        iterator& operator * ()     { return *this; } ///< noop

        template <typename U>
        void operator = (U&& val)
        {
            // TODO: maybe throw exception if closed
            if (!pipe_->enqueue(std::forward<U>(val)))
            {
                throw std::range_error("Tried to enqueue into a closed pipe");
            }
        }

    private:
        pipe_type pipe_;
    };

    /**
     * Convenience method to construct a PipeSink<T> from the intake of the
     * pipe.
     */
    template <typename T>
    PipeSink<T>
    makePipeSink(std::shared_ptr<Pipe<T>> const& pipe)
    {
        return PipeSink<T>(pipe);
    }

    //========================================================
    
    namespace detail
    {
        
        /**
         * A thin wrapper around a function object, to help out with
         * input/output type deduction, when connecting functions in a pipeline
         */
        template <
                  typename In,
                  typename Out,
                  class FuncObject ///< function object with templated operator()
                 >
        class iterator_filter
        {

            /**
             * Stores function that takes input iterators and output iterator.
             */
            FuncObject func_;

        public:
            typedef In input_type;
            typedef Out return_type;

            // TODO: if (>>) et al. are move enabled, this can be deleted
            iterator_filter(iterator_filter const&) = default;
            iterator_filter(iterator_filter&&) = default;

            iterator_filter(FuncObject const& func) 
                : func_(func)
            { }

            iterator_filter(FuncObject&& func) 
                : func_(std::move(func))
            { }

            template <typename InputItFirst, typename InputItLast, typename OutputIt>
            inline void operator() (InputItFirst&& in_first, InputItLast&& in_last, OutputIt&& out_first)
            {
                static_assert( std::is_same<typename std::remove_reference<InputItFirst>::type,
                                            typename std::remove_reference<InputItLast>::type>::value,
                               "Input iterator types for begin/end must match." );

                func_(std::forward<InputItFirst>(in_first),
                      std::forward<InputItLast>(in_last),
                      std::forward<OutputIt>(out_first));
            }

        };

        
    } /* detail */ 

    template <typename In, typename Out, class FuncObject>
    detail::iterator_filter<In, Out, typename std::remove_reference<FuncObject>::type >
    makeIteratorFilter(FuncObject&& func)
    {
        return detail::iterator_filter<In,
                                       Out,
                                       typename std::remove_reference<FuncObject>::type
                                      >( std::forward<FuncObject>(func) );
    }

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
            typedef PipeSource<return_type> monadic_type;
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


        /**
         * Specialization for iterator_filter
         */
        template <typename T, typename In, typename Out, class FuncObject, typename... Funcs>
        struct connect_traits<T, iterator_filter<In, Out, FuncObject>, Funcs...>
        {
        private:
            typedef iterator_filter<In, Out, FuncObject> filter_type;
            // figure out the return type of the function
            typedef typename filter_type::return_type T2;

            // fold
            typedef connect_traits<T2, Funcs...> helper_type;
        public:
            typedef typename helper_type::return_type return_type;
            typedef typename helper_type::monadic_type monadic_type;
        };

        //========================================================

        template <typename S, typename In, typename Func, typename Out>
        struct thread_task
        {
            typedef std::shared_ptr<Pipe<Expected<Out>>> pipe_type;

            /**
             * Task to be performed by the dedicated thread
             */
            static void invoke(S&& source, Func func, pipe_type const& pipe)
            {
                while (source.impl().hasNext())
                {
                    // TODO: use scope guard
                    try
                    {
                        if ( !pipe->enqueue(func(source.impl().next())) )
                        {
                            break;
                        }
                    }
                    catch (...)
                    {
                        pipe->enqueue(Expected<Out>::fromException());
                        break;
                    }
                }
                pipe->close();
                source.impl().close();
            }
        };

        /**
         * Specialization for Expected<T> input.
         */
        template <typename S, typename In, typename Func, typename Out>
        struct thread_task<S, Expected<In>, Func, Out>
        {
            typedef std::shared_ptr<Pipe<Expected<Out>>> pipe_type;

            /**
             * Task to be performed by the dedicated thread
             */
            static void invoke(S&& source, Func func, pipe_type const& pipe)
            {
                while (source.impl().hasNext())
                {
                    Expected<In> e = source.impl().next();
                    if (!e.valid())
                    {
                        pipe->enqueue(Expected<Out>::transferException(e));
                        break;
                    }

                    // TODO: use scope guard
                    try
                    {
                        if ( !pipe->enqueue(func(e.get())) )
                        {
                            break;
                        }
                    }
                    catch (...)
                    {
                        pipe->enqueue(Expected<Out>::fromException());
                        break;
                    }
                }

                pipe->close();
                source.impl().close();
            }
        };

        /**
         * Another thread_task struct that is a special case for iterator filters
         */
        template <typename S, typename Out, typename IterFilter>
        struct thread_task_iter
        {
            // TODO: add check that in/out are convertible to iterfilter in/out

            static_assert( std::is_convertible<Out, typename IterFilter::return_type>::value,
                           "Make sure input/output types are correctly specified"
                           "on the iterator filter.");

            typedef IterFilter filter_type;
            typedef std::shared_ptr<Pipe<Expected<Out>>> pipe_type;

            /**
             * Task to be performed by the dedicated thread
             */
            static void invoke(S&& source, filter_type func, pipe_type const& pipe)
            {
                auto intake = makePipeSink(pipe);
                // TODO: use scope guard
                try
                {
                    // call the iterator filter.
                    // if the internal function tries to write to a closed
                    // pipe, such as in the situation where an exception was
                    // thrown downstream already, another exception will be thrown
                    // by the intake iterator, and be "swallowed" below.
                    func(source.begin(), source.end(), intake.begin());
                }
                catch (...)
                {
                    pipe->enqueue(Expected<Out>::fromException());
                }

                pipe->close();
                source.impl().close();
            }
        };

        /**
         * Specialization for iterator_filter function object.
         *
         * @note The indirection is necessary because we need to define this
         * twice, once for input type In, and another for Expected<In>. Without
         * the Expected<In> specialization, the compiler can't disambiguate
         * between two template specializations, (one for just Expected<In> and
         * just iterator_filter). By specializing for both Expected<In> and
         * iterator_filter at the same time, no ambiguities arise. However,
         * duplication arises, hence another layer of indirection by inheriting
         * from the actual implementation, which works the same way for type In
         * and Expected<In>.
         */
        template <typename S, typename In, typename Out,
                  typename IterFilterIn, typename IterFilterOut, typename FuncObject>
        struct thread_task<S, In, iterator_filter<IterFilterIn, IterFilterOut, FuncObject>, Out>
               : thread_task_iter<S, Out, iterator_filter<IterFilterIn, IterFilterOut, FuncObject>>
        { };

        template <typename S, typename In, typename Out,
                  typename IterFilterIn, typename IterFilterOut, typename FuncObject>
        struct thread_task<S, Expected<In>, iterator_filter<IterFilterIn, IterFilterOut, FuncObject>, Out>
               : thread_task_iter<S, Out, iterator_filter<IterFilterIn, IterFilterOut, FuncObject>>
        { };

        //========================================================

        template <typename In, typename Out>
        struct connect_impl
        {
            typedef std::shared_ptr<Pipe<Expected<Out>>> pipe_type;

            template <typename S, typename Func>
            static Source<pipe_type> connect(S&& source, Func func, size_t capacity = 10)
            {
                static_assert( is_source<S>::value,
                        "Cannot chain filters to a non-Source type. "
                        "Connect implementation requires a Source as input." );

                auto pipe = std::make_shared<Pipe<Expected<Out>>>(capacity);

                // start processing thread
                std::thread processingThread(
                        thread_task<S, In, Func, Out>::invoke,
                        detail::async_forwarder<S>(std::forward<S>(source)),
                        func,
                        pipe
                );

                processingThread.detach(); // TODO: shouldn't detach?

                return makeSource(std::move(pipe));
            }

        };

        template <typename In>
        struct connect_impl<In, void>
        {

            /**
             * Specialization for functions returning void.
             */
            template <typename S, typename Func>
            static std::future<void> connect(S&& input, Func func)
            {
                // start processing thread
                return std::async(std::launch::async,
                        [func](S&& input) mutable
                        {
                            for (auto&& e : input) {
                                func(e);
                            }
                        },
                        detail::async_forwarder<S>(std::forward<S>(input))
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
    template <typename S, typename Func>
    auto connect(S&& input, Func func)
    ->  typename detail::connect_traits<
                typename std::remove_reference<S>::type::const_iterator::value_type,
                Func
        >::monadic_type
    {

        static_assert( is_source<S>::value,
                "Cannot chain filters to a non-Source type. "
                "Make sure the first argument to connect is a Source." );

        typedef typename std::remove_reference<S>::type::value_type input_type;
        typedef typename detail::connect_traits<
                    input_type,
                    Func
                >::return_type return_type;

        return detail::connect_impl<input_type, return_type>::connect(std::forward<S>(input), func);
    }

    // TODO: forward funcs too
    template <typename S, typename Func, typename Func2, typename... Funcs>
    auto connect(S&& input, Func func, Func2 func2, Funcs... funcs)
    ->  typename detail::connect_traits<
            typename std::remove_reference<S>::type::const_iterator::value_type,
            Func,
            Func2,
            Funcs...
        >::monadic_type
    {
        static_assert( is_source<S>::value,
                "Cannot chain filters to a non-Source type. "
                "Make sure the first argument to connect is a Source." );

        typedef typename detail::connect_traits<
                    typename std::remove_reference<S>::type::value_type,
                    Func
                >::return_type return_type;


        return connect( connect(std::forward<S>(input), func), func2, funcs... );
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
