#pragma once

#include <userver/formats/json/string_builder.hpp>

#include <ostream>

USERVER_NAMESPACE_BEGIN

/// @brief Wrapper above bool type.
class Boolean {
public:
    /// @brief Default constructor.
    inline explicit Boolean(bool value) : value_(value) {}

    /// @brief Assign operator.
    Boolean& operator=(bool value) {
        value_ = value;
        return *this;
    }

    /// @brief Operator that cast into bool.
    constexpr inline operator bool() const { return value_; }

    /// @brief Operators for std::ostream.
    friend std::ostream& operator<<(std::ostream& os, const Boolean& b) { return os << b.value_; }

private:
    /// @brief Contained value.
    bool value_ = false;
};

USERVER_NAMESPACE_END
