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
#include "move_checker.hpp"
#include "expected.hpp"

#ifdef __unix  // don't have timeout on other platforms
#define TIMEOUT_CHECKPOINT(text) BOOST_TEST_CHECKPOINT(text)
#else
#define TIMEOUT_CHECKPOINT(text) BOOST_TEST_MESSAGE( "Timeout support is not implemented on your platform" )
#endif

using namespace Plumbing;

static const int THE_ANSWER_TO_LIFE_THE_UNIVERSE_AND_EVERYTHING = 42;

//____________________________________________________________________________//

BOOST_AUTO_TEST_SUITE(pipe_tests)

BOOST_AUTO_TEST_CASE( empty_pipe )
{
    Pipe<std::string> pipe;
    std::thread a([&](){ 
            while (pipe.hasNext())
            {
                std::cout << pipe.dequeue() << std::endl;
            }
            });

    pipe.close();
    bool result = pipe.enqueue("Shouldn't work");
    BOOST_CHECK_EQUAL( result, false );

    TIMEOUT_CHECKPOINT("Trying to join with thread");

    a.join();
}

BOOST_AUTO_TEST_CASE( one_element_pipe )
{
    Pipe<int> pipe;
    std::vector<int> output;
    std::thread a([&](){ 
            while (pipe.hasNext())
            {
                output.push_back(pipe.dequeue());
            }
    });

    BOOST_CHECK_EQUAL( pipe.enqueue(THE_ANSWER_TO_LIFE_THE_UNIVERSE_AND_EVERYTHING), true );

    pipe.close();
    bool result = pipe.enqueue(666);
    BOOST_CHECK_EQUAL( result, false );

    TIMEOUT_CHECKPOINT("Trying to join with thread");

    a.join();

    BOOST_REQUIRE_EQUAL( output.size(), 1 );

    BOOST_CHECK_EQUAL( output[0], THE_ANSWER_TO_LIFE_THE_UNIVERSE_AND_EVERYTHING );
}

BOOST_AUTO_TEST_CASE( many_element_pipe )
{
    Pipe<int> pipe;
    std::vector<int> input{1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
    std::vector<int> output;
    std::thread a([&](){ 
            while (pipe.hasNext())
            {
                output.push_back(pipe.dequeue());
            }
    });

    for (auto&& elem : input)
    {
        BOOST_CHECK_EQUAL( pipe.enqueue(elem), true );
    }

    pipe.close();
    bool result = pipe.enqueue(666);
    BOOST_CHECK_EQUAL( result, false );

    TIMEOUT_CHECKPOINT("Trying to join with thread");

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
            while (pipe.hasNext())
            {
                output.push_back(pipe.dequeue());
            }
    });

    for (auto&& elem : input)
    {
        BOOST_CHECK_EQUAL( pipe.enqueue(elem), true );
    }

    pipe.close();
    bool result = pipe.enqueue(666);
    BOOST_CHECK_EQUAL( result, false );

    TIMEOUT_CHECKPOINT("Trying to join with thread");

    a.join();

    BOOST_REQUIRE_EQUAL( output.size(), input.size() );

    BOOST_CHECK( output == input );
}

BOOST_AUTO_TEST_CASE( double_close )
{
    Pipe<int> pipe;
    std::vector<int> output;

    std::thread a([&](){ 
            while (pipe.hasNext())
            {
                output.push_back(pipe.dequeue());
            }
    });

    BOOST_CHECK_EQUAL( pipe.enqueue(THE_ANSWER_TO_LIFE_THE_UNIVERSE_AND_EVERYTHING), true );

    pipe.close();
    bool result = pipe.enqueue(666);
    BOOST_CHECK_EQUAL( result, false );

    TIMEOUT_CHECKPOINT("Trying to close pipe a second time");

    pipe.close();

    TIMEOUT_CHECKPOINT("Trying to join with thread");

    a.join();

    BOOST_REQUIRE_EQUAL( output.size(), 1 );

    BOOST_CHECK_EQUAL( output[0], THE_ANSWER_TO_LIFE_THE_UNIVERSE_AND_EVERYTHING );
}

BOOST_AUTO_TEST_CASE( move_through_pipe )
{
    move_checker checker;

    Pipe<move_checker> pipe(2);
    BOOST_CHECK_EQUAL( pipe.enqueue(std::move(checker)), true );

    move_checker newChecker = pipe.dequeue();
    pipe.close();

    // TODO: would like to minimize copies
    BOOST_CHECK_EQUAL( checker.copies(), 2 );
    BOOST_CHECK_EQUAL( checker.moves(), 0 );
}

