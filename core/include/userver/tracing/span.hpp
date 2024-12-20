#pragma once

/// @file userver/tracing/span.hpp
/// @brief @copybrief tracing::Span

#include <optional>
#include <string_view>

#include <userver/logging/log.hpp>
#include <userver/logging/log_extra.hpp>
#include <userver/tracing/scope_time.hpp>
#include <userver/tracing/tracer_fwd.hpp>
#include <userver/utils/impl/internal_tag.hpp>
#include <userver/utils/impl/source_location.hpp>

USERVER_NAMESPACE_BEGIN

namespace tracing {

class SpanBuilder;

/// @brief Measures the execution time of the current code block, links it with
/// the parent tracing::Spans and stores that info in the log.
///
/// Logging of spans can be controled at runtime via @ref USERVER_NO_LOG_SPANS.
///
/// See @ref scripts/docs/en/userver/logging.md for usage examples and more
/// descriptions.
///
/// @warning Shall be created only as a local variable. Do not use it as a
/// class member!
class Span final {
public:
    class Impl;

    struct Event {
        Event(
            const std::string_view name,
            double time_unix_nano = std::chrono::system_clock::now().time_since_epoch().count()
        );

        Event() = default;

        double time_unix_nano{};
        std::string name;
    };

    explicit Span(
        TracerPtr tracer,
        std::string name,
        const Span* parent,
        ReferenceType reference_type,
        logging::Level log_level = logging::Level::kInfo,
        utils::impl::SourceLocation source_location = utils::impl::SourceLocation::Current()
    );

    /// Use default tracer and implicit coro local storage for parent
    /// identification, takes TraceID from the parent.
    ///
    /// For extremely rare cases where a new Trace ID is required use
    /// tracing::Span::MakeSpan().
    explicit Span(
        std::string name,
        ReferenceType reference_type = ReferenceType::kChild,
        logging::Level log_level = logging::Level::kInfo,
        utils::impl::SourceLocation source_location = utils::impl::SourceLocation::Current()
    );

    /// @cond
    // For internal use only
    explicit Span(Span::Impl& impl);
    /// @endcond

    Span(Span&& other) noexcept;

    ~Span();

    Span& operator=(const Span&) = delete;

    Span& operator=(Span&&) = delete;

    /// @brief Returns the Span of the current task.
    ///
    /// Should not be called in non-coroutine
    /// context. Should not be called from a task with no alive Span.
    ///
    /// Rule of thumb: it is safe to call it from a task created by
    /// utils::Async/utils::CriticalAsync/utils::PeriodicTask. If current task was
    /// created with an explicit engine::impl::*Async(), you have to create a Span
    /// beforehand.
    static Span& CurrentSpan();

    /// @brief Returns nullptr if called in non-coroutine context or from a task
    /// with no alive Span; otherwise returns the Span of the current task.
    static Span* CurrentSpanUnchecked();

    /// Factory function for extremely rare cases of creating a Span with custom
    /// IDs; prefer Span constructor instead.
    ///
    /// @return A new Span attached to current Span (if any) but with a new
    /// Trace ID.
    /// @param name Name of a new Span
    /// @param trace_id New Trace ID; if empty then the Trace ID is autogenerated
    /// @param parent_span_id Id of the parent Span, could be empty.
    static Span MakeSpan(std::string name, std::string_view trace_id, std::string_view parent_span_id);

    /// Factory function for extremely rare cases of creating a Span with custom
    /// IDs; prefer Span constructor instead.
    ///
    /// @return A new Span attached to current Span (if any), sets `link`.
    /// @param name Name of a new Span
    /// @param trace_id New Trace ID; if empty then the Trace ID is autogenerated
    /// @param parent_span_id Id of the parent Span, could be empty.
    /// @param link The new link
    static Span
    MakeSpan(std::string name, std::string_view trace_id, std::string_view parent_span_id, std::string link);

    /// Factory function for rare cases of creating a root Span that starts
    /// the trace_id chain, ignoring `CurrentSpan`, if any. Useful
    /// in background jobs, periodics, distlock tasks, cron tasks, etc.
    /// The result of such jobs is not directly requested by anything.
    ///
    /// @return A new Span that is the root of a new Span hierarchy.
    /// @param name Name of a new Span
    /// @param log_level Log level for the span's own log record
    static Span MakeRootSpan(std::string name, logging::Level log_level = logging::Level::kInfo);

    /// Create a child which can be used independently from the parent.
    ///
    /// The child shares no state with its parent. If you need to run code in
    /// parallel, create a child span and use the child in a separate task.
    Span CreateChild(std::string name) const;

    Span CreateFollower(std::string name) const;

    /// @brief Creates a tracing::ScopeTime attached to the span.
    ScopeTime CreateScopeTime();

