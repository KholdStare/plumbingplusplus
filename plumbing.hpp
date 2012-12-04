// ideas for "couplings" and fifos between transformations

#ifndef PLUMBING_H_IEGJRLCP
#define PLUMBING_H_IEGJRLCP

#include <vector>
#include <mutex>
#include <condition_variable>

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

    class Semaphore
    {
    private:
        std::mutex mutex_;
        std::condition_variable condition_;
        unsigned long count_;

    public:
        Semaphore(unsigned long count = 0)
            : count_(count)
        {}

        void notify()
        {
            std::lock_guard<std::mutex> lock(mutex_);
            ++count_;
            condition_.notify_one();
        }

        void wait()
        {
            std::unique_lock<std::mutex> lock(mutex_);
            while(!count_)
            {
                condition_.wait(lock);
            }

            --count_;
        }
    };

    // need to define pipes
    template <typename T>
    class Pipe
    {
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
        { }

        void enqueue(T const& e)
        {
            std::unique_lock<std::mutex> lock(mutex_);
            while(!writeHeadroom())
            {
                readyForWrite_.wait(lock);
            }

            fifo_[write_] = e;
            ++write_;

            readyForRead_.notify_one();
        }

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

        bool isOpen()
        {
            std::unique_lock<std::mutex> lock(mutex_);
            while(!readHeadroom() && open_)
            {
                readyForRead_.wait(lock);
            }

            return open_;
        }

        T pop()
        {
            std::unique_lock<std::mutex> lock(mutex_);
            while(!readHeadroom())
            {
                readyForRead_.wait(lock);
            }

            T const& e = fifo_[read_];
            ++read_;

            readyForWrite_.notify_one();

            return e;
        }

        // maintains write position in fifo
        //class In {
            //Pipe& pipe_;

        //public:
            //In(Pipe& pipe) : pipe_(pipe) { }

            //class iterator {
                //Pipe& pipe_;

            //public:
                //iterator(pipe* ) : in_(in) { }

                //T& operator *()
                //{
                    //return T(); // use current write pointer
                //}
            //};

            //iterator begin()
            //{
                //return iterator(*this);
            //}
        //};

        //class Out {
            //Pipe& pipe_;

        //public:
            //Out(Pipe& pipe) : pipe_(pipe) { }

            //class iterator {
                //Out& out_;

            //public:
                //iterator(Out& out) : out_(out) { }

                //T operator *()
                //{
                    //return T(); // use current read pointer
                //}
            //};

            //iterator begin()
            //{
                //return iterator(*this);
            //}

            //iterator end()
            //{
                //return iterator(*this);
            //}
        //};

    };

}

#endif /* end of include guard: PLUMBING_H_IEGJRLCP */
