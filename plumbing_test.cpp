#include "plumbing.hpp"

#include <thread>
#include <iostream>
#include <vector>

using namespace Plumbing;

int main(int argc, char const *argv[])
{
    std::vector<std::string> vals{"Hello", "Concurrent", "World", "Of"};

    // testing pipe usage
    std::cout << std::endl;
    Pipe<std::string> pipe(4);
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
