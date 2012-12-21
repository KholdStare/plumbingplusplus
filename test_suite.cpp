/**
 * The main test suite of the library. Uses Boost Unit Test Framework.
 */

//      Copyright Alexander Kondratskiy 2012 - 2013.
// Distributed under the Boost Software License, Version 1.0.
//    (See accompanying file LICENSE_1_0.txt or copy at
//          http://www.boost.org/LICENSE_1_0.txt)

#define BOOST_TEST_DYN_LINK
#define BOOST_TEST_MAIN
#include <boost/test/unit_test.hpp>

#include "plumbing.hpp"


using namespace Plumbing;

//____________________________________________________________________________//

BOOST_AUTO_TEST_SUITE(pipe_tests)

BOOST_AUTO_TEST_CASE( empty_pipe )
{
    Pipe<std::string> pipe;
    std::thread a([&](){ 
            while (pipe.isOpen())
            {
                std::cout << pipe.dequeue() << std::endl;
            }
            });

    pipe.close();

#ifdef __unix  // don't have timeout on other platforms
    BOOST_TEST_CHECKPOINT("Trying to join with thread");
#else
    BOOST_TEST_MESSAGE( "Timeout support is not implemented on your platform" );
#endif

    a.join();
}

BOOST_AUTO_TEST_CASE( one_element_pipe )
{
    Pipe<int> pipe;
    std::vector<int> output;
    std::thread a([&](){ 
            while (pipe.isOpen())
            {
                output.push_back(pipe.dequeue());
            }
    });

    pipe.enqueue(42);

    pipe.close();

#ifdef __unix  // don't have timeout on other platforms
    BOOST_TEST_CHECKPOINT("Trying to join with thread");
#else
    BOOST_TEST_MESSAGE( "Timeout support is not implemented on your platform" );
#endif

    a.join();

    BOOST_REQUIRE_EQUAL( output.size(), 1 );

    BOOST_CHECK_EQUAL( output[0], 42 );
}

