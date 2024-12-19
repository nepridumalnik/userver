#include "span_impl.hpp"

#include <boost/container/small_vector.hpp>

#include <userver/formats/json/string_builder.hpp>
#include <userver/logging/impl/tag_writer.hpp>
#include <userver/logging/log_extra.hpp>
#include <userver/tracing/span_event.hpp>
#include <userver/tracing/tags.hpp>
#include <userver/utils/trivial_map.hpp>

#include <logging/log_helper_impl.hpp>

USERVER_NAMESPACE_BEGIN

namespace tracing {

namespace {
namespace jaeger {

struct OpentracingTag {
    std::string_view opentracing_name;
    std::string_view type;
};

constexpr utils::TrivialBiMap kGetOpentracingTags = [](auto selector) {
    using Tag = OpentracingTag;
    return selector()
        .Case(kHttpStatusCode, Tag{"http.status_code", "int64"})
        .Case(kErrorFlag, Tag{"error", "bool"})
        .Case(kHttpMethod, Tag{"http.method", "string"})
        .Case(kHttpUrl, Tag{"http.url", "string"})

        .Case(kDatabaseType, Tag{"db.type", "string"})
        .Case(kDatabaseStatement, Tag{"db.statement", "string"})
        .Case(kDatabaseInstance, Tag{"db.instance", "string"})
        .Case(kDatabaseStatementName, Tag{"db.statement_name", "string"})
        .Case(kDatabaseCollection, Tag{"db.collection", "string"})
        .Case(kDatabaseStatementDescription, Tag{"db.query_description", "string"})

        .Case(kPeerAddress, Tag{"peer.address", "string"});
};

struct LogExtraValueVisitor {
    std::string string_value;

    void operator()(const std::string& val) { string_value = val; }

