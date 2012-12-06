#include "plumbing.hpp"

#include <thread>
#include <iostream>
#include <vector>

using namespace Plumbing;

int main(int argc, char const *argv[])
{
    std::vector<std::string> vals{"Hello", "Concurrent", "World", "Of"};

    // testing sink implementation type erasure
    auto sinkImpl = detail::SinkImpl<std::vector<std::string>>(vals);
    detail::SinkImplBase<std::string>& sink = sinkImpl;
    while (sink.hasNext())
    {
        std::cout << sink.next() << std::endl;
    }

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
