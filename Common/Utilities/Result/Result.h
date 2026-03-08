#pragma once
#include <cstddef>
#include <new>
#include <type_traits>
#include "ResultStatus.h"

template<typename T>
struct Ok {
    T value;
    explicit Ok(const T& v) : value(v) {}
    explicit Ok(T&& v) : value(static_cast<T&&>(v)) {}
};

template<typename E>
struct Err {
    E error;
    explicit Err(const E& e) : error(e) {}
    explicit Err(E&& e) : error(static_cast<E&&>(e)) {}
};

template<typename T, typename E = ResultStatus>
class Result {
private:
    union {
        T value_;
        E error_;
    };
    bool ok_;

public:

    template<typename U = T,
        typename = typename std::enable_if<!std::is_same<typename std::decay<U>::type, Result>::value &&
                                           !std::is_same<typename std::decay<U>::type, E>::value>::type>
    Result(U&& value) : ok_(true) {
        new (&value_) T(static_cast<U&&>(value));
    }

    template<typename U = E,
        typename = typename std::enable_if<!std::is_same<T, E>::value &&
                                            std::is_same<typename std::decay<U>::type, E>::value>::type,
        typename = void>
    Result(U&& error) : ok_(false) {
        new (&error_) E(static_cast<U&&>(error));
    }

    Result(Ok<T>&& ok) : ok_(true) {
        new (&value_) T(static_cast<T&&>(ok.value));
    }

    Result(Err<E>&& err) : ok_(false) {
        new (&error_) E(static_cast<E&&>(err.error));
    }

    Result(const Ok<T>& ok) : ok_(true) {
        new (&value_) T(ok.value);
    }

    Result(const Err<E>& err) : ok_(false) {
        new (&error_) E(err.error);
    }

    ~Result() { Destroy(); }

    Result(const Result& other) : ok_(other.ok_) {
        if (ok_) {
            new (&value_) T(other.value_);
        } else {
            new (&error_) E(other.error_);
        }
    }

    Result(Result&& other) : ok_(other.ok_) {
        if (ok_) {
            new (&value_) T(static_cast<T&&>(other.value_));
        } else {
            new (&error_) E(static_cast<E&&>(other.error_));
        }
    }

    Result& operator=(const Result& other) {
        if (this != &other) {
            Destroy();
            ok_ = other.ok_;
            if (ok_) {
                new (&value_) T(other.value_);
            } else {
                new (&error_) E(other.error_);
            }
        }
        return *this;
    }

    Result& operator=(Result&& other) {
        if (this != &other) {
            Destroy();
            ok_ = other.ok_;
            if (ok_) {
                new (&value_) T(static_cast<T&&>(other.value_));
            } else {
                new (&error_) E(static_cast<E&&>(other.error_));
            }
        }
        return *this;
    }

    static Result Capture(E status, T&& value) {
        if (status != E{}) return status;
        return static_cast<T&&>(value);
    }

    static Result Capture(E status, const T& value) {
        if (status != E{}) return status;
        return value;
    }

    bool IsOk() const { return ok_; }
    bool IsErr() const { return !ok_; }
    explicit operator bool() const { return ok_; }

    T& Value() { return value_; }
    const T& Value() const { return value_; }

    E& Error() { return error_; }
    const E& Error() const { return error_; }

    T& operator*() { return value_; }
    const T& operator*() const { return value_; }

    T* operator->() { return &value_; }
    const T* operator->() const { return &value_; }

    T ValueOr(const T& defaultValue) const {
        return ok_ ? value_ : defaultValue;
    }

    template<typename Fn>
    auto Map(Fn&& fn) const -> Result<decltype(fn(value_)), E> {
        using U = decltype(fn(value_));
        if (ok_) {
            return Ok<U>(fn(value_));
        }
        return Err<E>(error_);
    }

    template<typename Fn>
    auto MapErr(Fn&& fn) const -> Result<T, decltype(fn(error_))> {
        using U = decltype(fn(error_));
        if (!ok_) {
            return Err<U>(fn(error_));
        }
        return Ok<T>(value_);
    }

    template<typename Fn>
    auto AndThen(Fn&& fn) const -> decltype(fn(value_)) {
        if (ok_) {
            return fn(value_);
        }
        return Err<E>(error_);
    }

    template<typename Fn>
    auto OrElse(Fn&& fn) const -> decltype(fn(error_)) {
        if (!ok_) {
            return fn(error_);
        }
        return Ok<T>(value_);
    }

private:
    void Destroy() {
        if (ok_) {
            value_.~T();
        } else {
            error_.~E();
        }
    }
};
