#define BOOST_TEST_MODULE pichi router test

#include "utils.hpp"
#include <boost/asio/ip/tcp.hpp>
#include <boost/test/unit_test.hpp>
#include <pichi/api/router.hpp>

using namespace std;
using namespace pichi;
using namespace pichi::api;
namespace asio = boost::asio;
namespace ip = asio::ip;
namespace sys = boost::system;
using ResolvedResults = ip::basic_resolver_results<ip::tcp>;
using Endpoint = ip::tcp::endpoint;

static decltype(auto) fn = "geo.mmdb";
static decltype(auto) ph = "placeholder";

BOOST_AUTO_TEST_SUITE(ROUTER_TEST)

BOOST_AUTO_TEST_CASE(matchDomain_Empty_Domains)
{
  BOOST_CHECK(!matchDomain("example.com", ""));
  BOOST_CHECK(!matchDomain("", "example.com"));
}

BOOST_AUTO_TEST_CASE(matchDomain_Domains_Start_With_Dot)
{
  BOOST_CHECK_EXCEPTION(matchDomain(".", "com"), Exception, verifyException<PichiError::MISC>);
  BOOST_CHECK_EXCEPTION(matchDomain(".com", "com"), Exception, verifyException<PichiError::MISC>);
  BOOST_CHECK_EXCEPTION(matchDomain("example.com", "."), Exception,
                        verifyException<PichiError::MISC>);
  BOOST_CHECK_EXCEPTION(matchDomain("example.com", ".com"), Exception,
                        verifyException<PichiError::MISC>);
}

BOOST_AUTO_TEST_CASE(matchDomain_Matched)
{
  BOOST_CHECK(matchDomain("foo.bar.example.com", "bar.example.com"));
  BOOST_CHECK(matchDomain("foo.bar.example.com", "example.com"));
  BOOST_CHECK(matchDomain("foo.bar.example.com", "com"));
}

BOOST_AUTO_TEST_CASE(matchDomain_Same_End)
{
  BOOST_CHECK(!matchDomain("foobar.example.com", "bar.example.com"));
  BOOST_CHECK(!matchDomain("foobarexample.com", "example.com"));
  BOOST_CHECK(!matchDomain("example.com", "m"));
}

BOOST_AUTO_TEST_CASE(matchDomain_Containing_Not_Matched)
{
  BOOST_CHECK(!matchDomain("example.com", "example"));
  BOOST_CHECK(!matchDomain("foo.example.com", "example"));
  BOOST_CHECK(!matchDomain("example.com", "e.c"));
}

BOOST_AUTO_TEST_CASE(matchDomain_Same)
{
  BOOST_CHECK(matchDomain("example.com", "example.com"));
  BOOST_CHECK(matchDomain("foo.example.com", "foo.example.com"));
}

BOOST_AUTO_TEST_CASE(Router_Empty_Rules)
{
  auto router = Router{fn};
  BOOST_CHECK(begin(router) == end(router));

  router.update(ph, {});
  BOOST_CHECK(begin(router) != end(router));

  router.erase(ph);
  BOOST_CHECK(begin(router) == end(router));
}

BOOST_AUTO_TEST_CASE(Router_Erase_Not_Existing)
{
  auto router = Router{fn};
  BOOST_CHECK(begin(router) == end(router));

  router.erase(ph);
  BOOST_CHECK(begin(router) == end(router));
}

BOOST_AUTO_TEST_CASE(Router_Erase_Rule_Used_By_Order)
{
  auto router = Router{fn};
  router.update(ph, {ph});
  router.setRoute({{}, {ph}});

  BOOST_CHECK_EXCEPTION(router.erase(ph), Exception, verifyException<PichiError::RES_IN_USE>);
}

