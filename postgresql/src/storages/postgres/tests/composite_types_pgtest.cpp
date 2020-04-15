#include <storages/postgres/io/composite_types.hpp>
#include <storages/postgres/tests/util_pgtest.hpp>

namespace pg = storages::postgres;
namespace io = pg::io;
namespace tt = io::traits;

namespace {

constexpr const char* const kSchemaName = "__pgtest";
const std::string kCreateTestSchema = "create schema if not exists __pgtest";
const std::string kDropTestSchema = "drop schema if exists __pgtest cascade";

constexpr pg::DBTypeName kCompositeName{kSchemaName, "foobar"};
const std::string kCreateACompositeType = R"~(
create type __pgtest.foobar as (
  i integer,
  s text,
  d double precision,
  a integer[],
  v varchar[]
))~";

const std::string kCreateCompositeOfComposites = R"~(
create type __pgtest.foobars as (
  f __pgtest.foobar[]
))~";

}  // namespace

/*! [User type declaration] */
namespace pgtest {

struct FooBar {
  int i;
  std::string s;
  double d;
  std::vector<int> a;
  std::vector<std::string> v;

  bool operator==(const FooBar& rhs) const {
    return i == rhs.i && s == rhs.s && d == rhs.d && a == rhs.a && v == rhs.v;
  }
};

using FooBarOpt = boost::optional<FooBar>;

class FooClass {
  int i;
  std::string s;
  double d;
  std::vector<int> a;
  std::vector<std::string> v;

 public:
  FooClass() = default;
  explicit FooClass(int x) : i(x), s(std::to_string(x)), d(x), a{i}, v{s} {}

  auto Introspect() { return std::tie(i, s, d, a, v); }

  auto GetI() const { return i; }
  auto GetS() const { return s; }
  auto GetD() const { return d; }
  auto GetA() const { return a; }
  auto GetV() const { return v; }
};

using FooTuple = std::tuple<int, std::string, double, std::vector<int>,
                            std::vector<std::string>>;

struct BunchOfFoo {
  std::vector<FooBar> foobars;

  bool operator==(const BunchOfFoo& rhs) const {
    return foobars == rhs.foobars;
  }
};

struct NoUseInWrite {
  int i;
  std::string s;
  double d;
  std::vector<int> a;
  std::vector<std::string> v;
};

struct NoUserMapping {
  int i;
  std::string s;
  double d;
  std::vector<int> a;
  std::vector<std::string> v;
};

}  // namespace pgtest
/*! [User type declaration] */

/*! [User type mapping] */
namespace storages::postgres::io {

// This specialisation MUST go to the header together with the mapped type
template <>
struct CppToUserPg<pgtest::FooBar> {
  static constexpr DBTypeName postgres_name = kCompositeName;
};

// This specialisation MUST go to the header together with the mapped type
template <>
struct CppToUserPg<pgtest::FooClass> {
  static constexpr DBTypeName postgres_name = kCompositeName;
};

// This specialisation MUST go to the header together with the mapped type
template <>
struct CppToUserPg<pgtest::FooTuple> {
  static constexpr DBTypeName postgres_name = kCompositeName;
};

template <>
struct CppToUserPg<pgtest::BunchOfFoo> {
  static constexpr DBTypeName postgres_name = "__pgtest.foobars";
};

}  // namespace storages::postgres::io
/*! [User type mapping] */

namespace storages::postgres::io {

// This mapping is separate from the others as it shouldn't get to the code
// snippet for generating documentation.
template <>
struct CppToUserPg<pgtest::NoUseInWrite> {
  static constexpr DBTypeName postgres_name = kCompositeName;
};

}  // namespace storages::postgres::io

