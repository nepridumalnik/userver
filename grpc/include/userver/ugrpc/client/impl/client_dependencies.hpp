#pragma once

#include <string>

#include <userver/dynamic_config/source.hpp>
#include <userver/engine/task/task_processor_fwd.hpp>
#include <userver/testsuite/grpc_control.hpp>

#include <userver/ugrpc/client/client_factory_settings.hpp>
#include <userver/ugrpc/client/client_settings.hpp>
#include <userver/ugrpc/client/middlewares/fwd.hpp>

USERVER_NAMESPACE_BEGIN

namespace ugrpc::impl {
class StatisticsStorage;
class CompletionQueuePoolBase;
}  // namespace ugrpc::impl

namespace ugrpc::client::impl {

/// Contains all non-code-generated dependencies for creating a gRPC client
struct ClientDependencies final {
    std::string client_name;
    std::string endpoint;
    Middlewares mws;
    ugrpc::impl::CompletionQueuePoolBase& completion_queues;
    ugrpc::impl::StatisticsStorage& statistics_storage;
    dynamic_config::Source config_source;
    testsuite::GrpcControl& testsuite_grpc;
    const dynamic_config::Key<ClientQos>* qos{nullptr};
    const ClientFactorySettings& client_factory_settings;
    engine::TaskProcessor& channel_task_processor;
    DedicatedMethodsConfig dedicated_methods_config;
};

}  // namespace ugrpc::client::impl

USERVER_NAMESPACE_END