BOOST_AUTO_TEST_CASE(Router_Iteration)
{
  static auto const MAX = 10;
  auto router = Router{fn};
  BOOST_CHECK(begin(router) == end(router));

  for (auto i = 0; i < MAX; ++i) router.update(to_string(i), {to_string(i)});

  BOOST_CHECK(begin(router) != end(router));
  BOOST_CHECK(distance(begin(router), end(router)) == MAX);

  for (auto i = 0; i < MAX; ++i) {
    auto s = to_string(i);
    // TODO use find_if when VC++ doesn't require Iterator's copy assignment operator
    auto it = begin(router);
    while (it != end(router) && it->first != s) ++it;
    BOOST_CHECK(it != end(router));
    BOOST_CHECK(s == it->first);
    BOOST_CHECK(s == it->second.egress_);
  }
}

BOOST_AUTO_TEST_CASE(Router_isUsed)
{
  auto router = Router{fn};
  BOOST_CHECK(!router.isUsed(ph));

  router.update(ph, {ph});
  BOOST_CHECK(router.isUsed(ph));

  router.erase(ph);
  router.setRoute({ph});
  BOOST_CHECK(router.isUsed(ph));
}

BOOST_AUTO_TEST_CASE(Router_Set_Not_Existing_Route)
{
  auto verifyDefault = [](auto&& rvo) {
    BOOST_CHECK(rvo.default_.has_value());
    BOOST_CHECK(*rvo.default_ == "direct");
    BOOST_CHECK(rvo.rules_.empty());
  };

  auto router = Router{fn};
  verifyDefault(router.getRoute());

  BOOST_CHECK_EXCEPTION(router.setRoute({ph, {ph}}), Exception, verifyException<PichiError::MISC>);
  verifyDefault(router.getRoute());
}

BOOST_AUTO_TEST_CASE(Router_Set_Default_Route)
{
  auto router = Router{fn};
  auto vo = router.getRoute();
  BOOST_CHECK(vo.default_.has_value());
  BOOST_CHECK(*vo.default_ == "direct");
  BOOST_CHECK(vo.rules_.empty());

  router.setRoute({ph});
  vo = router.getRoute();
  BOOST_CHECK(vo.default_.has_value());
  BOOST_CHECK(*vo.default_ == ph);
  BOOST_CHECK(vo.rules_.empty());
}

BOOST_AUTO_TEST_CASE(Router_setRoute_With_Order)
{
  static auto const MAX = 10;
  auto verifyRules = [](auto&& expect, auto&& fact) {
    BOOST_CHECK(equal(cbegin(expect), cend(expect), cbegin(fact), cend(fact)));
  };

  auto router = Router{fn};

  for (auto i = 0; i < MAX; ++i) router.update(to_string(i), {to_string(i)});

  auto seq = RouteVO{};
  generate_n(back_inserter(seq.rules_), MAX, [i = 0]() mutable { return to_string(i++); });
  router.setRoute(seq);
  verifyRules(seq.rules_, router.getRoute().rules_);

  auto rev = RouteVO{};
  generate_n(back_inserter(rev.rules_), MAX, [i = MAX - 1]() mutable { return to_string(i--); });
  router.setRoute(rev);
  verifyRules(rev.rules_, router.getRoute().rules_);
}

BOOST_AUTO_TEST_CASE(Router_update_Invalid_Range)
{
  auto router = Router{fn};
  BOOST_CHECK(begin(router) == end(router));
  BOOST_CHECK_EXCEPTION(router.update(ph, {ph, {"Invalid Range"}}), sys::system_error,
                        verifyException<asio::error::invalid_argument>);
  BOOST_CHECK(begin(router) == end(router));
}

BOOST_AUTO_TEST_CASE(Router_update_Invalid_Type)
{
  auto router = Router{fn};
  BOOST_CHECK(begin(router) == end(router));
  BOOST_CHECK_EXCEPTION(router.update(ph, {ph, {}, {}, {AdapterType::DIRECT}}), Exception,
                        verifyException<PichiError::MISC>);
  BOOST_CHECK_EXCEPTION(router.update(ph, {ph, {}, {}, {AdapterType::REJECT}}), Exception,
                        verifyException<PichiError::MISC>);
  BOOST_CHECK(begin(router) == end(router));
}

