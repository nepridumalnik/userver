#include "span_impl.hpp"

#include <boost/container/small_vector.hpp>

#include <userver/formats/json/string_builder.hpp>
#include <userver/logging/impl/tag_writer.hpp>
#include <userver/logging/log_extra.hpp>
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

struct EventAttributeWriteVisitor {
    explicit EventAttributeWriteVisitor(formats::json::StringBuilder& builder) : builder{builder} {}

    template <typename T>
    void operator()(const T& value) {
        if constexpr (std::is_same_v<T, std::string>) {
            builder.WriteString(value);
        } else if constexpr (std::is_integral_v<T> && std::is_unsigned_v<T>) {
            builder.WriteUInt64(value);
        } else if constexpr (std::is_integral_v<T>) {
            builder.WriteInt64(value);
        } else if constexpr (std::is_floating_point_v<T>) {
            builder.WriteDouble(value);
        }
    }

    formats::json::StringBuilder& builder;
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

void HandleEventAttributes(const Span::Event& events, formats::json::StringBuilder& builder) {
    builder.Key("attributes");
    const formats::json::StringBuilder::ObjectGuard attributes_guard(builder);

    for (const auto& [key, value] : events.attributes) {
        builder.Key(key);
        EventAttributeWriteVisitor write_visitor(builder);
        std::visit(write_visitor, value);
    }
}

std::string MakeTagFromEvents(const std::vector<Span::Event>& events) {
    formats::json::StringBuilder builder;
    {
        const formats::json::StringBuilder::ArrayGuard array_guard(builder);

        for (const auto& event : events) {
            const formats::json::StringBuilder::ObjectGuard guard(builder);

            builder.Key("name");
            builder.WriteString(event.name);
            builder.Key("time_unix_nano");
            builder.WriteUInt64(event.time_unix_nano);

            HandleEventAttributes(event, builder);
        }
    }

    return builder.GetString();
}

constexpr std::string_view kOperationName = "operation_name";
constexpr std::string_view kTraceId = "trace_id";
constexpr std::string_view kParentId = "parent_id";
constexpr std::string_view kSpanId = "span_id";
constexpr std::string_view kServiceName = "service_name";

constexpr std::string_view kStartTime = "start_time";
constexpr std::string_view kStartTimeMillis = "start_time_millis";
constexpr std::string_view kDuration = "duration";

constexpr std::string_view kTags = "tags";
constexpr std::string_view kEvents = "events";

}  // namespace jaeger

}  // namespace

void Span::Impl::LogOpenTracing() const {
    if (!tracer_) {
        return;
    }

    auto logger = tracer_->GetOptionalLogger();
    if (logger) {
        const impl::DetachLocalSpansScope ignore_local_span;
        logging::LogHelper lh(
            *logger, log_level_, logging::Module(utils::impl::SourceLocation::Current()), logging::LogClass::kTrace
        );
        DoLogOpenTracing(lh.GetTagWriter());
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

    formats::json::StringBuilder tags;
    {
        const formats::json::StringBuilder::ArrayGuard guard(tags);
        AddOpentracingTags(tags, log_extra_inheritable_);
        if (log_extra_local_) {
            AddOpentracingTags(tags, *log_extra_local_);
        }
    }
    writer.PutTag(jaeger::kTags, tags.GetStringView());

    if (!events_.empty()) {
        const auto events_tag = jaeger::MakeTagFromEvents(events_);
        writer.PutTag(jaeger::kEvents, events_tag);
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