namespace static_test {

static_assert(io::traits::TupleHasParsers<pgtest::FooTuple>::value);
static_assert(tt::detail::CompositeHasParsers<pgtest::FooTuple>::value);
static_assert(tt::detail::CompositeHasParsers<pgtest::FooBar>::value);
static_assert(tt::detail::CompositeHasParsers<pgtest::FooClass>::value);
static_assert(tt::detail::CompositeHasParsers<pgtest::NoUseInWrite>::value);
static_assert(tt::detail::CompositeHasParsers<pgtest::NoUserMapping>::value);

static_assert(!tt::detail::CompositeHasParsers<int>::value);

static_assert(io::traits::TupleHasFormatters<pgtest::FooTuple>::value);
static_assert(tt::detail::CompositeHasFormatters<pgtest::FooTuple>::value);
static_assert(tt::detail::CompositeHasFormatters<pgtest::FooBar>::value);
static_assert(tt::detail::CompositeHasFormatters<pgtest::FooClass>::value);

static_assert(!tt::detail::CompositeHasParsers<int>::value);

static_assert(tt::kHasParser<pgtest::BunchOfFoo>);
static_assert(tt::kHasFormatter<pgtest::BunchOfFoo>);

static_assert(tt::kHasParser<pgtest::NoUserMapping>);
static_assert(!tt::kHasFormatter<pgtest::NoUserMapping>);

static_assert(tt::kTypeBufferCategory<pgtest::FooTuple> ==
              io::BufferCategory::kCompositeBuffer);
static_assert(tt::kTypeBufferCategory<pgtest::FooBar> ==
              io::BufferCategory::kCompositeBuffer);
static_assert(tt::kTypeBufferCategory<pgtest::FooClass> ==
              io::BufferCategory::kCompositeBuffer);
static_assert(tt::kTypeBufferCategory<pgtest::BunchOfFoo> ==
              io::BufferCategory::kCompositeBuffer);
static_assert(tt::kTypeBufferCategory<pgtest::NoUseInWrite> ==
              io::BufferCategory::kCompositeBuffer);
static_assert(tt::kTypeBufferCategory<pgtest::NoUserMapping> ==
              io::BufferCategory::kCompositeBuffer);

}  // namespace static_test

