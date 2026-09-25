#pragma once

// Compiled-in firmware identity. kFwVer is display-only; all comparisons use
// kFwVerNum. Keep them in lockstep - the host test asserts they agree.
namespace p1fw {

constexpr char kFwVer[] = "3.00";
constexpr unsigned kFwVerNum = 300;

}  // namespace p1fw