    /// @brief Creates a tracing::ScopeTime attached to the Span and starts
    /// measuring execution time.
    /// Tag `{scope_name}_time` with elapsed time is added to result span.
    ///
    /// @note `name` parameter is expected to satisfy snake case.
    /// Otherwise, it is converted to snake case.
    ScopeTime CreateScopeTime(std::string name);

    /// Returns total time elapsed for a certain scope of this span.
    /// If there is no record for the scope, returns 0.
    ScopeTime::Duration GetTotalDuration(const std::string& scope_name) const;

    /// Returns total time elapsed for a certain scope of this span.
    /// If there is no record for the scope, returns 0.
    ///
    /// Prefer using Span::GetTotalDuration()
    ScopeTime::DurationMillis GetTotalElapsedTime(const std::string& scope_name) const;

    /// Add a tag that is used on each logging in this Span and all
    /// future children.
    void AddTag(std::string key, logging::LogExtra::Value value);

    /// Add a tag that is used on each logging in this Span and all
    /// future children. It will not be possible to change its value.
    void AddTagFrozen(std::string key, logging::LogExtra::Value value);

    /// Add a tag that is local to the Span (IOW, it is not propagated to
    /// future children) and logged only once in the destructor of the Span.
    void AddNonInheritableTag(std::string key, logging::LogExtra::Value value);

    /// @overload AddNonInheritableTag
    void AddNonInheritableTags(const logging::LogExtra&);

    /// Add an event to Span
    void AddEvent(const std::string_view event_name);

    /// @brief Sets level for tags logging
    void SetLogLevel(logging::Level log_level);

    /// @brief Returns level for tags logging
    logging::Level GetLogLevel() const;

    /// @brief Sets the local log level that disables logging of this span if
    /// the local log level set and greater than the main log level of the Span.
    void SetLocalLogLevel(std::optional<logging::Level> log_level);

    /// @brief Returns the local log level that disables logging of this span if
    /// it is set and greater than the main log level of the Span.
    std::optional<logging::Level> GetLocalLogLevel() const;

    /// Set link - a request ID within a service. Can be called only once.
    ///
    /// Propagates within a single service, but not from client to server. A new
    /// link is generated for the "root" request handling task
    void SetLink(std::string link);

    /// Set parent_link - an ID . Can be called only once.
    void SetParentLink(std::string parent_link);

    /// Get link - a request ID within the service.
    ///
    /// Propagates within a single service, but not from client to server. A new
    /// link is generated for the "root" request handling task
    std::string GetLink() const;

    std::string GetParentLink() const;

    /// An ID of the request that does not change from service to service.
    ///
    /// Propagates both to sub-spans within a single service, and from client
    /// to server
    const std::string& GetTraceId() const;

    /// Identifies a specific span. It does not propagate
    const std::string& GetSpanId() const;
    const std::string& GetParentId() const;

    /// @returns true if this span would be logged with the current local and
    /// global log levels to the default logger.
    bool ShouldLogDefault() const noexcept;

    /// Detach the Span from current engine::Task so it is not
    /// returned by CurrentSpan() any more.
    void DetachFromCoroStack();

    /// Attach the Span to current engine::Task so it is returned
    /// by CurrentSpan().
    void AttachToCoroStack();

    std::chrono::system_clock::time_point GetStartSystemTime() const;

    /// @cond
    // For internal use only.
    void AddTags(const logging::LogExtra&, utils::impl::InternalTag);

    // For internal use only.
    impl::TimeStorage& GetTimeStorage(utils::impl::InternalTag);

    // For internal use only.
    void LogTo(logging::impl::TagWriter writer) const&;
    /// @endcond

private:
    struct OptionalDeleter {
        void operator()(Impl*) const noexcept;

        static OptionalDeleter ShouldDelete() noexcept;

        static OptionalDeleter DoNotDelete() noexcept;

    private:
        explicit OptionalDeleter(bool do_delete) : do_delete(do_delete) {}

        const bool do_delete;
    };

    friend class SpanBuilder;
    friend class TagScope;

    explicit Span(std::unique_ptr<Impl, OptionalDeleter>&& pimpl);

    std::string GetTag(std::string_view tag) const;

    std::unique_ptr<Impl, OptionalDeleter> pimpl_;
};

namespace impl {

class DetachLocalSpansScope final {
public:
    DetachLocalSpansScope() noexcept;
    ~DetachLocalSpansScope();

    DetachLocalSpansScope(DetachLocalSpansScope&&) = delete;
    DetachLocalSpansScope& operator=(DetachLocalSpansScope&&) = delete;

private:
    struct Impl;
    utils::FastPimpl<Impl, 16, 8> impl_;
};

struct LogSpanAsLastNoCurrent final {
    const Span& span;
};

logging::LogHelper& operator<<(logging::LogHelper& lh, LogSpanAsLastNoCurrent span);

}  // namespace impl

}  // namespace tracing

USERVER_NAMESPACE_END
