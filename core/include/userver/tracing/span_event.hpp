#pragma once

/// @file userver/tracing/span_event.hpp
/// @brief @copybrief tracing::SpanEvent

#include <map>
#include <string>
#include <vector>

USERVER_NAMESPACE_BEGIN

namespace tracing {

struct SpanEventAttribute {
    struct ArrayValue {
        std::vector<int> values;
    };

    struct KeyValueList {
        std::map<std::string, std::string> key_value_pairs;
    };

    using ValueType = std::variant<std::string, bool, int64_t, double, ArrayValue, KeyValueList, std::vector<uint8_t>>;

    std::string key;
    ValueType value;
};

struct SpanEvent {
    SpanEvent(const std::string_view name, std::initializer_list<SpanEventAttribute>&& attributes = {})
        : time_unix_nano{static_cast<double>(
              std::chrono::nanoseconds(std::chrono::system_clock::now().time_since_epoch()).count()
          )},
          name{name},
          attributes{std::move(attributes)} {}

    double time_unix_nano;
    std::string name;
    std::vector<SpanEventAttribute> attributes;
};

}  // namespace tracing

USERVER_NAMESPACE_END
