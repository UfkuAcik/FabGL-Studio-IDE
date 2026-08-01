#pragma once

#include <stdexcept>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace fabgl {

enum class ErrorCode {
    None = 0,
    InvalidArgument,
    InvalidFormat,
    NotFound,
    AlreadyExists,
    InvalidState,
    CycleDetected,
    UnsupportedVersion,
    TypeMismatch,
    CapacityExceeded,
    SerializationFailed,
    IoError,
    InternalError,
};

struct ErrorContext {
    std::string key;
    std::string value;
};

class Error final {
  public:
    Error() = default;
    Error(ErrorCode code, std::string message) : code_(code), message_(std::move(message)) {}

    [[nodiscard]] ErrorCode code() const noexcept {
        return code_;
    }
    [[nodiscard]] const std::string& message() const noexcept {
        return message_;
    }
    [[nodiscard]] const std::vector<ErrorContext>& context() const noexcept {
        return context_;
    }

    Error& addContext(std::string key, std::string value) {
        context_.push_back({std::move(key), std::move(value)});
        return *this;
    }

    [[nodiscard]] Error withContext(std::string key, std::string value) const {
        Error copy = *this;
        copy.addContext(std::move(key), std::move(value));
        return copy;
    }

  private:
    ErrorCode code_ = ErrorCode::None;
    std::string message_;
    std::vector<ErrorContext> context_;
};

template <typename T> class [[nodiscard]] Result final {
  public:
    static Result success(T value) {
        return Result(std::in_place_index<0>, std::move(value));
    }

    static Result failure(Error error) {
        return Result(std::in_place_index<1>, std::move(error));
    }

    [[nodiscard]] bool hasValue() const noexcept {
        return storage_.index() == 0;
    }
    [[nodiscard]] explicit operator bool() const noexcept {
        return hasValue();
    }

    T& value() & {
        ensureValue();
        return std::get<0>(storage_);
    }

    const T& value() const& {
        ensureValue();
        return std::get<0>(storage_);
    }

    T&& value() && {
        ensureValue();
        return std::move(std::get<0>(storage_));
    }

    Error& error() & {
        ensureError();
        return std::get<1>(storage_);
    }

    const Error& error() const& {
        ensureError();
        return std::get<1>(storage_);
    }

  private:
    template <std::size_t Index, typename Value>
    Result(std::in_place_index_t<Index> index, Value&& value)
        : storage_(index, std::forward<Value>(value)) {}

    void ensureValue() const {
        if (!hasValue()) {
            throw std::logic_error("attempted to read a failed Result");
        }
    }

    void ensureError() const {
        if (hasValue()) {
            throw std::logic_error("attempted to read the error of a successful Result");
        }
    }

    std::variant<T, Error> storage_;
};

template <> class [[nodiscard]] Result<void> final {
  public:
    static Result success() {
        return Result();
    }
    static Result failure(Error error) {
        return Result(std::move(error));
    }

    [[nodiscard]] bool hasValue() const noexcept {
        return !hasError_;
    }
    [[nodiscard]] explicit operator bool() const noexcept {
        return hasValue();
    }

    const Error& error() const {
        if (!hasError_) {
            throw std::logic_error("attempted to read the error of a successful Result");
        }
        return error_;
    }

  private:
    Result() = default;
    explicit Result(Error error) : error_(std::move(error)), hasError_(true) {}

    Error error_;
    bool hasError_ = false;
};

} // namespace fabgl
