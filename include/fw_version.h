#pragma once

// Compiled-in firmware identity. kFwVer is display-only; all comparisons use
// kFwVerNum. Keep them in lockstep - the host test asserts they agree.
namespace p1fw {

#if defined(P1_TEST_FW_VER) && defined(P1_TEST_FW_VER_NUM)
constexpr char kFwVer[] = P1_TEST_FW_VER;
constexpr unsigned kFwVerNum = P1_TEST_FW_VER_NUM;
#else
constexpr char kFwVer[] = "3.04";
constexpr unsigned kFwVerNum = 304;
#endif

}  // namespace p1fw
