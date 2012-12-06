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

    std::cout << std::endl;
    //std::vector<int> vals{42, 7, 32, 6};
    Pipe<std::string> pipe(2);
    //Pipe<int> pipe;
    std::thread a([&](){
            for (auto& e : vals) {
                pipe.enqueue(e);
            }
            });

    std::thread b([&](){ 
            while (pipe.isOpen())
            {
                std::cout << pipe.dequeue() << std::endl;
            }
            });

    a.join();

    pipe.enqueue("Awesomeness");
    //pipe.enqueue(666);

    pipe.close();
    b.join();

    return 0;
}
