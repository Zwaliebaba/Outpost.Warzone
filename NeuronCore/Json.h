// NeuronCore/Json.h
//
// A strict JSON reader for the asset manifests. DOM-style: Parse() returns
// the document root, values are queried by kind. Owns no third-party code
// (AGENTS.md R14); the game only parses trusted local files, so the parser
// is strict and small rather than forgiving: no comments, no trailing
// commas, exactly the RFC 8259 grammar.
#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace Neuron
{

class Json
{
public:
  enum class Kind : std::uint8_t { Null, Bool, Number, String, Array, Object };

  struct Error
  {
    std::size_t line; // 1-based
    std::size_t column; // 1-based
    std::string message;
  };

  /// Parse a complete document. _text must outlive nothing - all strings are
  /// copied into the returned tree.
  [[nodiscard]] static std::expected<Json, Error> Parse(std::string_view _text);

  Json() = default;

  [[nodiscard]] Kind Type() const { return m_kind; }
  [[nodiscard]] bool IsNull() const { return m_kind == Kind::Null; }
  [[nodiscard]] bool IsBool() const { return m_kind == Kind::Bool; }
  [[nodiscard]] bool IsNumber() const { return m_kind == Kind::Number; }
  [[nodiscard]] bool IsString() const { return m_kind == Kind::String; }
  [[nodiscard]] bool IsArray() const { return m_kind == Kind::Array; }
  [[nodiscard]] bool IsObject() const { return m_kind == Kind::Object; }

  /// Wrong-kind access returns the type's empty value; callers validate the
  /// kind first (the manifest loader treats a wrong kind as a data error).
  [[nodiscard]] bool AsBool() const { return m_kind == Kind::Bool && m_bool; }
  [[nodiscard]] double AsNumber() const { return m_kind == Kind::Number ? m_number : 0.0; }
  [[nodiscard]] int AsInt() const { return static_cast<int>(AsNumber()); }
  [[nodiscard]] const std::string& AsString() const;

  /// Array access. Size() is 0 for non-arrays; Item() asserts in debug.
  [[nodiscard]] std::size_t Size() const { return m_array.size(); }
  [[nodiscard]] const Json& Item(std::size_t _index) const;

  /// Object access. Find returns nullptr when the key is absent or this is
  /// not an object. Members preserves declaration order.
  [[nodiscard]] const Json* Find(std::string_view _key) const;
  [[nodiscard]] const std::vector<std::pair<std::string, Json>>& Members() const { return m_members; }

private:
  friend class JsonParser;

  Kind m_kind = Kind::Null;
  bool m_bool = false;
  double m_number = 0.0;
  std::string m_string;
  std::vector<Json> m_array;
  std::vector<std::pair<std::string, Json>> m_members;
};

} // namespace Neuron