BOOST_AUTO_TEST_CASE(Router_Matching_Range)
{
  auto router = Router{fn};
  router.update(ph, {ph, {"10.0.0.0/8", "fd00::/8"}});
  router.setRoute({{}, {ph}});

  BOOST_CHECK(router.route({}, ph, AdapterType::DIRECT, []() {
    return ResolvedResults::create(Endpoint{ip::make_address("10.0.0.1"), 443}, ph, ph);
  }) == ph);
  BOOST_CHECK(router.route({}, ph, AdapterType::DIRECT, []() {
    return ResolvedResults::create(Endpoint{ip::make_address("fd00::1"), 443}, ph, ph);
  }) == ph);
  BOOST_CHECK(router.route({}, ph, AdapterType::DIRECT, []() {
    return ResolvedResults::create(Endpoint{ip::make_address("127.0.0.1"), 443}, ph, ph);
  }) == "direct");
  BOOST_CHECK(router.route({}, ph, AdapterType::DIRECT, []() {
    return ResolvedResults::create(Endpoint{ip::make_address("fe00::1"), 443}, ph, ph);
  }) == "direct");
}

BOOST_AUTO_TEST_CASE(Router_Matching_Ingress)
{
  auto router = Router{fn};
  router.update(ph, {ph, {}, {ph}});
  router.setRoute({{}, {ph}});

  auto r = []() {
    return ResolvedResults::create(Endpoint{ip::make_address("fe00::1"), 443}, ph, ph);
  };
  BOOST_CHECK(router.route({}, ph, AdapterType::DIRECT, r) == ph);
  BOOST_CHECK(router.route({}, "NotMatched", AdapterType::DIRECT, r) == "direct");
}

BOOST_AUTO_TEST_CASE(Router_Matching_Type)
{
  auto router = Router{fn};
  router.update(ph, {ph, {}, {}, {AdapterType::HTTP}});
  router.setRoute({{}, {ph}});

  auto r = []() {
    return ResolvedResults::create(Endpoint{ip::make_address("fe00::1"), 443}, ph, ph);
  };
  BOOST_CHECK(router.route({}, ph, AdapterType::HTTP, r) == ph);
  BOOST_CHECK(router.route({}, ph, AdapterType::DIRECT, r) == "direct");
}

BOOST_AUTO_TEST_CASE(Router_Matching_Pattern)
{
  auto router = Router{fn};
  router.update(ph, {ph, {}, {}, {}, {"^.*\\.example\\.com$"}});
  router.setRoute({{}, {ph}});

  // auto dummy = ResolvedResults::create(Endpoint{ip::make_address("fe00::1"), 443}, ph, ph);
  auto dummy = []() { return ResolvedResults{}; };
  for (auto type :
       {net::Endpoint::Type::DOMAIN_NAME, net::Endpoint::Type::IPV4, net::Endpoint::Type::IPV6}) {
    BOOST_CHECK(router.route({type, "foo.example.com", ph}, ph, AdapterType::DIRECT, dummy) == ph);
    BOOST_CHECK(router.route({type, "fooexample.com", ph}, ph, AdapterType::DIRECT, dummy) ==
                "direct");
  }
}

BOOST_AUTO_TEST_CASE(Router_Matching_Domain)
{
  auto router = Router{fn};
  router.update(ph, {ph, {}, {}, {}, {}, {"example.com"}});
  router.setRoute({{}, {ph}});

  // auto dummy = ResolvedResults::create(Endpoint{ip::make_address("fe00::1"), 443}, ph, ph);
  auto dummy = []() { return ResolvedResults{}; };
  BOOST_CHECK(router.route({net::Endpoint::Type::DOMAIN_NAME, "foo.example.com", ph}, ph,
                           AdapterType::DIRECT, dummy) == ph);
  BOOST_CHECK(router.route({net::Endpoint::Type::DOMAIN_NAME, "fooexample.com", ph}, ph,
                           AdapterType::DIRECT, dummy) == "direct");
}

