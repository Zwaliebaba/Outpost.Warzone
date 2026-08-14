#pragma once

#include <format>
#include <stdexcept>
#include <string>
#include <string_view>

#if defined(_DEBUG)
#include <crtdbg.h>
#define NEW new ( _NORMAL_BLOCK , __FILE__ , __LINE__ )
#else
#define NEW new
#endif

/* DEBUG gates 80-odd blocks across the tree, several of them in headers where
 * it changes a struct's layout, so every translation unit has to agree on it.
 * LegacyDebug.h used to define it and this replaces that; the conditions are
 * unchanged.
 */
#ifdef _DEBUG
#ifndef NODEBUG
#ifndef DEBUG
#define DEBUG
#endif
#endif
#endif

#ifdef _NDEBUG
#ifndef FORCEDEBUG
#undef DEBUG
#else
#ifndef DEBUG
#define DEBUG
#endif
#endif
#endif

namespace Neuron
{
  template <class... Types>
  void DebugTrace(const std::string_view _fmt, Types&&... _args)
  {
#ifdef _DEBUG
    const std::string message = std::vformat(_fmt, std::make_format_args(_args...));
    OutputDebugStringA(message.c_str());
#else
    __noop(_fmt);
#endif
  }

  template <class... Types>
  void DebugTrace(const std::wstring_view _fmt, Types&&... _args)
  {
#ifdef _DEBUG
    const std::wstring message = std::vformat(_fmt, std::make_wformat_args(_args...));
    OutputDebugStringW(message.c_str());
#else
    __noop(_fmt);
#endif
  }

  /* Report and die. This is where the DBERROR sites end up as well as the
   * assertions, and DBERROR was deliberately visible in release builds, so the
   * message goes out before the break rather than only into the exception.
   */
  template <class... Types>
  void __declspec(noreturn) Fatal(const std::string_view _fmt, Types&&... _args)
  {
    const std::string message = std::vformat(_fmt, std::make_format_args(_args...));
    OutputDebugStringA(message.c_str());
    OutputDebugStringA("\n");
    __debugbreak();
    throw std::runtime_error(message);
  }

  template <class... Types>
  void __declspec(noreturn) Fatal(const std::wstring_view _fmt, Types&&... _args)
  {
    const std::wstring message = std::vformat(_fmt, std::make_wformat_args(_args...));
    OutputDebugStringW(message.c_str());
    OutputDebugStringW(L"\n");
    __debugbreak();
    throw std::runtime_error("Fatal Error");
  }
}

#define ASSERT(expression)                   (void)((!!(expression)) || (Neuron::Fatal("Assert failed: {} [{}:{}]", #expression, __FILE__, __LINE__), 0))
#define ASSERT_TEXT(expression, ...)         (void)((!!(expression)) || (Neuron::Fatal(__VA_ARGS__), 0))

#ifdef _DEBUG
#define DEBUG_ASSERT(expression)             ASSERT(expression)
#define DEBUG_ASSERT_TEXT(expression, ...)   ASSERT_TEXT(expression, __VA_ARGS__)
#define DEBUG_WARNING(expression, ...)       (void)((!(expression)) || (Neuron::DebugTrace(__VA_ARGS__), 0))

#else
#define DEBUG_ASSERT(expression)             (__noop(expression))
#define DEBUG_ASSERT_TEXT(expression, ...)   (__noop(expression, __VA_ARGS__))
#define DEBUG_WARNING(expression, ...)       (__noop(expression, __VA_ARGS__))

#endif
