#ifndef MOVE_CHECKER_HPP_12ZK6OPS
#define MOVE_CHECKER_HPP_12ZK6OPS

#include <vector>
#include <utility>
#include <memory>
#include <iostream>

namespace Plumbing
{

class move_checker
{
    // shared counters of copies and moves
    std::shared_ptr<int> copies_;
    std::shared_ptr<int> moves_;

public:
    // expensive payload
    std::vector<int> payload; 

    typedef std::vector<int>::const_iterator const_iterator;
    typedef std::vector<int>::iterator iterator;

    move_checker()
        : copies_(new int(0)),
          moves_(new int(0)),
          payload({1, 2, 3, 4, 5, 6, 7})
    { }

    // copy constructor. counts copy operations
    move_checker(move_checker const& other)
        : copies_(other.copies_),
          moves_(other.moves_),
          payload(other.payload)
    {
        *copies_ += 1;
    }

    // copy assignment
    move_checker& operator = (move_checker const& other)
    {
        copies_ = other.copies_;
        moves_ = other.moves_;
        payload = other.payload;

        *copies_ += 1;

        return *this;
    }

    // move constructor. counts move operations
    move_checker(move_checker&& other)
        : copies_(other.copies_),
          moves_(other.moves_),
          payload(std::move(other.payload))
    {
        *moves_ += 1;
    }

    // move assignment
    move_checker& operator = (move_checker&& other)
    {
        copies_ = other.copies_;
        moves_ = other.moves_;
        payload = std::move(other.payload);

        *moves_ += 1;

        return *this;
    }

    iterator       begin()       { return payload.begin(); }
    iterator       end()         { return payload.end(); }
    const_iterator begin() const { return payload.begin(); }
    const_iterator end()   const { return payload.end(); }

    // methods to report on the number of copies/moves
    int copies() const { return *copies_; }
    int moves()  const { return *moves_; }
};

void check_copies(move_checker const& checker, int expected)
{
    std::cout << "Copies: " << checker.copies()
              << " (expected " << expected << ")" << std::endl;
}

void check_moves(move_checker const& checker, int expected)
{
    std::cout << "Moves: " << checker.moves()
              << " (expected " << expected << ")" << std::endl;
}

}

#endif /* end of include guard: MOVE_CHECKER_HPP_12ZK6OPS */
