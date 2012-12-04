#include "plumbing.hpp"

#include <thread>
#include <iostream>

using namespace Plumbing;

int main(int argc, char const *argv[])
{
    Pipe<int> pipe;
    std::thread a([&](){ 
            pipe.enqueue(42);
            pipe.enqueue(7);
            pipe.enqueue(32);
            pipe.enqueue(6);
            });

    std::thread b([&](){ 
            while (pipe.isOpen())
            {
                std::cout << pipe.pop() << std::endl;
            }
            });

    a.join();

    pipe.enqueue(666);

    pipe.close();
    b.join();

    return 0;
}