BOOST_AUTO_TEST_CASE(Router_Matching_Domain_With_Invalid_Type)
{
  auto router = Router{fn};
  router.update(ph, {ph, {}, {}, {}, {}, {"example.com"}});
  router.setRoute({{}, {ph}});

  // auto dummy = ResolvedResults::create(Endpoint{ip::make_address("fe00::1"), 443}, ph, ph);
  auto dummy = []() { return ResolvedResults{}; };
  BOOST_CHECK(router.route({net::Endpoint::Type::IPV4, "foo.example.com", ph}, ph,
                           AdapterType::DIRECT, dummy) == "direct");
  BOOST_CHECK(router.route({net::Endpoint::Type::IPV6, "foo.example.com", ph}, ph,
                           AdapterType::DIRECT, dummy) == "direct");
}

BOOST_AUTO_TEST_CASE(Router_Matching_Country)
{
  auto router = Router{fn};
  router.update(ph, {ph, {}, {}, {}, {}, {}, {"AU"}});
  router.setRoute({{}, {ph}});

  BOOST_CHECK(router.route({}, ph, AdapterType::DIRECT, []() {
    return ResolvedResults::create(Endpoint{ip::make_address("1.1.1.1"), 443}, ph, ph);
  }) == ph);
  BOOST_CHECK(router.route({}, ph, AdapterType::DIRECT, []() {
    return ResolvedResults::create(Endpoint{ip::make_address("::ffff:1.1.1.1"), 443}, ph, ph);
  }) == ph);
  BOOST_CHECK(router.route({}, ph, AdapterType::DIRECT, []() {
    return ResolvedResults::create(Endpoint{ip::make_address("8.8.8.8"), 443}, ph, ph);
  }) == "direct");
  BOOST_CHECK(router.route({}, ph, AdapterType::DIRECT, []() {
    return ResolvedResults::create(Endpoint{ip::make_address("::ffff:8.8.8.8"), 443}, ph, ph);
  }) == "direct");
}

BOOST_AUTO_TEST_CASE(Router_Conditionally_Resolving_Default)
{
  auto resolve = []() {
    BOOST_ERROR("Unexpect resolving invocation.");
    return ResolvedResults{};
  };
  auto router = Router{fn};
  router.route({}, ph, AdapterType::DIRECT, resolve);
}

BOOST_AUTO_TEST_CASE(Router_Conditionally_Resolving_Unnecessary_Rules)
{
  auto resolve = []() {
    BOOST_ERROR("Unexpect resolving invocation.");
    return ResolvedResults{};
  };
  auto router = Router{fn};
  router.update(ph, {ph, {}, {ph}, {AdapterType::SS}, {ph}, {ph}, {}});
  router.setRoute({ph, {ph}});
  router.route({}, ph, AdapterType::DIRECT, resolve);
}

BOOST_AUTO_TEST_CASE(Router_Conditionally_Resolving_Unnecessary_Route)
{
  auto resolve = []() {
    BOOST_ERROR("Unexpect resolving invocation.");
    return ResolvedResults{};
  };
  auto router = Router{fn};
  router.update("range", {ph, {"127.0.0.1/32"}});
  router.update("country", {ph, {}, {}, {}, {}, {}, {ph}});
  router.route({}, ph, AdapterType::DIRECT, resolve);
}

BOOST_AUTO_TEST_CASE(Router_Conditionally_Resolving_Necessary_Range)
{
  auto resolved = false;
  auto resolve = [&resolved]() {
    resolved = true;
    return ResolvedResults{};
  };
  auto router = Router{fn};
  router.update(ph, {ph, {"127.0.0.1/32"}});
  router.setRoute({ph, {ph}});

  router.route({}, ph, AdapterType::DIRECT, resolve);
  BOOST_CHECK(resolved);
}

BOOST_AUTO_TEST_CASE(Router_Conditionally_Resolving_Necessary_Country)
{
  auto resolved = false;
  auto resolve = [&resolved]() {
    resolved = true;
    return ResolvedResults{};
  };
  auto router = Router{fn};
  router.update(ph, {ph, {}, {}, {}, {}, {}, {ph}});
  router.setRoute({ph, {ph}});

  router.route({}, ph, AdapterType::DIRECT, resolve);
  BOOST_CHECK(resolved);
}

BOOST_AUTO_TEST_SUITE_END()
