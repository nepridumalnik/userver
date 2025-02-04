#pragma once

#include <atomic>
#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <type_traits>
#include <typeindex>
#include <typeinfo>
#include <unordered_map>

#include <userver/formats/json/value.hpp>
#include <userver/formats/json/value_builder.hpp>
#include <userver/utils/meta_light.hpp>
#include <userver/utils/statistics/writer.hpp>

USERVER_NAMESPACE_BEGIN

namespace utils::statistics::impl {

template <typename Metric>
formats::json::ValueBuilder DumpMetric(const std::atomic<Metric>& m) {
    static_assert(std::atomic<Metric>::is_always_lock_free, "std::atomic misuse");
    return m.load();
}

template <typename Metric>
void ResetMetric(std::atomic<Metric>& m) {
    static_assert(std::atomic<Metric>::is_always_lock_free, "std::atomic misuse");
    m = Metric();
}

template <typename Metric>
using HasDumpMetric = decltype(DumpMetric(std::declval<Metric&>()));

template <typename Metric>
using HasResetMetric = decltype(ResetMetric(std::declval<Metric&>()));

// TODO remove after C++20 atomic value-initialization
template <typename T>
void InitializeAtomic(T& /*value*/) {}

template <typename T>
void InitializeAtomic(std::atomic<T>& value) {
    value.store(T(), std::memory_order_relaxed);
}

class MetricWrapperBase {
public:
    MetricWrapperBase& operator=(MetricWrapperBase&&) = delete;
    virtual ~MetricWrapperBase();

    virtual formats::json::ValueBuilder DeprecatedJsonDump() = 0;

    virtual void DumpToWriter(utils::statistics::Writer& writer) = 0;

    virtual bool HasWriterSupport() const noexcept = 0;

    virtual void Reset() = 0;
};

template <typename Metric>
class MetricWrapper final : public MetricWrapperBase {
    static_assert(
        meta::kIsDetected<HasDumpMetric, Metric> || kHasWriterSupport<Metric>,
        "Provide a `void DumpMetric(utils::statistics::Writer&, const Metric&)`"
        "function in the namespace of `Metric`."
    );

    static_assert(!std::is_arithmetic_v<Metric>, "Type is not atomic, use std::atomic<T> instead");

public:
    template <typename... Args>
    explicit MetricWrapper(std::in_place_t, const Args&... args) : data_(args...) {
        if constexpr (sizeof...(Args) == 0) {
            InitializeAtomic(data_);
        }
    }

    formats::json::ValueBuilder DeprecatedJsonDump() override {
        if constexpr (!kHasWriterSupport<Metric>) {
            return DumpMetric(data_);
        }
        return {};
    }

    void DumpToWriter(Writer& writer) override {
        if constexpr (kHasWriterSupport<Metric>) {
            writer = data_;
        }
    }

    bool HasWriterSupport() const noexcept override { return kHasWriterSupport<Metric>; }

    void Reset() override {
        if constexpr (meta::kIsDetected<HasResetMetric, Metric>) {
            ResetMetric(data_);
        }
    }

    Metric& Get() { return data_; }

private:
    Metric data_;
};

using MetricFactory = std::function<std::unique_ptr<MetricWrapperBase>()>;

template <typename Metric, typename... Args>
// NOLINTNEXTLINE(cppcoreguidelines-missing-std-forward)
MetricFactory MakeMetricFactory(Args&&... args) {
    if constexpr (sizeof...(Args) == 0) {
        static_assert(
            std::is_default_constructible_v<Metric>,
            "Metric type is not default-constructible. You can pass "
            "additional args to MetricTag to forward them to Metric."
        );
    } else {
        static_assert(
            std::is_constructible_v<Metric, const Args&...>, "Metric type is not constructible from the given args"
        );
        static_assert(
            (true && ... && std::is_copy_constructible_v<std::decay_t<Args>>), "Metric args must be copy-constructible"
        );
    }

    return [args...] { return std::make_unique<MetricWrapper<Metric>>(std::in_place, args...); };
}

struct MetricKey final {
    std::type_index idx;
    std::string path;

    bool operator==(const MetricKey& other) const noexcept { return idx == other.idx && path == other.path; }
};

struct MetricKeyHash final {
    std::size_t operator()(const MetricKey& key) const noexcept;
};

using MetricMap = std::unordered_map<MetricKey, std::unique_ptr<MetricWrapperBase>, MetricKeyHash>;

void RegisterMetricInfo(const MetricKey& key, MetricFactory factory);

template <typename Metric>
Metric& GetMetric(MetricMap& metrics, const MetricKey& key) {
    return dynamic_cast<impl::MetricWrapper<Metric>&>(*metrics.at(key)).Get();
}

}  // namespace utils::statistics::impl

USERVER_NAMESPACE_END