/**
 * Test whether closing from the READ side prematurely,
 * works as expected
 */
BOOST_AUTO_TEST_CASE( premature_closing )
{
    Pipe<int> pipe;
    std::vector<int> input{1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
    std::vector<int> output;
    static const size_t limit = 3;

    std::thread a([&](){ 
            for (size_t i = 0; i < limit && pipe.hasNext(); ++i)
            {
                output.push_back(pipe.dequeue());
            }
            pipe.forceClose(); // close from read side!
    });

    for (auto&& elem : input)
    {
        // can fail enqueueing!
        if (!pipe.enqueue(elem))
        {
            break;
        }
    }

    pipe.close();
    bool result = pipe.enqueue(666);
    BOOST_CHECK_EQUAL( result, false );

    TIMEOUT_CHECKPOINT("Trying to join with thread");

    a.join();

    BOOST_REQUIRE_EQUAL( output.size(), limit );
}

/**
 * Test whether closing from the READ side when the 
 * write side is full works.
 */
BOOST_AUTO_TEST_CASE( read_closing_when_full )
{
    Pipe<int> pipe(3);
    std::vector<int> input{1, 2};

    TIMEOUT_CHECKPOINT("Trying to enqueue with no reads");

    for (auto&& elem : input)
    {
        BOOST_CHECK_EQUAL( pipe.enqueue(elem), true );
    }


    TIMEOUT_CHECKPOINT("Trying to force close when pipe full");

    pipe.forceClose();
    bool result = pipe.enqueue(666);
    BOOST_CHECK_EQUAL( result, false );
}

BOOST_AUTO_TEST_SUITE_END()
//____________________________________________________________________________//

BOOST_AUTO_TEST_SUITE( source_tests )

BOOST_AUTO_TEST_CASE( source_create )
{
    move_checker checker;

    Source<move_checker> source = makeSource(checker);

    BOOST_CHECK_EQUAL( checker.copies(), 0 );
    BOOST_CHECK_EQUAL( checker.moves(), 0 );
}

BOOST_AUTO_TEST_CASE( sink_connect )
{
    move_checker checker;

    Source<move_checker> source = makeSource(checker);

    auto inc = [](int i) { return i+1; };

    Sink<int> sink = connect(source, inc);

    std::vector<int> results;
    std::copy(std::begin(sink), std::end(sink),
            std::back_inserter(results));

    std::vector<int> expectedResults;
    std::transform(std::begin(checker), std::end(checker),
            std::back_inserter(expectedResults), inc);

    BOOST_CHECK_EQUAL( results.size(), expectedResults.size() );
    size_t size = results.size();
    for (size_t i = 0; i < size; ++i)
    {
        BOOST_CHECK_EQUAL( results[i], expectedResults[i] );
    }

    BOOST_CHECK_EQUAL( checker.copies(), 0 );
    BOOST_CHECK_EQUAL( checker.moves(), 0 );
}

BOOST_AUTO_TEST_CASE( source_from_pipe )
{
    move_checker checker;

    auto sharedPipe = std::make_shared<Pipe<move_checker>>();
    Source<std::shared_ptr<Pipe<move_checker>>> source = makeSource(sharedPipe);

    sharedPipe->enqueue(std::move(checker));

    move_checker result = source.impl().next();

    BOOST_CHECK_EQUAL( checker.copies(), 2 );
    BOOST_CHECK_EQUAL( checker.moves(), 0 );
}

BOOST_AUTO_TEST_SUITE_END()
//____________________________________________________________________________//

BOOST_AUTO_TEST_SUITE(perfect_forwarding)

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
int accessValue(T&& checker) // checker here will be move_checker
{
    auto lambda =
        [](T&& checker) mutable
        {
            return checker.payload[0];
        };

    return lambda(std::forward<T>(checker));
}


template <typename T>
int accessValueAsync(T&& checker) // checker here will be move_checker
{
    std::future<int> fut =
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

    int output = accessValue(copy);

    BOOST_CHECK_EQUAL( checker.copies(), 1 );
    BOOST_CHECK_EQUAL( checker.moves(), 0 );

    BOOST_CHECK_EQUAL( output, 1);
}

BOOST_AUTO_TEST_CASE( forwarded_lambda_move )
{
    move_checker checker;
    move_checker copy(checker);

    BOOST_CHECK_EQUAL( checker.copies(), 1 );
    BOOST_CHECK_EQUAL( copy.copies(), 1 );
    BOOST_CHECK_EQUAL( checker.moves(), 0 );
    BOOST_CHECK_EQUAL( copy.moves(), 0 );

    int output = accessValue(std::move(copy));

    BOOST_CHECK_EQUAL( checker.copies(), 1 );
    BOOST_CHECK_EQUAL( checker.moves(), 0 );

    BOOST_CHECK_EQUAL( output, 1 );
}

BOOST_AUTO_TEST_CASE( forwarded_lambda_copy_async )
{
    move_checker checker;
    move_checker copy(checker);

    BOOST_CHECK_EQUAL( checker.copies(), 1 );
    BOOST_CHECK_EQUAL( copy.copies(), 1 );
    BOOST_CHECK_EQUAL( checker.moves(), 0 );
    BOOST_CHECK_EQUAL( copy.moves(), 0 );

    int output = accessValueAsync(copy);

    // costs us one move and one copy unfortunately
    BOOST_CHECK_EQUAL( checker.copies(), 1 );
    BOOST_CHECK_EQUAL( checker.moves(), 0 );

    BOOST_CHECK_EQUAL( output, 1);
}

BOOST_AUTO_TEST_CASE( forwarded_lambda_move_async )
{
    move_checker checker;
    move_checker copy(checker);

    BOOST_CHECK_EQUAL( checker.copies(), 1 );
    BOOST_CHECK_EQUAL( copy.copies(), 1 );
    BOOST_CHECK_EQUAL( checker.moves(), 0 );
    BOOST_CHECK_EQUAL( copy.moves(), 0 );

    int output = accessValueAsync(std::move(copy));

    BOOST_CHECK_EQUAL( checker.copies(), 1 );
    BOOST_CHECK_EQUAL( checker.moves(), 3 );

    BOOST_CHECK_EQUAL( output, 1);
}

BOOST_AUTO_TEST_CASE( lambda_move_async )
{
    move_checker checker;
    move_checker copy(checker);

    BOOST_CHECK_EQUAL( checker.copies(), 1 );
    BOOST_CHECK_EQUAL( copy.copies(), 1 );
    BOOST_CHECK_EQUAL( checker.moves(), 0 );
    BOOST_CHECK_EQUAL( copy.moves(), 0 );

    std::future<int> fut =
        std::async(std::launch::async,
            [](move_checker&& c) mutable
            {
                return c.payload[0];
            },
            std::move(copy)
        );

    int output = fut.get();

    BOOST_CHECK_EQUAL( checker.copies(), 1 );
    BOOST_CHECK_EQUAL( checker.moves(), 2 );

    BOOST_CHECK_EQUAL( output, 1 );
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
    std::vector<int> output;

    auto testFunc =
        [&](move_checker const& checker) mutable
        {
            output.push_back(checker.payload[0]);
        };

    connect( makeSource(checkerVec), testFunc).wait();

    BOOST_CHECK_EQUAL( output.size(), 10 );
    BOOST_CHECK_EQUAL( checkerVec[0].copies(), 0 );
    BOOST_CHECK_EQUAL( checkerVec[0].moves(), 0 );

    std::vector<move_checker> checkerVecCopy(checkerVec);

    BOOST_CHECK_EQUAL( checkerVec[0].copies(), 1 );
    BOOST_CHECK_EQUAL( checkerVec[0].moves(), 0 );

    connect( makeSource(std::move(checkerVecCopy)), testFunc).wait();

    BOOST_CHECK_EQUAL( output.size(), 20 );
    BOOST_CHECK_EQUAL( checkerVec[0].copies(), 1 );
    BOOST_CHECK_EQUAL( checkerVec[0].moves(), 0 );
}

// TODO: look into how to make an lvalue reference possible
// with the generic Source<InputIterable>
move_checker modifyMoveChecker(move_checker const& checker) 
{
    move_checker result(std::move(checker));
    result.payload[0] = THE_ANSWER_TO_LIFE_THE_UNIVERSE_AND_EVERYTHING;

    return result;
}

BOOST_AUTO_TEST_CASE( two_part_pipeline )
{
    std::vector<move_checker> checkerVec(10);
    std::vector<int> output;

    auto testFunc =
        [&](move_checker const& checker) mutable
        {
            output.push_back(checker.payload[0]);
        };

    connect( makeSource(checkerVec), modifyMoveChecker, testFunc).wait();

    BOOST_CHECK_EQUAL( output.size(), 10 );
    BOOST_CHECK_EQUAL( output[0], THE_ANSWER_TO_LIFE_THE_UNIVERSE_AND_EVERYTHING );
    BOOST_CHECK_EQUAL( checkerVec[0].copies(), 3 );
    BOOST_CHECK_EQUAL( checkerVec[0].moves(), 0 );

    std::vector<move_checker> checkerVecCopy(checkerVec);

    BOOST_CHECK_EQUAL( checkerVec[0].copies(), 4 );
    BOOST_CHECK_EQUAL( checkerVec[0].moves(), 0 );

    connect( makeSource(std::move(checkerVecCopy)), modifyMoveChecker, testFunc).wait();

    BOOST_CHECK_EQUAL( output.size(), 20 );
    BOOST_CHECK_EQUAL( output[0], THE_ANSWER_TO_LIFE_THE_UNIVERSE_AND_EVERYTHING );
    BOOST_CHECK_EQUAL( checkerVec[0].copies(), 7 );
    BOOST_CHECK_EQUAL( checkerVec[0].moves(), 0 );
}

BOOST_AUTO_TEST_SUITE_END()
//____________________________________________________________________________//

BOOST_AUTO_TEST_SUITE( iterator_filter_tests )

/**
 * Wraps std::copy so it can be passed uninstantiated
 */
struct copy_wrapper
{

    template <typename InputIt, typename OutputIt>
    void operator() (InputIt&& in_first, InputIt&& in_last, OutputIt&& out_first)
    {
        std::copy(std::forward<InputIt>(in_first),
                  std::forward<InputIt>(in_last),
                  std::forward<OutputIt>(out_first));
    }
};


BOOST_AUTO_TEST_CASE( int_copy )
{
    move_checker checker;

    std::vector<int> expected;
    std::copy(checker.begin(), checker.end(), std::back_inserter(expected));

    auto iterfilter = makeIteratorFilter<int, int>(copy_wrapper());

    std::vector<int> results;
    iterfilter(checker.begin(), checker.end(), std::back_inserter(results));

    BOOST_CHECK_EQUAL( expected.size(), results.size() );
    BOOST_CHECK( expected == results );
    BOOST_CHECK_EQUAL( checker.copies(), 0 );
    BOOST_CHECK_EQUAL( checker.moves(), 0 );
}

BOOST_AUTO_TEST_SUITE_END()
//____________________________________________________________________________//

BOOST_AUTO_TEST_SUITE( expected_tests )

BOOST_AUTO_TEST_CASE( expected_andrei_tests )
{

    Expected<int> e = 1;
    BOOST_CHECK_EQUAL(true, e.valid());
    auto e1 = e;
    BOOST_CHECK_EQUAL(true, e1.valid());
    BOOST_CHECK_EQUAL(e.get(), e1.get());
    e = e1;
    BOOST_CHECK_EQUAL(e.get(), e1.get());

    Expected<int> f = Expected<int>::fromException(THE_ANSWER_TO_LIFE_THE_UNIVERSE_AND_EVERYTHING);
    BOOST_CHECK_EQUAL(0, f.valid());

    try {
        throw std::exception();
    }
    catch (...) {
        Expected<int> f = Expected<int>::fromException();
        BOOST_CHECK_EQUAL(0, f.valid());
        BOOST_CHECK_EQUAL(0, f.hasException<int>());
        BOOST_CHECK_EQUAL(true, f.hasException<std::exception>());
    }

    auto g = Expected<int>::fromCode([] {
            return THE_ANSWER_TO_LIFE_THE_UNIVERSE_AND_EVERYTHING;
        });
    BOOST_CHECK_EQUAL(THE_ANSWER_TO_LIFE_THE_UNIVERSE_AND_EVERYTHING, g.get());

    g = Expected<int>::fromCode([]() -> Expected<int> {
            throw THE_ANSWER_TO_LIFE_THE_UNIVERSE_AND_EVERYTHING;
        });

    BOOST_CHECK_EQUAL(0, g.valid());
    BOOST_CHECK_EQUAL(1, g.hasException<int>());
}

void voidFunction(int& a)
{
    a += THE_ANSWER_TO_LIFE_THE_UNIVERSE_AND_EVERYTHING;
}

BOOST_AUTO_TEST_CASE( expected_andrei_tests_void )
{

    Expected<void> e;
    BOOST_CHECK_EQUAL(true, e.valid());
    auto e1 = e;
    BOOST_CHECK_EQUAL(true, e1.valid());
    e = e1;
    BOOST_CHECK_EQUAL(true, e1.valid());

    Expected<void> f = Expected<void>::fromException(THE_ANSWER_TO_LIFE_THE_UNIVERSE_AND_EVERYTHING);
    BOOST_CHECK_EQUAL(0, f.valid());

    try {
        throw std::exception();
    }
    catch (...) {
        Expected<void> f = Expected<void>::fromException();
        BOOST_CHECK_EQUAL(0, f.valid());
        BOOST_CHECK_EQUAL(0, f.hasException<int>());
        BOOST_CHECK_EQUAL(true, f.hasException<std::exception>());
    }

    int val = 5;
    auto g = Expected<void>::fromCode([&val] {
            voidFunction(val);
        });
    g.get();

    g = Expected<void>::fromCode([]() -> Expected<void> {
            throw THE_ANSWER_TO_LIFE_THE_UNIVERSE_AND_EVERYTHING;
        });

    BOOST_CHECK_EQUAL(0, g.valid());
    BOOST_CHECK_EQUAL(1, g.hasException<int>());
}

BOOST_AUTO_TEST_CASE( move_test )
{
    move_checker checker;

    Expected<move_checker> e = checker;

    BOOST_CHECK_EQUAL( true, e.valid() );
    BOOST_CHECK_EQUAL( checker.copies(), 1 );
    BOOST_CHECK_EQUAL( checker.moves(), 0 );

    Expected<move_checker> e2(std::move(checker));

    BOOST_CHECK_EQUAL( true, e2.valid() );
    BOOST_CHECK_EQUAL( checker.copies(), 1 );
    BOOST_CHECK_EQUAL( checker.moves(), 1 );
}

BOOST_AUTO_TEST_CASE( pipe_test )
{
    move_checker checker;

    Expected<move_checker> e(std::move(checker));

    BOOST_CHECK_EQUAL( true, e.valid() );
    BOOST_CHECK_EQUAL( checker.copies(), 0 );
    BOOST_CHECK_EQUAL( checker.moves(), 1 );

    Pipe<Expected<move_checker>> pipe(3);
    BOOST_CHECK_EQUAL( true, pipe.enqueue(e) );

    e = pipe.dequeue();

    BOOST_CHECK_EQUAL( true, e.valid() );
    BOOST_CHECK_EQUAL( checker.copies(), 2 );
    BOOST_CHECK_EQUAL( checker.moves(), 4 );
}

BOOST_AUTO_TEST_SUITE_END()
//____________________________________________________________________________//

BOOST_AUTO_TEST_SUITE( exception_propagation )

BOOST_AUTO_TEST_CASE( one_exception )
{
    move_checker checker;

    auto thrower =
        [](int val)
        {
            if (val == 1)
            {
                throw THE_ANSWER_TO_LIFE_THE_UNIVERSE_AND_EVERYTHING;
            }

            return val+1;
        };

    auto intSource = makeSource(checker);

    Sink<int> sink = ( intSource >> thrower );

    BOOST_REQUIRE_EQUAL( true, sink.impl().hasNext() );
    Expected<int> e = sink.impl().next();

    BOOST_CHECK_EQUAL( false, e.valid() );
    BOOST_CHECK_EQUAL( true, e.hasException<int>() );
    BOOST_CHECK_EQUAL( false, sink.impl().hasNext() );
}

BOOST_AUTO_TEST_CASE( delayed_exception )
{
    move_checker checker;
    static const int failIndex = 3;

    // throw after consuming a few values
    auto thrower =
        [=](int val)
        {
            if (val == failIndex)
            {
                throw THE_ANSWER_TO_LIFE_THE_UNIVERSE_AND_EVERYTHING;
            }

            return val+1;
        };

    // set up source and pipeline
    auto intSource = makeSource(checker);
    Sink<int> sink = ( intSource >> thrower );

    // loop through first valid values
    for (int i = 1; i < failIndex; ++i)
    {
        BOOST_REQUIRE_EQUAL( true, sink.impl().hasNext() );
        Expected<int> e = sink.impl().next();

        BOOST_CHECK_EQUAL( true, e.valid() );
        BOOST_CHECK_EQUAL( i+1, e.get() );
        BOOST_CHECK_EQUAL( false, e.hasException<int>() );
        BOOST_CHECK_EQUAL( true, sink.impl().hasNext() );
    }

    BOOST_REQUIRE_EQUAL( true, sink.impl().hasNext() );
    Expected<int> e = sink.impl().next();

    BOOST_CHECK_EQUAL( false, e.valid() );
    BOOST_CHECK_EQUAL( true, e.hasException<int>() );
    BOOST_CHECK_EQUAL( false, sink.impl().hasNext() );
}

BOOST_AUTO_TEST_SUITE_END()
