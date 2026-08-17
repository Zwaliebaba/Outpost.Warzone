/*
 * Frame.h - host stand-in for the framework header.
 *
 * Only the handful of facilities the script compiler reaches for. Fatal
 * throws rather than aborting so one bad script does not end the run.
 */
#pragma once

#include "pch.h"

namespace Neuron
{
template <typename... T>
[[noreturn]] void Fatal(const std::string_view _fmt, T&&... _args)
{
  throw std::runtime_error(std::vformat(_fmt, std::make_format_args(_args...)));
}

template <typename... T>
void DebugTrace(const std::string_view, T&&...)
{
}
} // namespace Neuron

#define DEBUG_ASSERT_TEXT(e, ...) (void)0
