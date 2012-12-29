#include "plumbing.hpp"

#include <thread>
#include <iostream>
#include <vector>

using namespace Plumbing;

template <typename T>
void printLine(T elem)
{
    std::cout << elem << std::endl;
};

// NOTE: Currently does not work properly because
// hasNext() and dequeue() do not happend atomically.
void testSplitting()
{
    std::vector<int> vals{1,2,3,4,5,6,7,8,9,10};
    // testing pipe usage
    std::cout << std::endl;
    Pipe<int> pipe(4);

    // both threads are reading from the same pipe
    std::thread a([&](){ 
        while (pipe.hasNext())
        {
            std::cout << "Thread A: " << pipe.dequeue() << std::endl;
        }
    });

    std::thread b([&](){ 
        while (pipe.hasNext())
        {
            std::cout << "Thread B: " << pipe.dequeue() << std::endl;
        }
    });

    for (auto& e : vals) {
        pipe.enqueue(e);
    }

    pipe.close();

    a.join();
    b.join();
}

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
            while (pipe.hasNext())
            {
                std::cout << pipe.dequeue() << std::endl;
            }
        });

    a.join();

    pipe.enqueue("Awesomeness");

    pipe.close();
    b.join();

    //testSplitting();
    
    // testing connect
    std::cout << std::endl;
    std::cout << "Connect test:" << std::endl;
    auto getFirstChar = 
        []( std::string const& s )
        {
            return s[0]; // print first character
        };

    auto source = makeSource(vals);
    ( source >> getFirstChar >> printLine<char> ).wait();
    connect( source, getFirstChar, printLine<char> ).wait();
    connect( source, printLine<std::string>).wait();

    return 0;
}
