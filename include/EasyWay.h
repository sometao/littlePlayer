#pragma once
#include <chrono>
#include <iostream>
#include <random>
#include <thread>
#include <vector>

//#define DEBUG_LOG

class EasyWay {
#ifdef DEBUG_LOG
  static const bool isDebug = true;
#else
  static const bool isDebug = false;
#endif

 public:
  static long long currentTimeMillis() {
    using namespace std::chrono;
    auto time_now = system_clock::now();
    auto duration_in_ms = duration_cast<milliseconds>(time_now.time_since_epoch());
    return duration_in_ms.count();
  }

  static void printDebug(const string& errMsg) {
    if (isDebug) {
      std::cout << errMsg << std::endl;
    }
  }

  static void throwErrorMsg(const string& errMsg, bool printMsg = true) {
    if (printMsg) {
      std::cout << errMsg << std::endl;
    }
    throw std::runtime_error(errMsg);
  }

  static void sleep(int milliSecond) {
    std::this_thread::sleep_for(std::chrono::milliseconds(milliSecond));
  }

  static std::vector<size_t> range(size_t begin, size_t until, size_t step = 1) {
    using std::vector;
    vector<size_t> rst{};
    for (auto i = begin; i < until; i += step) {
      rst.push_back(i);
    }
    return rst;
  }

  static auto uniformIntDistribution(int min, int max, int seed = INT_MIN) {
    static std::random_device rd;
    static std::mt19937 gen{seed == INT_MIN ? rd() : seed};
    static std::uniform_int_distribution<> dis(min, max);
    auto func = [] { return dis(gen); };
    return func;
  }

  static auto uniformDoubleDistribution(double min, double max, int seed = INT_MIN) {
    static std::random_device rd;
    static std::mt19937 gen{seed == INT_MIN ? rd() : seed};
    static std::uniform_real_distribution<> dis(min, max);
    auto func = [] { return dis(gen); };
    return func;
  }

  static void trim(string& s) {
    if (s.empty()) {
      return;
    }
    s.erase(0, s.find_first_not_of(" "));
    s.erase(s.find_last_not_of(" ") + 1);
  }
};

namespace {

using namespace std;

void testCurrentTimeMillis1() {
  for (auto i : EasyWay::range(0, 10)) {
    auto t = EasyWay::currentTimeMillis();
    cout << "i\t=" << i << "\t\t\t t:" << t << endl;
    EasyWay::sleep(1);
  }
}

void TestUniformDouble1() {
  auto random1 = EasyWay::uniformDoubleDistribution(1, 2, 12345);
  for (auto i : EasyWay::range(0, 10)) {
    cout << random1() << "\n";
  }
  cout << endl;
}

void TestUniformInt1() {
  auto random1 = EasyWay::uniformIntDistribution(1000, 3000, 12345);
  for (auto i : EasyWay::range(0, 10)) {
    cout << random1() << "\n";
  }

  cout << "-----------------------------------------"
          "！！！！！！！！！！！！！！！！！！！！！！！！！！！！！！！！！！！！！！！！！！！！！！！！-"
       << endl;

  auto random2 = EasyWay::uniformIntDistribution(1000, 3000, 12345);
  for (auto i : EasyWay::range(0, 10)) {
    cout << random2() << "\n";
  }

  cout << endl;
}

}  // namespace

class EasyWayTester {
 public:
  static void test() {
    // testCurrentTimeMillis1();
    // TestUniformDouble1();
    TestUniformInt1();
  };
};
