//
// switch-tvbox: 协议层契约测试
//
// 极简自包含测试框架。仓库原本没有任何测试设施（见 docs/t2-contract-spec.md
// 第 2 节缺口 2），本文件不引入外部依赖，可在 PC 上独立编译运行：
//
//   g++ -std=c++17 -I wiliwili/include tests/test_contract.cpp \
//       wiliwili/source/tvbox/vod_error.cpp \
//       wiliwili/source/tvbox/vod_parser.cpp -o test_contract
//
// 也可经CMake 目标 tvbox_contract_tests 运行（需 BUILD_TVBOX_CLI=ON）。
//
#pragma once

#include <cstdio>
#include <string>
#include <vector>

#include "tvbox/vod_error.hpp"

namespace tvtest {

struct TestCase {
    const char* id;      // 对应 docs/t2-contract-spec.md 第 6 节的用例编号
    const char* name;
    void (*fn)();
};

inline std::vector<TestCase>& registry() {
    static std::vector<TestCase> cases;
    return cases;
}

inline int& failureCount() {
    static int n = 0;
    return n;
}

inline const char*& currentCase() {
    static const char* id = "";
    return id;
}

struct Registrar {
    Registrar(const char* id, const char* name, void (*fn)()) {
        registry().push_back({id, name, fn});
    }
};

inline void reportFailure(const char* file, int line, const std::string& msg) {
    ++failureCount();
    std::printf("  FAIL [%s] %s:%d: %s\n", currentCase(), file, line, msg.c_str());
}

// id_ 必须是无引号的标识符 token（用于生成唯一符号），例如 C1
// 显示用的用例编号由 id_ 字符串化后得到
#define TEST_CONTRACT(id_, name_)                                                  \
    static void test_##id_();                                                      \
    static tvtest::Registrar reg_##id_(#id_, name_, test_##id_);                    \
    static void test_##id_()

#define CHECK(cond)                                                                \
    do {                                                                            \
        if (!(cond)) tvtest::reportFailure(__FILE__, __LINE__, "CHECK failed: " #cond); \
    } while (0)

#define CHECK_MSG(cond, msg)                                                       \
    do {                                                                            \
        if (!(cond)) tvtest::reportFailure(__FILE__, __LINE__,                      \
                                           std::string("CHECK failed: " #cond " | ") + (msg)); \
    } while (0)

#define CHECK_EQ(a, b)                                                             \
    do {                                                                            \
        auto va_ = (a);                                                             \
        auto vb_ = (b);                                                             \
        if (!(va_ == vb_))                                                          \
            tvtest::reportFailure(__FILE__, __LINE__,                               \
                                  std::string(#a " != " #b " | got: ") +            \
                                      std::to_string(va_) + " vs " + std::to_string(vb_)); \
    } while (0)

#define CHECK_STR(a, b)                                                            \
    do {                                                                            \
        std::string va_ = (a);                                                      \
        std::string vb_ = (b);                                                      \
        if (va_ != vb_)                                                             \
            tvtest::reportFailure(__FILE__, __LINE__,                               \
                                  std::string(#a " != " #b " | got: \"") + va_ +   \
                                      "\" vs \"" + vb_ + "\"");                      \
    } while (0)

// 枚举比较专用：CHECK_EQ 依赖 std::to_string，对 enum class 会编译失败
inline std::string codeName(tvbox::ErrorCode c) { return tvbox::errorMessage(c); }
inline std::string codeName(int c) { return std::to_string(c); }

#define CHECK_CODE(a, b)                                                           \
    do {                                                                            \
        auto va_ = (a);                                                             \
        auto vb_ = (b);                                                             \
        if (!(va_ == vb_))                                                          \
            tvtest::reportFailure(__FILE__, __LINE__,                               \
                                  std::string(#a " != " #b " | got: ") +             \
                                      tvtest::codeName(va_) + " vs " +              \
                                      tvtest::codeName(vb_));                       \
    } while (0)

inline int runAll() {
    int failed = 0;
    for (const auto& tc : registry()) {
        currentCase() = tc.id;
        const int before = failureCount();
        std::printf("[ RUN  ] %s %s\n", tc.id, tc.name);
        tc.fn();
        if (failureCount() == before) {
            std::printf("[  OK  ] %s\n", tc.id);
        } else {
            ++failed;
            std::printf("[ FAIL ] %s\n", tc.id);
        }
    }
    std::printf("\n%d case(s) run, %d failed, %d assertion failure(s)\n",
                static_cast<int>(registry().size()), failed, failureCount());
    return failed == 0 ? 0 : 1;
}

}  // namespace tvtest