namespace {

POSTGRE_TEST_P(CompositeTypeRoundtrip) {
  ASSERT_TRUE(conn.get()) << "Expected non-empty connection pointer";
  ASSERT_FALSE(conn->IsReadOnly()) << "Expect a read-write connection";

  pg::ResultSet res{nullptr};
  ASSERT_NO_THROW(conn->Execute(kDropTestSchema)) << "Drop schema";
  ASSERT_NO_THROW(conn->Execute(kCreateTestSchema)) << "Create schema";

  EXPECT_NO_THROW(conn->Execute(kCreateACompositeType))
      << "Successfully create a composite type";
  EXPECT_NO_THROW(conn->Execute(kCreateCompositeOfComposites))
      << "Successfully create composite of composites";

  // The datatypes are expected to be automatically reloaded
  EXPECT_NO_THROW(
      res = conn->Execute("select ROW(42, 'foobar', 3.14, ARRAY[-1, 0, 1], "
                          "ARRAY['a', 'b', 'c'])::__pgtest.foobar"));
  const std::vector<int> expected_int_vector{-1, 0, 1};
  const std::vector<std::string> expected_str_vector{"a", "b", "c"};

  ASSERT_FALSE(res.IsEmpty());

  pgtest::FooBar fb;
  EXPECT_NO_THROW(res[0].To(fb));
  EXPECT_THROW(res[0][0].As<std::string>(), pg::InvalidParserCategory);
  EXPECT_EQ(42, fb.i);
  EXPECT_EQ("foobar", fb.s);
  EXPECT_EQ(3.14, fb.d);
  EXPECT_EQ(expected_int_vector, fb.a);
  EXPECT_EQ(expected_str_vector, fb.v);

  pgtest::FooTuple ft;
  EXPECT_NO_THROW(res[0].To(ft));
  EXPECT_EQ(42, std::get<0>(ft));
  EXPECT_EQ("foobar", std::get<1>(ft));
  EXPECT_EQ(3.14, std::get<2>(ft));
  EXPECT_EQ(expected_int_vector, std::get<3>(ft));
  EXPECT_EQ(expected_str_vector, std::get<4>(ft));

  pgtest::FooClass fc;
  EXPECT_NO_THROW(res[0].To(fc));
  EXPECT_EQ(42, fc.GetI());
  EXPECT_EQ("foobar", fc.GetS());
  EXPECT_EQ(3.14, fc.GetD());
  EXPECT_EQ(expected_int_vector, fc.GetA());
  EXPECT_EQ(expected_str_vector, fc.GetV());

  EXPECT_NO_THROW(res = conn->Execute("select $1 as foo", fb));
  EXPECT_NO_THROW(res = conn->Execute("select $1 as foo", ft));
  EXPECT_NO_THROW(res = conn->Execute("select $1 as foo", fc));

  using FooVector = std::vector<pgtest::FooBar>;
  EXPECT_NO_THROW(
      res = conn->Execute("select $1 as array_of_foo", FooVector{fb, fb, fb}));

  ASSERT_FALSE(res.IsEmpty());
  EXPECT_THROW(res[0][0].As<pgtest::FooBar>(), pg::InvalidParserCategory);
  EXPECT_THROW(res[0][0].As<std::string>(), pg::InvalidParserCategory);
  EXPECT_EQ((FooVector{fb, fb, fb}), res[0].As<FooVector>());

  pgtest::BunchOfFoo bf{{fb, fb, fb}};
  EXPECT_NO_THROW(res = conn->Execute("select $1 as bunch", bf));
  ASSERT_FALSE(res.IsEmpty());
  pgtest::BunchOfFoo bf1;
  EXPECT_NO_THROW(res[0].To(bf1));
  EXPECT_EQ(bf, bf1);
  EXPECT_EQ(bf, res[0].As<pgtest::BunchOfFoo>());

  // Unwrapping composite structure to a row
  EXPECT_NO_THROW(res = conn->Execute("select $1.*", bf));
  ASSERT_FALSE(res.IsEmpty());
  EXPECT_NO_THROW(res[0].To(bf1, pg::kRowTag));
  EXPECT_EQ(bf, bf1);
  EXPECT_EQ(bf, res[0].As<pgtest::BunchOfFoo>(pg::kRowTag));

  EXPECT_ANY_THROW(res[0][0].To(bf1));

  // Using a mapped type only for reading
  EXPECT_NO_THROW(res = conn->Execute("select $1 as foo", fb));
  EXPECT_NO_THROW(res.AsContainer<std::vector<pgtest::NoUseInWrite>>())
      << "A type that is not used for writing query parameter buffers must be"
         "available for reading";

  EXPECT_NO_THROW(conn->Execute(kDropTestSchema)) << "Drop schema";
}

POSTGRE_TEST_P(OptionalCompositeTypeRoundtrip) {
  ASSERT_TRUE(conn.get()) << "Expected non-empty connection pointer";
  ASSERT_FALSE(conn->IsReadOnly()) << "Expect a read-write connection";

  pg::ResultSet res{nullptr};
  ASSERT_NO_THROW(conn->Execute(kDropTestSchema)) << "Drop schema";
  ASSERT_NO_THROW(conn->Execute(kCreateTestSchema)) << "Create schema";

  EXPECT_NO_THROW(conn->Execute(kCreateACompositeType))
      << "Successfully create a composite type";

  EXPECT_NO_THROW(
      res = conn->Execute("select ROW(42, 'foobar', 3.14, ARRAY[-1, 0, 1], "
                          "ARRAY['a', 'b', 'c'])::__pgtest.foobar"));
  {
    auto fo = res.Front().As<pgtest::FooBarOpt>();
    EXPECT_TRUE(!!fo) << "Non-empty optional result expected";
  }

  EXPECT_NO_THROW(res = conn->Execute("select null::__pgtest.foobar"));
  {
    auto fo = res.Front().As<pgtest::FooBarOpt>();
    EXPECT_TRUE(!fo) << "Empty optional result expected";
  }

  EXPECT_NO_THROW(conn->Execute(kDropTestSchema)) << "Drop schema";
}

POSTGRE_TEST_P(CompositeTypeRoundtripAsRecord) {
  ASSERT_TRUE(conn.get()) << "Expected non-empty connection pointer";
  ASSERT_FALSE(conn->IsReadOnly()) << "Expect a read-write connection";

  pg::ResultSet res{nullptr};
  ASSERT_NO_THROW(conn->Execute(kDropTestSchema)) << "Drop schema";
  ASSERT_NO_THROW(conn->Execute(kCreateTestSchema)) << "Create schema";

  EXPECT_NO_THROW(conn->Execute(kCreateACompositeType))
      << "Successfully create a composite type";
  EXPECT_NO_THROW(conn->Execute(kCreateCompositeOfComposites))
      << "Successfully create composite of composites";

  EXPECT_NO_THROW(
      res = conn->Execute(
          "SELECT fb.* FROM (SELECT ROW(42, 'foobar', 3.14, ARRAY[-1, 0, 1], "
          "ARRAY['a', 'b', 'c'])::__pgtest.foobar) fb"));
  const std::vector<int> expected_int_vector{-1, 0, 1};
  const std::vector<std::string> expected_str_vector{"a", "b", "c"};

  ASSERT_FALSE(res.IsEmpty());

  pgtest::FooBar fb;
  res[0].To(fb);
  EXPECT_NO_THROW(res[0].To(fb));
  EXPECT_THROW(res[0][0].As<std::string>(), pg::InvalidParserCategory);
  EXPECT_EQ(42, fb.i);
  EXPECT_EQ("foobar", fb.s);
  EXPECT_EQ(3.14, fb.d);
  EXPECT_EQ(expected_int_vector, fb.a);
  EXPECT_EQ(expected_str_vector, fb.v);

  pgtest::FooTuple ft;
  EXPECT_NO_THROW(res[0].To(ft));
  EXPECT_EQ(42, std::get<0>(ft));
  EXPECT_EQ("foobar", std::get<1>(ft));
  EXPECT_EQ(3.14, std::get<2>(ft));
  EXPECT_EQ(expected_int_vector, std::get<3>(ft));
  EXPECT_EQ(expected_str_vector, std::get<4>(ft));

  pgtest::FooClass fc;
  EXPECT_NO_THROW(res[0].To(fc));
  EXPECT_EQ(42, fc.GetI());
  EXPECT_EQ("foobar", fc.GetS());
  EXPECT_EQ(3.14, fc.GetD());
  EXPECT_EQ(expected_int_vector, fc.GetA());
  EXPECT_EQ(expected_str_vector, fc.GetV());

  pgtest::NoUserMapping nm;
  EXPECT_NO_THROW(res[0].To(nm));
  EXPECT_THROW(res[0][0].As<std::string>(), pg::InvalidParserCategory);
  EXPECT_EQ(42, nm.i);
  EXPECT_EQ("foobar", nm.s);
  EXPECT_EQ(3.14, nm.d);
  EXPECT_EQ(expected_int_vector, nm.a);
  EXPECT_EQ(expected_str_vector, nm.v);

  EXPECT_NO_THROW(res = conn->Execute("SELECT ROW($1.*) AS record", fb));
  EXPECT_NO_THROW(res = conn->Execute("SELECT ROW($1.*) AS record", ft));
  EXPECT_NO_THROW(res = conn->Execute("SELECT ROW($1.*) AS record", fc));

  using FooVector = std::vector<pgtest::FooBar>;
  EXPECT_NO_THROW(res = conn->Execute("SELECT $1::record[] AS array_of_records",
                                      FooVector{fb, fb, fb}));

  ASSERT_FALSE(res.IsEmpty());
  EXPECT_THROW(res[0][0].As<pgtest::FooBar>(), pg::InvalidParserCategory);
  EXPECT_THROW(res[0][0].As<std::string>(), pg::InvalidParserCategory);
  EXPECT_EQ((FooVector{fb, fb, fb}), res[0].As<FooVector>());

  pgtest::BunchOfFoo bf{{fb, fb, fb}};
  EXPECT_NO_THROW(res =
                      conn->Execute("SELECT ROW($1.f::record[]) AS bunch", bf));
  ASSERT_FALSE(res.IsEmpty());
  pgtest::BunchOfFoo bf1;
  EXPECT_NO_THROW(res[0].To(bf1));
  EXPECT_EQ(bf, bf1);
  EXPECT_EQ(bf, res[0].As<pgtest::BunchOfFoo>());

  // Unwrapping composite structure to a row
  EXPECT_NO_THROW(res = conn->Execute("select $1.f::record[]", bf));
  ASSERT_FALSE(res.IsEmpty());
  EXPECT_NO_THROW(res[0].To(bf1, pg::kRowTag));
  EXPECT_EQ(bf, bf1);
  EXPECT_EQ(bf, res[0].As<pgtest::BunchOfFoo>(pg::kRowTag));

  EXPECT_ANY_THROW(res[0][0].To(bf1));

  // Using a mapped type only for reading
  EXPECT_NO_THROW(res = conn->Execute("SELECT ROW($1.*) AS record", fb));
  EXPECT_NO_THROW(res.AsContainer<std::vector<pgtest::NoUseInWrite>>())
      << "A type that is not used for writing query parameter buffers must be"
         "available for reading";

  EXPECT_NO_THROW(conn->Execute(kDropTestSchema)) << "Drop schema";
}

POSTGRE_TEST_P(OptionalCompositeTypeRoundtripAsRecord) {
  ASSERT_TRUE(conn.get()) << "Expected non-empty connection pointer";
  ASSERT_FALSE(conn->IsReadOnly()) << "Expect a read-write connection";

  pg::ResultSet res{nullptr};
  ASSERT_NO_THROW(conn->Execute(kDropTestSchema)) << "Drop schema";
  ASSERT_NO_THROW(conn->Execute(kCreateTestSchema)) << "Create schema";

  EXPECT_NO_THROW(conn->Execute(kCreateACompositeType))
      << "Successfully create a composite type";

  EXPECT_NO_THROW(
      res = conn->Execute(
          "SELECT fb.* FROM (SELECT ROW(42, 'foobar', 3.14, ARRAY[-1, 0, 1], "
          "ARRAY['a', 'b', 'c'])::__pgtest.foobar) fb"));
  {
    auto fo = res.Front().As<pgtest::FooBarOpt>();
    EXPECT_TRUE(!!fo) << "Non-empty optional result expected";
  }

  EXPECT_NO_THROW(res = conn->Execute("select null::record"));
  {
    auto fo = res.Front().As<pgtest::FooBarOpt>();
    EXPECT_TRUE(!fo) << "Empty optional result expected";
  }

  EXPECT_NO_THROW(conn->Execute(kDropTestSchema)) << "Drop schema";
}

// Please never use this in your code, this is only to check type loaders
POSTGRE_TEST_P(VariableRecordTypes) {
  ASSERT_TRUE(conn.get()) << "Expected non-empty connection pointer";
  ASSERT_FALSE(conn->IsReadOnly()) << "Expect a read-write connection";

  pg::ResultSet res{nullptr};
  EXPECT_NO_THROW(
      res = conn->Execute("WITH test AS (SELECT unnest(ARRAY[1, 2]) a)"
                          "SELECT CASE WHEN a = 1 THEN ROW(42)"
                          "WHEN a = 2 THEN ROW('str'::text) "
                          "END FROM test"));
  ASSERT_EQ(2, res.Size());

  EXPECT_EQ(42, std::get<0>(res[0].As<std::tuple<int>>()));
  EXPECT_EQ("str", std::get<0>(res[1].As<std::tuple<std::string>>()));
}

}  // namespace
