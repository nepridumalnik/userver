#pragma once

#include <atomic>
#include <memory>
#include <vector>

#include <boost/lockfree/queue.hpp>

#include <engine/condition_variable.hpp>
#include <engine/task/task_processor.hpp>
#include <engine/task/task_with_result.hpp>
#include <rcu/rcu.hpp>
#include <utils/size_guard.hpp>

#include <storages/postgres/detail/connection.hpp>
#include <storages/postgres/detail/connection_ptr.hpp>
#include <storages/postgres/detail/non_transaction.hpp>
#include <storages/postgres/options.hpp>
#include <storages/postgres/statistics.hpp>
#include <storages/postgres/transaction.hpp>

namespace storages {
namespace postgres {
namespace detail {

class ConnectionPoolImpl
    : public std::enable_shared_from_this<ConnectionPoolImpl> {
 public:
  static std::shared_ptr<ConnectionPoolImpl> Create(
      const std::string& dsn, engine::TaskProcessor& bg_task_processor,
      size_t initial_size, size_t max_size, CommandControl default_cmd_ctl);
  ~ConnectionPoolImpl();

  std::string const& GetDsn() const { return dsn_; }

  [[nodiscard]] ConnectionPtr Acquire(engine::Deadline);
  void Release(Connection* connection);

  const InstanceStatistics& GetStatistics() const;
  [[nodiscard]] Transaction Begin(const TransactionOptions& options,
                                  engine::Deadline deadline,
                                  OptionalCommandControl trx_cmd_ctl = {});

  [[nodiscard]] NonTransaction Start(engine::Deadline deadline);

  void SetDefaultCommandControl(CommandControl);

 protected:
  ConnectionPoolImpl(const std::string& dsn,
                     engine::TaskProcessor& bg_task_processor, size_t max_size,
                     CommandControl default_cmd_ctl);

 private:
  using SizeGuard = ::utils::SizeGuard<std::atomic<size_t>>;
  using SharedCounter = std::shared_ptr<std::atomic<size_t>>;
  using SharedSizeGuard = ::utils::SizeGuard<SharedCounter>;

  void Init(size_t initial_size);

  [[nodiscard]] engine::TaskWithResult<bool> Connect(SharedSizeGuard&&);

  void Push(Connection* connection);
  Connection* Pop(engine::Deadline);

  void Clear();
  void DeleteConnection(Connection* connection);

  void AccountConnectionStats(Connection::Statistics stats);

 private:
  using RecentCounter = ::utils::statistics::RecentPeriod<
      ::utils::statistics::RelaxedCounter<size_t>, size_t>;

  mutable InstanceStatistics stats_;
  std::string dsn_;
  engine::TaskProcessor& bg_task_processor_;
  size_t max_size_;
  engine::Mutex wait_mutex_;
  engine::ConditionVariable conn_available_;
  boost::lockfree::queue<Connection*> queue_;
  SharedCounter size_;
  std::atomic<size_t> wait_count_;
  rcu::Variable<CommandControl> default_cmd_ctl_;
  RecentCounter recent_conn_errors_;
};

}  // namespace detail
}  // namespace postgres
}  // namespace storages
