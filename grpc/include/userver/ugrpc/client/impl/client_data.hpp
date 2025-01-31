#pragma once

#include <cstddef>
#include <memory>
#include <optional>
#include <utility>

#include <grpcpp/completion_queue.h>

#include <userver/dynamic_config/snapshot.hpp>
#include <userver/rcu/rcu.hpp>
#include <userver/testsuite/grpc_control.hpp>
#include <userver/utils/fixed_array.hpp>

#include <userver/ugrpc/client/fwd.hpp>
#include <userver/ugrpc/client/impl/channel_factory.hpp>
#include <userver/ugrpc/client/impl/client_dependencies.hpp>
#include <userver/ugrpc/client/impl/stub_any.hpp>
#include <userver/ugrpc/client/impl/stub_pool.hpp>
#include <userver/ugrpc/client/middlewares/fwd.hpp>
#include <userver/ugrpc/impl/static_service_metadata.hpp>
#include <userver/ugrpc/impl/statistics.hpp>

USERVER_NAMESPACE_BEGIN

namespace ugrpc::client::impl {

struct GenericClientTag final {
    explicit GenericClientTag() = default;
};

/// The internal state of generated gRPC clients
class ClientData final {
public:
    struct StubState {
        StubPool stubs;
        // method_id -> stub_pool
        utils::FixedArray<StubPool> dedicated_stubs;
    };

    class StubHandle {
    public:
        StubHandle(rcu::ReadablePtr<StubState>&& state, StubAny& stub) : state_{std::move(state)}, stub_{stub} {}

        StubHandle(StubHandle&&) noexcept = default;
        StubHandle& operator=(StubHandle&&) = delete;

        StubHandle(const StubHandle&) = delete;
        StubHandle& operator=(const StubHandle&) = delete;

        template <typename Stub>
        Stub& Get() {
            return StubCast<Stub>(stub_);
        }

    private:
        rcu::ReadablePtr<StubState> state_;
        StubAny& stub_;
    };

    ClientData() = delete;

    template <typename Service>
    ClientData(ClientDependencies&& dependencies, ugrpc::impl::StaticServiceMetadata metadata, std::in_place_type_t<Service>)
        : dependencies_(std::move(dependencies)),
          metadata_(metadata),
          service_statistics_(&GetServiceStatistics()),
          channel_factory_(CreateChannelFactory(dependencies_)),
          stub_state_(std::make_unique<rcu::Variable<StubState>>()) {
        if (dependencies_.qos) {
            SubscribeOnConfigUpdate<Service>(*dependencies_.qos);
        } else {
            ConstructStubState<typename Service::Stub>();
        }
    }

    template <typename Service>
    ClientData(ClientDependencies&& dependencies, GenericClientTag, std::in_place_type_t<Service>)
        : dependencies_(std::move(dependencies)),
          channel_factory_(CreateChannelFactory(dependencies_)),
          stub_state_(std::make_unique<rcu::Variable<StubState>>()) {
        ConstructStubState<typename Service::Stub>();
    }

    ~ClientData();

    ClientData(ClientData&&) noexcept = default;
    ClientData& operator=(ClientData&&) = delete;

    ClientData(const ClientData&) = delete;
    ClientData& operator=(const ClientData&) = delete;

    StubHandle NextStubFromMethodId(std::size_t method_id) const {
        auto stub_state = stub_state_->Read();
        auto& dedicated_stubs = stub_state->dedicated_stubs[method_id];
        auto& stubs = dedicated_stubs.Size() ? dedicated_stubs : stub_state->stubs;
        auto& stub = stubs.NextStub();
        return StubHandle{std::move(stub_state), stub};
    }

    StubHandle NextStub() const {
        auto stub_state = stub_state_->Read();
        auto& stub = stub_state->stubs.NextStub();
        return StubHandle{std::move(stub_state), stub};
    }

    grpc::CompletionQueue& NextQueue() const;

    dynamic_config::Snapshot GetConfigSnapshot() const { return dependencies_.config_source.GetSnapshot(); }

    ugrpc::impl::MethodStatistics& GetStatistics(std::size_t method_id) const;

    ugrpc::impl::MethodStatistics& GetGenericStatistics(std::string_view call_name) const;

    std::string_view GetClientName() const { return dependencies_.client_name; }

    const Middlewares& GetMiddlewares() const { return dependencies_.mws; }

    const ugrpc::impl::StaticServiceMetadata& GetMetadata() const;

    const testsuite::GrpcControl& GetTestsuiteControl() const { return dependencies_.testsuite_grpc; }

    const dynamic_config::Key<ClientQos>* GetClientQos() const;

    rcu::ReadablePtr<StubState> GetStubState() const { return stub_state_->Read(); }

private:
    static ChannelFactory CreateChannelFactory(const ClientDependencies& dependencies);

    template <typename Stub>
    static utils::FixedArray<StubPool> MakeDedicatedStubs(
        const ChannelFactory& channel_factory,
        const ugrpc::impl::StaticServiceMetadata& metadata,
        const DedicatedMethodsConfig& dedicated_methods_config
    ) {
        return utils::GenerateFixedArray(GetMethodsCount(metadata), [&](std::size_t method_id) {
            const auto method_channel_count =
                GetMethodChannelCount(dedicated_methods_config, GetMethodName(metadata, method_id));
            return StubPool::Create<Stub>(method_channel_count, channel_factory);
        });
    }

    ugrpc::impl::ServiceStatistics& GetServiceStatistics();

    template <typename Service>
    void SubscribeOnConfigUpdate(const dynamic_config::Key<ClientQos>& qos) {
        config_subscription_ = dependencies_.config_source.UpdateAndListen(
            this, dependencies_.client_name, &ClientData::OnConfigUpdate<Service>, qos
        );
    }

    template <typename Service>
    void OnConfigUpdate(const dynamic_config::Snapshot& /*config*/) {
        ConstructStubState<typename Service::Stub>();
    }

    template <typename Stub>
    void ConstructStubState() {
        auto stubs = StubPool::Create<Stub>(dependencies_.client_factory_settings.channel_count, channel_factory_);

        auto dedicated_stubs =
            metadata_.has_value()
                ? MakeDedicatedStubs<Stub>(channel_factory_, *metadata_, dependencies_.dedicated_methods_config)
                : utils::FixedArray<StubPool>{};

        stub_state_->Assign({std::move(stubs), std::move(dedicated_stubs)});
    }

    ClientDependencies dependencies_;
    std::optional<ugrpc::impl::StaticServiceMetadata> metadata_{std::nullopt};
    ugrpc::impl::ServiceStatistics* service_statistics_{nullptr};

    ChannelFactory channel_factory_;
    std::unique_ptr<rcu::Variable<StubState>> stub_state_;

    // These fields must be the last ones
    concurrent::AsyncEventSubscriberScope config_subscription_;
};

template <typename Client>
ClientData& GetClientData(Client& client) {
    return client.impl_;
}

}  // namespace ugrpc::client::impl

USERVER_NAMESPACE_END
