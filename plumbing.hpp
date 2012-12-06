// ideas for "couplings" and fifos between transformations

#ifndef PLUMBING_H_IEGJRLCP
#define PLUMBING_H_IEGJRLCP

#include <cassert>

#include <vector>
#include <mutex>
#include <condition_variable>

#include <iterator>
#include <memory>
#include <functional>
#include <type_traits>

#include <boost/optional.hpp>

// used if number of inputs/outputs is not one-to-one.
// e.g. 3 images in, 1 image out (hdr)
// e.g. take two numbers and sum
/*
template <typename InIter, typename OutIter>
void iteratorTransformation(InIter first, InIter last, OutIter out);
*/

// perhaps restrict on postcrement?
/*
void sumTwo(int* first, int* last, int* out)
{
    while( first != last )
    {
        *out = *first++;
        *out += *first++;
        
        ++out;
    }
}
*/

// used when transformation creates a signle output from a single output.
/*
template <typename InType, typename OutType>
OutType tranformation(InType const& in);
*/

// is a transformation
/*
float convertToFloat(int in)
{
    return static_cast<float>(in);
}
*/

namespace Plumbing
{
    template <typename T>
    class Pipe
    {
        // TODO: dynamically resize fifo according to demand?

        // TODO: perhaps create a different queue which is "infinite" (a linked list),
        //       but provides a way to "stall" it on a predicate (e.g. memory usage)

        std::vector<T> fifo_;
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
            return fifo_.size() - ( (write_ - read_) % fifo_.size() ) - 1;
        }

        inline void incrementWrite()
        {
            write_ = (write_ + 1) % fifo_.size();
        }

        inline void incrementRead()
        {
            read_ = (read_ + 1) % fifo_.size();
        }

        /**
         * Return the number of free slots available for reading
         */
        inline int readHeadroom()
        {
            return (write_ - read_) % fifo_.size();
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

        template <class... Args>
        void emplace(Args&&... args)
        {
            std::unique_lock<std::mutex> lock(mutex_);
            while(!writeHeadroom())
            {
                readyForWrite_.wait(lock);
            }

            fifo_.emplace(fifo_.cbegin() + write_, std::forward<Args>(args)...);
            incrementWrite();

            readyForRead_.notify_one();
        }

        // TODO: close currently closes the pipe immediately, even if the read side
        // has not read all the remaining items. Have to enqueue "close value"
        void close()
        {
            std::unique_lock<std::mutex> lock(mutex_);
            while(!writeHeadroom())
            {
                readyForWrite_.wait(lock);
            }

            open_ = false;
            readyForRead_.notify_one();
        }

        /**************************************
         *  Facilities for reading from pipe  *
         **************************************/
        
        bool isOpen()
        {
            std::unique_lock<std::mutex> lock(mutex_);
            while(!readHeadroom() && open_)
            {
                readyForRead_.wait(lock);
            }

            return open_;
        }

        T dequeue()
        {
            std::unique_lock<std::mutex> lock(mutex_);
            while(!readHeadroom())
            {
                readyForRead_.wait(lock);
            }

            T const& e = std::move(fifo_[read_]);
            incrementRead();

            readyForWrite_.notify_one();

            return std::move(e);
        }
    };

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
        struct sink_traits<Pipe<T>>
        {
            typedef Pipe<T> type;
            typedef void iterator;
            typedef T value_type;
        };

        /**
         * Template specialization for Pipes
         */
        template <>
        template <typename T>
        class SinkImpl<Pipe<T>> : public SinkImplBase<T>
        {
            typedef Pipe<T> pipe_type;
            typedef SinkImpl<pipe_type> type;
            typedef T value_type;
            Pipe<T>& pipe_;

        public:

            SinkImpl(Pipe<T>& pipe)
                : pipe_(pipe)
            { }

            ~SinkImpl() { }

            bool hasNext()
            {
                return pipe_.isOpen();
            }

            value_type next()
            {
                return pipe_.dequeue();
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
        typedef void difference_type; // TODO: is this ok?

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

            // an "end" iterator is:
            // - either the default constructed iterator (pimpl is nullptr)
            // - or has reached the end of iteration (hasNext() returns false)
            return !(b || a->hasNext());
        }

        bool operator != (iterator& other) { return !(*this == other); }
    };

    template <typename Input, typename Output>
    Sink<Output> connect(Sink<Input>& input,
                         std::function<Output(Input)> tranformation)
    {

    }

    template <typename InputIterable>
    Sink<typename detail::sink_traits<InputIterable>::value_type>
    MakeSink(InputIterable& iterable)
    {
        return Sink<typename detail::sink_traits<InputIterable>::value_type>(iterable);
    }

}

#endif /* end of include guard: PLUMBING_H_IEGJRLCP */
