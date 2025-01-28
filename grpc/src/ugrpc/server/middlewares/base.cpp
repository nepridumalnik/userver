#include <userver/ugrpc/server/middlewares/base.hpp>

#include <userver/components/component_config.hpp>
#include <userver/components/component_context.hpp>

#include <userver/ugrpc/server/impl/exceptions.hpp>

USERVER_NAMESPACE_BEGIN

namespace ugrpc::server {

MiddlewareBase::MiddlewareBase() = default;

MiddlewareBase::~MiddlewareBase() = default;

MiddlewareCallContext::MiddlewareCallContext(
    const Middlewares& middlewares,
    CallAnyBase& call,
    utils::function_ref<void()> user_call,
    const dynamic_config::Snapshot& config,
    ::google::protobuf::Message* request
)
    : middleware_(middlewares.begin()),
      middleware_end_(middlewares.end()),
      user_call_(std::move(user_call)),
      call_(call),
      config_(config),
      request_(request) {}

void MiddlewareCallContext::Next() {
    if (is_called_from_handle_) {
        // It is important for non-stream calls
        if (request_) {
            (*middleware_)->CallRequestHook(*this, *request_);
            if (call_.IsFinished()) throw impl::MiddlewareRpcInterruptionError();
        }
        ++middleware_;
    }
    if (middleware_ == middleware_end_) {
        ClearMiddlewaresResources();
        user_call_();
    } else {
        is_called_from_handle_ = true;
        (*middleware_)->Handle(*this);
    }
}

void MiddlewareCallContext::ClearMiddlewaresResources() {
    UASSERT(config_);
    config_.reset();
}

CallAnyBase& MiddlewareCallContext::GetCall() const { return call_; }

const dynamic_config::Snapshot& MiddlewareCallContext::GetInitialDynamicConfig() const {
    UASSERT(config_);
    return config_.value();
}

void MiddlewareBase::CallRequestHook(const MiddlewareCallContext&, google::protobuf::Message&) {}

void MiddlewareBase::CallResponseHook(const MiddlewareCallContext&, google::protobuf::Message&) {}

MiddlewareComponentBase::MiddlewareComponentBase(
    const components::ComponentConfig& config,
    const components::ComponentContext& context,
    MiddlewareDependencyBuilder&& dependency
)
    : components::ComponentBase(config, context), dependency_(std::move(dependency).Extract(config.Name())) {}

const impl::MiddlewareDependency& MiddlewareComponentBase::GetMiddlewareDependency() const { return dependency_; }

}  // namespace ugrpc::server

USERVER_NAMESPACE_END