BOOST_AUTO_TEST_CASE( many_element_pipe )
{
    Pipe<int> pipe;
    std::vector<int> input{1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
    std::vector<int> output;
    std::thread a([&](){ 
            while (pipe.isOpen())
            {
                output.push_back(pipe.dequeue());
            }
    });

    for (auto&& elem : input)
    {
        pipe.enqueue(elem);
    }

    pipe.close();

#ifdef __unix  // don't have timeout on other platforms
    BOOST_TEST_CHECKPOINT("Trying to join with thread");
#else
    BOOST_TEST_MESSAGE( "Timeout support is not implemented on your platform" );
#endif

    a.join();

    BOOST_REQUIRE_EQUAL( output.size(), input.size() );

    BOOST_CHECK( output == input );
}

BOOST_AUTO_TEST_CASE( larger_capacity_pipe )
{
    Pipe<int> pipe(5);
    std::vector<int> input{1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
    std::vector<int> output;
    std::thread a([&](){ 
            while (pipe.isOpen())
            {
                output.push_back(pipe.dequeue());
            }
    });

    for (auto&& elem : input)
    {
        pipe.enqueue(elem);
    }

    pipe.close();

#ifdef __unix  // don't have timeout on other platforms
    BOOST_TEST_CHECKPOINT("Trying to join with thread");
#else
    BOOST_TEST_MESSAGE( "Timeout support is not implemented on your platform" );
#endif

    a.join();

    BOOST_REQUIRE_EQUAL( output.size(), input.size() );

    BOOST_CHECK( output == input );
}

BOOST_AUTO_TEST_SUITE_END()

//____________________________________________________________________________//

BOOST_AUTO_TEST_SUITE(perfect_forwarding)

char const* payloadString = "Wololo";

/**
 * A helper class to keep track of the number of moves/copies.
 *
 * It can be used to ensure passing of objects does not incur
 * unnecessary/unanticipated copies.
 */
class move_checker
{
    std::shared_ptr<int> copies_;
    std::shared_ptr<int> moves_;

public:
    std::vector<std::string> payload; // expensive payload

    move_checker()
        : copies_(new int(0)),
          moves_(new int(0)),
          payload(1000, std::string(payloadString))
    { }

    move_checker(move_checker const& other)
        : copies_(other.copies_),
          moves_(other.moves_),
          payload(other.payload)
    {
        *copies_ += 1;
    }

    move_checker& operator = (move_checker const& other)
    {
        copies_ = other.copies_;
        moves_ = other.moves_;
        payload = other.payload;

        *copies_ += 1;

        return *this;
    }

    move_checker(move_checker&& other)
        : copies_(std::move(other.copies_)),
          moves_(std::move(other.moves_)),
          payload(std::move(other.payload))
    {
        *moves_ += 1;
    }

    move_checker& operator = (move_checker&& other)
    {
        copies_ = std::move(other.copies_);
        moves_ = std::move(other.moves_);
        payload = std::move(other.payload);

        *moves_ += 1;

        return *this;
    }

    int copies() const { return *copies_; }
    int moves()  const { return *moves_; }
};
 
BOOST_AUTO_TEST_CASE( move_checker_init )
{
    move_checker checker;

    BOOST_CHECK_EQUAL( checker.copies(), 0 );
    BOOST_CHECK_EQUAL( checker.moves(), 0 );
}


BOOST_AUTO_TEST_CASE( move_checker_copy )
{
    move_checker checker;

    BOOST_CHECK_EQUAL( checker.copies(), 0 );
    BOOST_CHECK_EQUAL( checker.moves(), 0 );

    move_checker copy(checker);

    BOOST_CHECK_EQUAL( checker.copies(), 1 );
    BOOST_CHECK_EQUAL( copy.copies(), 1 );
    BOOST_CHECK_EQUAL( checker.moves(), 0 );
    BOOST_CHECK_EQUAL( copy.moves(), 0 );
}

BOOST_AUTO_TEST_CASE( move_checker_copy_assignment )
{
    move_checker checker;

    BOOST_CHECK_EQUAL( checker.copies(), 0 );
    BOOST_CHECK_EQUAL( checker.moves(), 0 );

    move_checker copy = checker;

    BOOST_CHECK_EQUAL( checker.copies(), 1 );
    BOOST_CHECK_EQUAL( copy.copies(), 1 );
    BOOST_CHECK_EQUAL( checker.moves(), 0 );
    BOOST_CHECK_EQUAL( copy.moves(), 0 );
}

BOOST_AUTO_TEST_CASE( move_checker_move )
{
    move_checker checker;

    BOOST_CHECK_EQUAL( checker.copies(), 0 );
    BOOST_CHECK_EQUAL( checker.moves(), 0 );

    move_checker copy(std::move(checker));

    BOOST_CHECK_EQUAL( copy.copies(), 0 );
    BOOST_CHECK_EQUAL( copy.moves(), 1 );
}

template <typename T>
std::string accessValue(T&& checker) // checker here will be move_checker
{
    auto lambda =
        [](T&& checker) mutable
        {
            return checker.payload[0];
        };

    return lambda(std::forward<T>(checker));
}


template <typename T>
std::string accessValueAsync(T&& checker) // checker here will be move_checker
{
    std::future<std::string> fut =
        std::async(std::launch::async,
            [](T&& checker) mutable
            {
                return checker.payload[0];
            },
            detail::async_forwarder<T>(std::forward<T>(checker)));

    return fut.get();
}


BOOST_AUTO_TEST_CASE( forwarded_lambda_reference )
{
    move_checker checker;
    move_checker copy(checker);

    BOOST_CHECK_EQUAL( checker.copies(), 1 );
    BOOST_CHECK_EQUAL( copy.copies(), 1 );
    BOOST_CHECK_EQUAL( checker.moves(), 0 );
    BOOST_CHECK_EQUAL( copy.moves(), 0 );

    std::string output = accessValue(copy);

    BOOST_CHECK_EQUAL( checker.copies(), 1 );
    BOOST_CHECK_EQUAL( checker.moves(), 0 );

    BOOST_CHECK_EQUAL( output, payloadString);
}

BOOST_AUTO_TEST_CASE( forwarded_lambda_move )
{
    move_checker checker;
    move_checker copy(checker);

    BOOST_CHECK_EQUAL( checker.copies(), 1 );
    BOOST_CHECK_EQUAL( copy.copies(), 1 );
    BOOST_CHECK_EQUAL( checker.moves(), 0 );
    BOOST_CHECK_EQUAL( copy.moves(), 0 );

    std::string output = accessValue(std::move(copy));

    BOOST_CHECK_EQUAL( checker.copies(), 1 );
    BOOST_CHECK_EQUAL( checker.moves(), 0 );

    BOOST_CHECK_EQUAL( output, payloadString);
}

BOOST_AUTO_TEST_CASE( forwarded_lambda_copy_async )
{
    move_checker checker;
    move_checker copy(checker);

    BOOST_CHECK_EQUAL( checker.copies(), 1 );
    BOOST_CHECK_EQUAL( copy.copies(), 1 );
    BOOST_CHECK_EQUAL( checker.moves(), 0 );
    BOOST_CHECK_EQUAL( copy.moves(), 0 );

    std::string output = accessValueAsync(copy);

    // costs us one move and one copy unfortunately
    BOOST_CHECK_EQUAL( checker.copies(), 1 );
    BOOST_CHECK_EQUAL( checker.moves(), 0 );

    BOOST_CHECK_EQUAL( output, payloadString);
}

BOOST_AUTO_TEST_CASE( forwarded_lambda_move_async )
{
    move_checker checker;
    move_checker copy(checker);

    BOOST_CHECK_EQUAL( checker.copies(), 1 );
    BOOST_CHECK_EQUAL( copy.copies(), 1 );
    BOOST_CHECK_EQUAL( checker.moves(), 0 );
    BOOST_CHECK_EQUAL( copy.moves(), 0 );

    std::string output = accessValueAsync(std::move(copy));

    BOOST_CHECK_EQUAL( checker.copies(), 1 );
    BOOST_CHECK_EQUAL( checker.moves(), 3 );

    BOOST_CHECK_EQUAL( output, payloadString);
}

BOOST_AUTO_TEST_CASE( lambda_move_async )
{
    move_checker checker;
    move_checker copy(checker);

    BOOST_CHECK_EQUAL( checker.copies(), 1 );
    BOOST_CHECK_EQUAL( copy.copies(), 1 );
    BOOST_CHECK_EQUAL( checker.moves(), 0 );
    BOOST_CHECK_EQUAL( copy.moves(), 0 );

    std::future<std::string> fut =
        std::async(std::launch::async,
            [](move_checker&& c) mutable
            {
                return c.payload[0];
            },
            std::move(copy)
        );

    std::string output = fut.get();

    BOOST_CHECK_EQUAL( checker.copies(), 1 );
    BOOST_CHECK_EQUAL( checker.moves(), 2 );

    BOOST_CHECK_EQUAL( output, payloadString);
}

/**
 * Check the cost of moving through a pipe in terms of moves and copies.
 */
BOOST_AUTO_TEST_CASE( move_through_pipe )
{
    Pipe<move_checker> pipe;
    move_checker checker;
    move_checker copy(checker);

    BOOST_CHECK_EQUAL( checker.copies(), 1 );
    BOOST_CHECK_EQUAL( copy.copies(), 1 );
    BOOST_CHECK_EQUAL( checker.moves(), 0 );
    BOOST_CHECK_EQUAL( copy.moves(), 0 );

    pipe.enqueue(checker);

    BOOST_CHECK_EQUAL( checker.copies(), 2 );
    BOOST_CHECK_EQUAL( checker.moves(), 0 );

    auto result = pipe.dequeue();

    BOOST_CHECK_EQUAL( checker.copies(), 3 );
    BOOST_CHECK_EQUAL( checker.moves(), 0 );

    pipe.enqueue(std::move(copy));

    BOOST_CHECK_EQUAL( checker.copies(), 4 );
    BOOST_CHECK_EQUAL( checker.moves(), 0 );

    auto result2 = pipe.dequeue();

    BOOST_CHECK_EQUAL( checker.copies(), 5 );
    BOOST_CHECK_EQUAL( checker.moves(), 0 );
}

BOOST_AUTO_TEST_CASE( simple_pipeline )
{
    std::vector<move_checker> checkerVec(10);
    std::vector<std::string> output;

    auto testFunc =
        [&](move_checker const& checker) mutable
        {
            output.push_back(checker.payload[0]);
        };

    connect(checkerVec, testFunc).wait();

    BOOST_CHECK_EQUAL( output.size(), 10 );
    BOOST_CHECK_EQUAL( checkerVec[0].copies(), 0 );
    BOOST_CHECK_EQUAL( checkerVec[0].moves(), 0 );

    std::vector<move_checker> checkerVecCopy(checkerVec);

    BOOST_CHECK_EQUAL( checkerVec[0].copies(), 1 );
    BOOST_CHECK_EQUAL( checkerVec[0].moves(), 0 );

    connect(std::move(checkerVecCopy), testFunc).wait();

    BOOST_CHECK_EQUAL( output.size(), 20 );
    BOOST_CHECK_EQUAL( checkerVec[0].copies(), 1 );
    BOOST_CHECK_EQUAL( checkerVec[0].moves(), 0 );
}

move_checker modifyMoveChecker(move_checker& checker) 
{
    checker.payload[0] = "Trololo";

    return checker;
}

BOOST_AUTO_TEST_CASE( two_part_pipeline )
{
    std::vector<move_checker> checkerVec(10);
    std::vector<std::string> output;

    auto testFunc =
        [&](move_checker const& checker) mutable
        {
            output.push_back(checker.payload[0]);
        };

    connect(checkerVec, modifyMoveChecker, testFunc).wait();

    BOOST_CHECK_EQUAL( output.size(), 10 );
    BOOST_CHECK_EQUAL( output[0], "Trololo" );
    BOOST_CHECK_EQUAL( checkerVec[0].copies(), 3 );
    BOOST_CHECK_EQUAL( checkerVec[0].moves(), 0 );

    std::vector<move_checker> checkerVecCopy(checkerVec);

    BOOST_CHECK_EQUAL( checkerVec[0].copies(), 4 );
    BOOST_CHECK_EQUAL( checkerVec[0].moves(), 0 );

    connect(std::move(checkerVecCopy), modifyMoveChecker, testFunc).wait();

    BOOST_CHECK_EQUAL( output.size(), 20 );
    BOOST_CHECK_EQUAL( output[0], "Trololo" );
    BOOST_CHECK_EQUAL( checkerVec[0].copies(), 7 );
    BOOST_CHECK_EQUAL( checkerVec[0].moves(), 0 );
}

//____________________________________________________________________________//

BOOST_AUTO_TEST_SUITE_END()
