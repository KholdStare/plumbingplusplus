#ifndef SOURCE_HPP_IBUOPHGC
#define SOURCE_HPP_IBUOPHGC

//      Copyright Alexander Kondratskiy 2012 - 2013.
// Distributed under the Boost Software License, Version 1.0.
//    (See accompanying file LICENSE_1_0.txt or copy at
//          http://www.boost.org/LICENSE_1_0.txt)
//

#include "pipe.hpp"

#include <type_traits>
#include <memory>

namespace Plumbing
{

    template <typename InputIterable> class ISource;

    template <typename InputIterable>
    class ISource
    {
        typedef InputIterable wrapped_type;

    public:
        typedef typename wrapped_type::const_iterator const_iterator;
        typedef typename const_iterator::value_type value_type;
        typedef decltype( *std::declval<const_iterator>() ) return_type;

        // TODO: consider requiring default constructor for a "null" value
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

        return_type next() {
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
        typedef value_type return_type;

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
        typedef typename ISource<wrapped_type>::return_type return_type;
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
        return_type operator * ()    { return impl_->next(); }

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

    template <typename T>
    struct is_source<Source<T>&> : std::true_type { };

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
    using PipeSource = Source< std::shared_ptr<Pipe<Expected<T>>> >;

}

#endif /* end of include guard: SOURCE_HPP_IBUOPHGC */

