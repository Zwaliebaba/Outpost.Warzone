#include "pch.h"
#include "CppUnitTest.h"

#include "Json.h"

using namespace Microsoft::VisualStudio::CppUnitTestFramework;
using Neuron::Json;

namespace NeuronCoreTest
{

TEST_CLASS(JsonTest)
{
public:
  TEST_METHOD(ParsesADocument)
  {
    auto r = Json::Parse(R"({"name":"CAM_1A","players":4,"neg":-12,"f":2.5e2,"ok":true,"nil":null,
      "arr":[1,2,3],"nested":{"a":[{"b":"c"}]}})");
    Assert::IsTrue(r.has_value());
    const Json& j = *r;
    Assert::IsTrue(j.IsObject());
    Assert::AreEqual(std::string("CAM_1A"), j.Find("name")->AsString());
    Assert::AreEqual(4, j.Find("players")->AsInt());
    Assert::AreEqual(-12, j.Find("neg")->AsInt());
    Assert::AreEqual(250.0, j.Find("f")->AsNumber(), 1e-9);
    Assert::IsTrue(j.Find("ok")->AsBool());
    Assert::IsTrue(j.Find("nil")->IsNull());
    Assert::AreEqual(std::size_t{3}, j.Find("arr")->Size());
    Assert::AreEqual(3, j.Find("arr")->Item(2).AsInt());
    Assert::AreEqual(std::string("c"), j.Find("nested")->Find("a")->Item(0).Find("b")->AsString());
    Assert::IsNull(j.Find("absent"));
    // declaration order is preserved
    Assert::AreEqual(std::string("name"), j.Members()[0].first);
  }

  TEST_METHOD(DecodesEscapes)
  {
    auto r = Json::Parse(R"("a\"b\\c\/d\n\t\u0041\u00e9\ud83d\ude00")");
    Assert::IsTrue(r.has_value());
    Assert::AreEqual(std::string("a\"b\\c/d\n\tA\xc3\xa9\xf0\x9f\x98\x80"), r->AsString());
  }

  TEST_METHOD(AcceptsMinimalDocuments)
  {
    Assert::IsTrue(Json::Parse("  [ ]  ").has_value());
    Assert::IsTrue(Json::Parse("{}")->IsObject());
    Assert::IsTrue(Json::Parse("[[[[]]]]").has_value());
    Assert::AreEqual(std::string("x"), Json::Parse("\"x\"")->AsString());
    Assert::AreEqual(0, Json::Parse("0")->AsInt());
    Assert::AreEqual(-0.5, Json::Parse("-0.5")->AsNumber(), 0.0);
  }

  TEST_METHOD(RejectsWhatStrictJsonRejects)
  {
    // one line per rejected shape: trailing commas, comments, single quotes,
    // malformed numbers, bad literals, trailing content, broken strings
    Assert::IsFalse(Json::Parse("{,}").has_value());
    Assert::IsFalse(Json::Parse("[1,]").has_value());
    Assert::IsFalse(Json::Parse("{\"a\":1,}").has_value());
    Assert::IsFalse(Json::Parse("// c\n{}").has_value());
    Assert::IsFalse(Json::Parse("{'a':1}").has_value());
    Assert::IsFalse(Json::Parse("01").has_value());
    Assert::IsFalse(Json::Parse("+1").has_value());
    Assert::IsFalse(Json::Parse("1.").has_value());
    Assert::IsFalse(Json::Parse(".5").has_value());
    Assert::IsFalse(Json::Parse("truex").has_value());
    Assert::IsFalse(Json::Parse("{} {}").has_value());
    Assert::IsFalse(Json::Parse("\"unterminated").has_value());
    Assert::IsFalse(Json::Parse("\"bad\\q\"").has_value());
    Assert::IsFalse(Json::Parse("\"\\ud800\"").has_value());
    Assert::IsFalse(Json::Parse("").has_value());
  }

  TEST_METHOD(ReportsErrorPosition)
  {
    auto e = Json::Parse("{\n  \"a\": ?\n}");
    Assert::IsFalse(e.has_value());
    Assert::AreEqual(std::size_t{2}, e.error().line);
    Assert::AreEqual(std::size_t{8}, e.error().column);
  }

  TEST_METHOD(DepthGuardFailsCleanly)
  {
    const std::string deep(200, '[');
    Assert::IsFalse(Json::Parse(deep).has_value());
  }
};

} // namespace NeuronCoreTest
