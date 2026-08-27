/* A stand-in for MSVC's CppUnitTest.h, so mingw can syntax-check the test
 * projects. Like the other files under tools/stubs, this is a transcription of
 * somebody else's API: it checks our use of it, not itself.
 *
 * It exists because the test sources are the part of the tree most exposed to
 * Windows header pollution and had no local gate at all. NetWireTest declared
 * a `char small[8]` and MSVC rejected it, because rpcndr.h -- which arrives
 * with Windows.h -- carries `#define small char`. mingw defines it too, so a
 * cross-check would have caught that before CI did.
 *
 * AreEqual deliberately takes one type rather than two. That is what MSVC's
 * primary template does, so a call whose arguments disagree fails here the way
 * it fails there, instead of passing locally and failing on the build agent.
 *
 * What this cannot do is run a test or check an assertion: the bodies are
 * compiled and discarded. vstest.console on the real framework is what says
 * whether they pass.
 */

#pragma once

namespace Microsoft
{
namespace VisualStudio
{
namespace CppUnitTestFramework
{

class Assert
{
public:
  /* The trailing wchar_t message every assertion takes, and the tolerance
     form of AreEqual for floating point. Both are real signatures the suites
     already use, so leaving them out would fail here against a green build. */
  template <typename T>
  static void AreEqual(const T& _expected, const T& _actual, const wchar_t* _message = nullptr)
  {
    (void)_expected;
    (void)_actual;
    (void)_message;
  }

  static void AreEqual(double _expected, double _actual, double _tolerance, const wchar_t* _message = nullptr)
  {
    (void)_expected;
    (void)_actual;
    (void)_tolerance;
    (void)_message;
  }

  template <typename T>
  static void AreNotEqual(const T& _notExpected, const T& _actual, const wchar_t* _message = nullptr)
  {
    (void)_notExpected;
    (void)_actual;
    (void)_message;
  }

  static void IsTrue(bool _condition, const wchar_t* _message = nullptr)
  {
    (void)_condition;
    (void)_message;
  }

  static void IsFalse(bool _condition, const wchar_t* _message = nullptr)
  {
    (void)_condition;
    (void)_message;
  }

  template <typename T>
  static void IsNull(const T* _pointer, const wchar_t* _message = nullptr)
  {
    (void)_pointer;
    (void)_message;
  }

  template <typename T>
  static void IsNotNull(const T* _pointer, const wchar_t* _message = nullptr)
  {
    (void)_pointer;
    (void)_message;
  }

  static void Fail(const wchar_t* _message = nullptr) { (void)_message; }
};

} // namespace CppUnitTestFramework
} // namespace VisualStudio
} // namespace Microsoft

/* The framework's macros declare a class of test methods. A struct and plain
 * member functions are enough to compile the bodies, which is all this is for.
 */
#define TEST_CLASS(name) struct name
#define TEST_METHOD(name) void name()
#define TEST_METHOD_INITIALIZE(name) void name()
#define TEST_METHOD_CLEANUP(name) void name()
#define TEST_CLASS_INITIALIZE(name) static void name()
#define TEST_CLASS_CLEANUP(name) static void name()