    void operator()(int val) { string_value = std::to_string(val); }
};

void GetTagObject(
    formats::json::StringBuilder& builder,
    std::string_view key,
    const logging::LogExtra::Value& value,
    std::string_view type
) {
    const formats::json::StringBuilder::ObjectGuard guard(builder);
    LogExtraValueVisitor visitor;
    std::visit(visitor, value);

    builder.Key("value");
    builder.WriteString(visitor.string_value);

    builder.Key("type");
    builder.WriteString(type);

    builder.Key("key");
    builder.WriteString(key);
}

constexpr std::string_view kOperationName = "operation_name";
constexpr std::string_view kTraceId = "trace_id";
constexpr std::string_view kParentId = "parent_id";
constexpr std::string_view kSpanId = "span_id";
constexpr std::string_view kServiceName = "service_name";

constexpr std::string_view kStartTime = "start_time";
constexpr std::string_view kStartTimeMillis = "start_time_millis";
constexpr std::string_view kDuration = "duration";

}  // namespace jaeger

void WriteEventAttributes(
    const std::vector<tracing::SpanEventAttribute>& attributes,
    formats::json::StringBuilder& attributes_builder
) {
    const formats::json::StringBuilder::ArrayGuard attributes_guard(attributes_builder);

    for (const auto& attribute : attributes) {
        formats::json::StringBuilder attribute_object;
        const formats::json::StringBuilder::ObjectGuard attribute_guard(attribute_object);

        attribute_object.Key("key");
        attribute_object.WriteString(attribute.key);

        attribute_object.Key("value");
        std::visit(
            [&attribute_object](const auto& value) {
                using T = std::decay_t<decltype(value)>;

                if constexpr (std::is_same_v<T, std::string>) {
                    attribute_object.WriteString(value);
                } else if constexpr (std::is_same_v<T, bool>) {
                    attribute_object.WriteBool(value);
                } else if constexpr (std::is_same_v<T, int64_t>) {
                    attribute_object.WriteInt64(value);
                } else if constexpr (std::is_same_v<T, double>) {
                    attribute_object.WriteDouble(value);
                } else if constexpr (std::is_same_v<T, SpanEventAttribute::ArrayValue>) {
                    formats::json::StringBuilder array_builder;
                    const formats::json::StringBuilder::ArrayGuard array_guard(array_builder);
                    for (const auto& array_value : value.values) {
                        array_builder.WriteInt64(array_value);
                    }
                    attribute_object.WriteRawString(array_builder.GetStringView());
                } else if constexpr (std::is_same_v<T, SpanEventAttribute::KeyValueList>) {
                    formats::json::StringBuilder kv_builder;
                    const formats::json::StringBuilder::ObjectGuard kv_guard(kv_builder);
                    for (const auto& [key, val] : value.key_value_pairs) {
                        kv_builder.Key(key);
                        kv_builder.WriteString(val);
                    }
                    attribute_object.WriteRawString(kv_builder.GetStringView());
                } else if constexpr (std::is_same_v<T, std::vector<uint8_t>>) {
                    formats::json::StringBuilder byte_builder;
                    const formats::json::StringBuilder::ArrayGuard byte_guard(byte_builder);
                    for (const auto& byte : value) {
                        byte_builder.WriteInt64(static_cast<int>(byte));
                    }
                    attribute_object.WriteRawString(byte_builder.GetStringView());
                }
            },
            attribute.value
        );

        attributes_builder.WriteRawString(attribute_object.GetStringView());
    }
}

}  // namespace

void Span::Impl::LogOpenTracing() const {
    if (!tracer_) {
        return;
    }

    auto logger = tracer_->GetOptionalLogger();
    if (logger) {
        const impl::DetachLocalSpansScope ignore_local_span;
        logging::LogHelper lh(*logger, log_level_);
        DoLogOpenTracing(lh.GetTagWriterAfterText({}));
    }
}

void Span::Impl::DoLogOpenTracing(logging::impl::TagWriter writer) const {
    const auto steady_now = std::chrono::steady_clock::now();
    const auto duration = steady_now - start_steady_time_;
    const auto duration_microseconds = std::chrono::duration_cast<std::chrono::microseconds>(duration).count();
    auto start_time =
        std::chrono::duration_cast<std::chrono::microseconds>(start_system_time_.time_since_epoch()).count();

    if (tracer_) {
        writer.PutTag(jaeger::kServiceName, tracer_->GetServiceName());
    }
    writer.PutTag(jaeger::kTraceId, trace_id_);
    writer.PutTag(jaeger::kParentId, parent_id_);
    writer.PutTag(jaeger::kSpanId, span_id_);
    writer.PutTag(jaeger::kStartTime, start_time);
    writer.PutTag(jaeger::kStartTimeMillis, start_time / 1000);
    writer.PutTag(jaeger::kDuration, duration_microseconds);
    writer.PutTag(jaeger::kOperationName, name_);

    {
        formats::json::StringBuilder tags;
        const formats::json::StringBuilder::ArrayGuard guard(tags);
        AddOpentracingTags(tags, log_extra_inheritable_);
        if (log_extra_local_) {
            AddOpentracingTags(tags, *log_extra_local_);
        }
        writer.PutTag("tags", tags.GetStringView());
    }

    {
        formats::json::StringBuilder events;
        const formats::json::StringBuilder::ArrayGuard guard(events);

        // TODO: Implement events serialization
        for (const auto& event : events_) {
            formats::json::StringBuilder event_object;
            const formats::json::StringBuilder::ObjectGuard event_guard(event_object);

            event_object.Key("name");
            event_object.WriteString(event.name);

            event_object.Key("time_unix_nano");
            event_object.WriteDouble(event.time_unix_nano);

            WriteEventAttributes(event.attributes, event_object);
        }

        writer.PutTag("events", events.GetStringView());
    }
}

void Span::Impl::AddOpentracingTags(formats::json::StringBuilder& output, const logging::LogExtra& input) {
    for (const auto& [key, value] : *input.extra_) {
        const auto tag_it = jaeger::kGetOpentracingTags.TryFind(key);
        if (tag_it) {
            const auto& tag = *tag_it;
            jaeger::GetTagObject(output, tag.opentracing_name, value.GetValue(), tag.type);
        }
    }
}

}  // namespace tracing

USERVER_NAMESPACE_END
