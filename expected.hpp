// This code is put in the public domain by Andrei Alexandrescu
// See http://www.reddit.com/r/programming/comments/14m1tc/andrei_alexandrescu_systematic_error_handling_in/c7etk47

#ifndef EXPECTED_HPP_TN6DJT51
#define EXPECTED_HPP_TN6DJT51

#include <stdexcept>
#include <algorithm>

template <class T>
  class Expected {
  union {
    std::exception_ptr spam;
    T ham;
  };
  bool gotHam;

  Expected() {
    // used by fromException below
  }

public:
  Expected(const T& rhs) : ham(rhs), gotHam(true) {}
  Expected(T&& rhs) : ham(std::move(rhs)), gotHam(true) {}

  Expected(const Expected& rhs) : gotHam(rhs.gotHam) {
    if (gotHam) new(&ham) T(rhs.ham);
    else new(&spam) std::exception_ptr(rhs.spam);
  }
  Expected(Expected&& rhs) : gotHam(rhs.gotHam) {
    if (gotHam) new(&ham) T(std::move(rhs.ham));
    else new(&spam) std::exception_ptr(std::move(rhs.spam));
  }

  void swap(Expected& rhs) {
    if (gotHam) {
      if (rhs.gotHam) {
        using std::swap;
        swap(ham, rhs.ham);
      } else {
        auto t = std::move(rhs.spam);
        new(&rhs.ham) T(std::move(ham));
        new(&spam) std::exception_ptr(t);
        std::swap(gotHam, rhs.gotHam);
      }
    } else {
      if (rhs.gotHam) {
        rhs.swap(*this);
      } else {
        spam.swap(rhs.spam);
        std::swap(gotHam, rhs.gotHam);
      }
    }
  }

  Expected& operator=(Expected<T> rhs) {
    swap(rhs);
    return *this;
  }

  ~Expected() {
    //using std::exception_ptr;
    if (gotHam) ham.~T();
    else spam.~exception_ptr();
  }

  static Expected<T> fromException(std::exception_ptr p) {
    Expected<T> result;
    result.gotHam = false;
    new(&result.spam) std::exception_ptr(std::move(p));
    return result;
  }

  template <class E>
  static Expected<T> fromException(const E& exception) {
    if (typeid(exception) != typeid(E)) {
      throw std::invalid_argument(
        "Expected<T>::fromException: slicing detected.");
    }
    return fromException(std::make_exception_ptr(exception));
  }

  static Expected<T> fromException() {
    return fromException(std::current_exception());
  }

  bool valid() const {
    return gotHam;
  }

  /**
   * implicit conversion may throw if spam
   */
  operator T&() {
    if (!gotHam) std::rethrow_exception(spam);
    return ham;
  }

  T& get() {
    if (!gotHam) std::rethrow_exception(spam);
    return ham;
  }

  const T& get() const {
    if (!gotHam) std::rethrow_exception(spam);
    return ham;
  }

  template <class E>
  bool hasException() const {
    try {
      if (!gotHam) std::rethrow_exception(spam);
    } catch (const E& object) {
      return true;
    } catch (...) {
    }
    return false;
  }

  template <class F>
  static Expected fromCode(F fun) {
    try {
      return Expected(fun());
    } catch (...) {
      return fromException();
    }
  }
};

#endif /* end of include guard: EXPECTED_HPP_TN6DJT51 */
