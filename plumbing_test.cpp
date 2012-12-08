#include "plumbing.hpp"

#include <thread>
#include <iostream>
#include <vector>

using namespace Plumbing;

int main(int argc, char const *argv[])
{
    std::vector<std::string> vals{"Hello", "Concurrent", "World", "Of"};

    // testing sink implementation type erasure
    std::cout << "SinkImpl type erasure:" << std::endl;
    auto sinkImpl = detail::SinkImpl<std::vector<std::string>>(vals);
    detail::SinkImplBase<std::string>& sinkDetail = sinkImpl;
    while (sinkDetail.hasNext())
    {
        std::cout << sinkDetail.next() << std::endl;
    }

    // testing sink type erasure
    std::cout << std::endl;
    std::cout << "Sink type erasure:" << std::endl;
    auto sink = MakeSink(vals);
    for(auto&& e : sink)
    {
        std::cout << e << std::endl;
    }

    // testing pipe usage
    std::cout << std::endl;
    Pipe<std::string> pipe(2);
    std::thread a([&](){
            for (auto& e : vals) {
                pipe.enqueue(e);
            }
            });

    std::thread b([&](){ 
            auto sink = MakeSink(pipe);
            for (auto&& e : sink) {
                std::cout << e << std::endl;
            }
            });

    a.join();

    pipe.enqueue("Awesomeness");

    pipe.close();
    b.join();

    // testing connect
    std::cout << std::endl;
    std::cout << "Connect test:" << std::endl;
    auto getFirstChar = 
        []( std::string const& s )
        {
            return s[0]; // print first character
        };

    auto printLine =
        [](char c)
        {
            std::cout << c << std::endl;
        };

    ( vals >> getFirstChar >> printLine ).wait();

    return 0;
}
