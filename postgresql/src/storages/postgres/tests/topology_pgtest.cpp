#include <gtest/gtest.h>

#include <engine/standalone.hpp>
#include <storages/postgres/detail/connection.hpp>
#include <storages/postgres/detail/quorum_commit.hpp>
#include <storages/postgres/exceptions.hpp>

#include <storages/postgres/tests/util_pgtest.hpp>

namespace pg = storages::postgres;

std::vector<pg::Dsn> GetClusterFromEnv() {
  const auto* cluster_dsn = std::getenv("POSTGRES_CLUSTER_TEST");
  return cluster_dsn ? std::vector<pg::Dsn>{pg::Dsn{cluster_dsn}}
                     : std::vector<pg::Dsn>{};
}

class PostgreTopology : public PostgreSQLBase,
                        public ::testing::WithParamInterface<pg::Dsn> {};

TEST_P(PostgreTopology, Smoke) {
  auto dsns = pg::SplitByHost(GetParam());
  ASSERT_FALSE(dsns.empty());

  if (dsns.size() < 2) {
    LOG_WARNING() << "Running topology test with a single host is useless";
  }
  RunInCoro([&dsns] {
    error_injection::Settings ei_settings;
    pg::detail::QuorumCommitTopology qcc(
        GetTaskProcessor(), dsns, pg::TopologySettings{kMaxTestWaitTime},
        pg::ConnectionSettings{}, kTestCmdCtl, {}, ei_settings);
    auto hosts = qcc.GetDsnIndicesByType();

    EXPECT_EQ(1, hosts->count(pg::ClusterHostType::kMaster));
    if (dsns.size() > 1) {
      // Should detect slaves
      EXPECT_EQ(1, hosts->count(pg::ClusterHostType::kSlave));
      EXPECT_LT(0, hosts->at(pg::ClusterHostType::kSlave).size());
    }
  });
}

TEST_P(PostgreTopology, ReplicationLag) {
  auto dsns = pg::SplitByHost(GetParam());
  ASSERT_FALSE(dsns.empty());

  if (dsns.size() < 2) {
    LOG_WARNING() << "Running topology test with a single host is useless";
  }
  RunInCoro([&dsns] {
    error_injection::Settings ei_settings;
    pg::detail::QuorumCommitTopology qcc(
        GetTaskProcessor(), dsns,
        pg::TopologySettings{std::chrono::seconds{-1}},
        pg::ConnectionSettings{}, kTestCmdCtl, {}, ei_settings);
    auto hosts = qcc.GetDsnIndicesByType();

    EXPECT_EQ(1, hosts->count(pg::ClusterHostType::kMaster));
    // Slaves should be excluded due to unsatisfied lag requirements
    EXPECT_EQ(0, hosts->count(pg::ClusterHostType::kSyncSlave));
    EXPECT_EQ(0, hosts->count(pg::ClusterHostType::kSlave));
  });
}

INSTANTIATE_TEST_SUITE_P(/*empty*/, PostgreTopology,
                         ::testing::ValuesIn(GetClusterFromEnv()), DsnToString);
