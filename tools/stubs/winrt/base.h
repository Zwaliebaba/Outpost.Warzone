/* Cross-check shim for winrt/base.h. NOT part of the build.
 *
 * crosscheck.py copies this into the shadow tree's stub directory because
 * mingw-w64 ships no C++/WinRT at all, so MovieStream.cpp and Sequence.cpp -
 * the Phase 6 FMV path - could not be syntax-checked without it. MSVC never
 * sees this file: it uses the real header from the Windows SDK.
 *
 * That makes this weaker than the rest of the cross-check, in the same way
 * tools/stubs/x3daudio.h is: it verifies our use of com_ptr against a
 * transcription rather than against the SDK, so it catches arity, spelling
 * and type errors in our code but cannot catch a mistake in the
 * transcription itself.
 *
 * Only the members the tree actually uses are declared - construction,
 * destruction, get(), put(), operator->, and comparison against nullptr.
 * The real com_ptr has a much larger surface (as(), copy_from(), attach(),
 * detach(), try_as() and the rest); if a new use appears, the cross-check
 * will fail here rather than silently accept it, which is the intended
 * behaviour. Semantics transcribed from the documented contract:
 *
 *   - put() returns the address of a null slot, for a function that hands
 *     back an already-AddRef'd pointer. The real one asserts the slot is
 *     empty first; releasing here keeps that from leaking in the shim.
 *   - the destructor releases, and copying is suppressed so a checked
 *     translation unit cannot lean on reference-count semantics this
 *     transcription does not model.
 */
#pragma once

#include <unknwn.h>
#include <cstddef>

namespace winrt
{

template <typename T>
struct com_ptr
{
  com_ptr() noexcept = default;
  com_ptr(std::nullptr_t) noexcept {}

  ~com_ptr() noexcept
  {
    if (m_ptr != nullptr)
      m_ptr->Release();
  }

  com_ptr(const com_ptr&) = delete;
  com_ptr& operator=(const com_ptr&) = delete;

  com_ptr& operator=(std::nullptr_t) noexcept
  {
    if (m_ptr != nullptr)
      m_ptr->Release();
    m_ptr = nullptr;
    return *this;
  }

  T** put() noexcept
  {
    if (m_ptr != nullptr)
    {
      m_ptr->Release();
      m_ptr = nullptr;
    }
    return &m_ptr;
  }

  void** put_void() noexcept { return reinterpret_cast<void**>(put()); }

  T* get() const noexcept { return m_ptr; }
  T* operator->() const noexcept { return m_ptr; }
  explicit operator bool() const noexcept { return m_ptr != nullptr; }

  friend bool operator==(const com_ptr& _p, std::nullptr_t) noexcept { return _p.m_ptr == nullptr; }
  friend bool operator!=(const com_ptr& _p, std::nullptr_t) noexcept { return _p.m_ptr != nullptr; }

private:
  T* m_ptr = nullptr;
};

} // namespace winrt
