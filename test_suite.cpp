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

//____________________________________________________________________________//

using namespace Plumbing;

// most frequently you implement test cases as a free functions with automatic registration
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

//____________________________________________________________________________//

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

//____________________________________________________________________________//

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

//____________________________________________________________________________//

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

BOOST_AUTO_TEST_SUITE(perfect_forwarding)

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
          payload(1000, std::string("Wololo"))
    { }

    move_checker(move_checker const& other)
        : copies_(other.copies_),
          moves_(other.moves_),
          payload(other.payload)
    {
        *copies_ += 1;
    }

    move_checker(move_checker&& other)
        : copies_(std::move(other.copies_)),
          moves_(std::move(other.moves_)),
          payload(std::move(other.payload))
    {
        *moves_ += 1;
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

//____________________________________________________________________________//

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

//____________________________________________________________________________//

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

//____________________________________________________________________________//

BOOST_AUTO_TEST_CASE( move_checker_move )
{
    move_checker checker;

    BOOST_CHECK_EQUAL( checker.copies(), 0 );
    BOOST_CHECK_EQUAL( checker.moves(), 0 );

    move_checker copy(std::move(checker));

    BOOST_CHECK_EQUAL( copy.copies(), 0 );
    BOOST_CHECK_EQUAL( copy.moves(), 1 );
}

//____________________________________________________________________________//

BOOST_AUTO_TEST_CASE( forwarder_explicit_init )
{
    move_checker checker;
    move_checker copy(checker);

    BOOST_CHECK_EQUAL( checker.copies(), 1 );
    BOOST_CHECK_EQUAL( copy.copies(), 1 );
    BOOST_CHECK_EQUAL( checker.moves(), 0 );
    BOOST_CHECK_EQUAL( copy.moves(), 0 );

    struct detail::forwarder<move_checker&&> f(std::move(copy));

    BOOST_CHECK_EQUAL( f.val.copies(), 1 );
    BOOST_CHECK_EQUAL( f.val.moves(), 1 );
}

//____________________________________________________________________________//

BOOST_AUTO_TEST_CASE( forwarder_make_init )
{
    move_checker checker;
    move_checker copy(checker);

    BOOST_CHECK_EQUAL( checker.copies(), 1 );
    BOOST_CHECK_EQUAL( copy.copies(), 1 );
    BOOST_CHECK_EQUAL( checker.moves(), 0 );
    BOOST_CHECK_EQUAL( copy.moves(), 0 );

    auto f = detail::make_forwarder(std::move(copy));
    (void) f;

    BOOST_CHECK_EQUAL( f.val.copies(), 1 );
    BOOST_CHECK_EQUAL( f.val.moves(), 1 );
}

//____________________________________________________________________________//
 
BOOST_AUTO_TEST_CASE( forwarder_copy )
{
    move_checker checker;
    move_checker copy(checker);

    BOOST_CHECK_EQUAL( checker.copies(), 1 );
    BOOST_CHECK_EQUAL( copy.copies(), 1 );
    BOOST_CHECK_EQUAL( checker.moves(), 0 );
    BOOST_CHECK_EQUAL( copy.moves(), 0 );

    auto f = detail::make_forwarder(std::move(copy));

    BOOST_CHECK_EQUAL( f.val.copies(), 1 );
    BOOST_CHECK_EQUAL( f.val.moves(), 1 );

    struct detail::forwarder<move_checker&&> f2(f);

    BOOST_CHECK_EQUAL( f2.val.copies(), 1 );
    BOOST_CHECK_EQUAL( f2.val.moves(), 2 );
}

//____________________________________________________________________________//
 
BOOST_AUTO_TEST_CASE( forwarder_move )
{
    move_checker checker;
    move_checker copy(checker);

    BOOST_CHECK_EQUAL( checker.copies(), 1 );
    BOOST_CHECK_EQUAL( copy.copies(), 1 );
    BOOST_CHECK_EQUAL( checker.moves(), 0 );
    BOOST_CHECK_EQUAL( copy.moves(), 0 );

    auto f = detail::make_forwarder(std::move(copy));
    struct detail::forwarder<move_checker&&> f2(std::move(f));

    BOOST_CHECK_EQUAL( f2.val.copies(), 1 );
    BOOST_CHECK_EQUAL( f2.val.moves(), 2 );
}

//____________________________________________________________________________//

template <typename T>
std::string accessValue(T&& checker) // checker here will be move_checker
{
    auto&& f = detail::make_forwarder(std::forward<T>(checker));

    std::string output;
    auto lambda =
        [f, &output]() mutable
        {
            output = f.val.payload[0];
        };

    lambda();
    return output;
}

BOOST_AUTO_TEST_CASE( forwarder_lambda )
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

    BOOST_CHECK_EQUAL( output, "Wololo" );
}

BOOST_AUTO_TEST_SUITE_END()
