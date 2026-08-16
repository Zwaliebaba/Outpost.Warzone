#include "pch.h"
#include "Json.h"

#include <cassert>
#include <cctype>
#include <cstdlib>
#include <cstring>

namespace Neuron
{

namespace
{
  const std::string g_emptyString;
}

const std::string& Json::AsString() const
{
  return m_kind == Kind::String ? m_string : g_emptyString;
}

const Json& Json::Item(std::size_t _index) const
{
  assert(m_kind == Kind::Array && _index < m_array.size());
  static const Json nullValue;
  if (m_kind != Kind::Array || _index >= m_array.size())
    return nullValue;
  return m_array[_index];
}

const Json* Json::Find(std::string_view _key) const
{
  if (m_kind != Kind::Object)
    return nullptr;
  for (const auto& [key, value] : m_members)
  {
    if (key == _key)
      return &value;
  }
  return nullptr;
}

/// Recursive-descent parser over the whole buffer. Errors carry line/column
/// computed from the byte offset when the error is raised, so the happy path
/// never counts newlines.
class JsonParser
{
public:
  explicit JsonParser(std::string_view _text) : m_text(_text) {}

  std::expected<Json, Json::Error> Run()
  {
    SkipWhitespace();
    Json root;
    if (!ParseValue(root, 0))
      return std::unexpected(m_error);
    SkipWhitespace();
    if (m_pos != m_text.size())
    {
      Fail("trailing content after the document");
      return std::unexpected(m_error);
    }
    return root;
  }

private:
  // Deep enough for any manifest, shallow enough that a malformed file
  // cannot run the thread out of stack.
  static constexpr int MaxDepth = 64;

  std::string_view m_text;
  std::size_t m_pos = 0;
  Json::Error m_error;

  [[nodiscard]] bool AtEnd() const { return m_pos >= m_text.size(); }
  [[nodiscard]] char Peek() const { return m_text[m_pos]; }

  bool Fail(const char* _message)
  {
    m_error.line = 1;
    m_error.column = 1;
    for (std::size_t i = 0; i < m_pos && i < m_text.size(); ++i)
    {
      if (m_text[i] == '\n')
      {
        m_error.line += 1;
        m_error.column = 1;
      }
      else
        m_error.column += 1;
    }
    m_error.message = _message;
    return false;
  }

  void SkipWhitespace()
  {
    while (!AtEnd())
    {
      const char c = Peek();
      if (c != ' ' && c != '\t' && c != '\n' && c != '\r')
        break;
      m_pos += 1;
    }
  }

  bool Literal(const char* _word)
  {
    const std::size_t len = strlen(_word);
    if (m_text.size() - m_pos < len || m_text.compare(m_pos, len, _word) != 0)
      return Fail("invalid literal");
    m_pos += len;
    return true;
  }

  bool ParseValue(Json& _out, int _depth)
  {
    if (_depth > MaxDepth)
      return Fail("nesting too deep");
    if (AtEnd())
      return Fail("unexpected end of document");

    switch (Peek())
    {
    case '{': return ParseObject(_out, _depth);
    case '[': return ParseArray(_out, _depth);
    case '"':
      _out.m_kind = Json::Kind::String;
      return ParseString(_out.m_string);
    case 't':
      _out.m_kind = Json::Kind::Bool;
      _out.m_bool = true;
      return Literal("true");
    case 'f':
      _out.m_kind = Json::Kind::Bool;
      _out.m_bool = false;
      return Literal("false");
    case 'n':
      _out.m_kind = Json::Kind::Null;
      return Literal("null");
    default: return ParseNumber(_out);
    }
  }

  bool ParseObject(Json& _out, int _depth)
  {
    _out.m_kind = Json::Kind::Object;
    m_pos += 1; // '{'
    SkipWhitespace();
    if (!AtEnd() && Peek() == '}')
    {
      m_pos += 1;
      return true;
    }
    for (;;)
    {
      SkipWhitespace();
      if (AtEnd() || Peek() != '"')
        return Fail("expected a member name");
      std::string key;
      if (!ParseString(key))
        return false;
      SkipWhitespace();
      if (AtEnd() || Peek() != ':')
        return Fail("expected ':' after the member name");
      m_pos += 1;
      SkipWhitespace();
      Json value;
      if (!ParseValue(value, _depth + 1))
        return false;
      _out.m_members.emplace_back(std::move(key), std::move(value));
      SkipWhitespace();
      if (AtEnd())
        return Fail("unterminated object");
      if (Peek() == ',')
      {
        m_pos += 1;
        continue;
      }
      if (Peek() == '}')
      {
        m_pos += 1;
        return true;
      }
      return Fail("expected ',' or '}' in object");
    }
  }

  bool ParseArray(Json& _out, int _depth)
  {
    _out.m_kind = Json::Kind::Array;
    m_pos += 1; // '['
    SkipWhitespace();
    if (!AtEnd() && Peek() == ']')
    {
      m_pos += 1;
      return true;
    }
    for (;;)
    {
      SkipWhitespace();
      Json value;
      if (!ParseValue(value, _depth + 1))
        return false;
      _out.m_array.push_back(std::move(value));
      SkipWhitespace();
      if (AtEnd())
        return Fail("unterminated array");
      if (Peek() == ',')
      {
        m_pos += 1;
        continue;
      }
      if (Peek() == ']')
      {
        m_pos += 1;
        return true;
      }
      return Fail("expected ',' or ']' in array");
    }
  }

  bool HexDigit(unsigned& _out)
  {
    if (AtEnd())
      return Fail("unterminated \\u escape");
    const char c = Peek();
    if (c >= '0' && c <= '9')
      _out = static_cast<unsigned>(c - '0');
    else if (c >= 'a' && c <= 'f')
      _out = static_cast<unsigned>(c - 'a' + 10);
    else if (c >= 'A' && c <= 'F')
      _out = static_cast<unsigned>(c - 'A' + 10);
    else
      return Fail("invalid \\u escape digit");
    m_pos += 1;
    return true;
  }

  bool HexQuad(unsigned& _out)
  {
    _out = 0;
    for (int i = 0; i < 4; ++i)
    {
      unsigned digit;
      if (!HexDigit(digit))
        return false;
      _out = (_out << 4) | digit;
    }
    return true;
  }

  static void AppendUtf8(std::string& _out, unsigned _codepoint)
  {
    if (_codepoint < 0x80)
      _out.push_back(static_cast<char>(_codepoint));
    else if (_codepoint < 0x800)
    {
      _out.push_back(static_cast<char>(0xC0 | (_codepoint >> 6)));
      _out.push_back(static_cast<char>(0x80 | (_codepoint & 0x3F)));
    }
    else if (_codepoint < 0x10000)
    {
      _out.push_back(static_cast<char>(0xE0 | (_codepoint >> 12)));
      _out.push_back(static_cast<char>(0x80 | ((_codepoint >> 6) & 0x3F)));
      _out.push_back(static_cast<char>(0x80 | (_codepoint & 0x3F)));
    }
    else
    {
      _out.push_back(static_cast<char>(0xF0 | (_codepoint >> 18)));
      _out.push_back(static_cast<char>(0x80 | ((_codepoint >> 12) & 0x3F)));
      _out.push_back(static_cast<char>(0x80 | ((_codepoint >> 6) & 0x3F)));
      _out.push_back(static_cast<char>(0x80 | (_codepoint & 0x3F)));
    }
  }

  bool ParseString(std::string& _out)
  {
    m_pos += 1; // '"'
    for (;;)
    {
      if (AtEnd())
        return Fail("unterminated string");
      const unsigned char c = static_cast<unsigned char>(Peek());
      if (c == '"')
      {
        m_pos += 1;
        return true;
      }
      if (c < 0x20)
        return Fail("unescaped control character in string");
      if (c != '\\')
      {
        _out.push_back(static_cast<char>(c));
        m_pos += 1;
        continue;
      }
      m_pos += 1; // '\'
      if (AtEnd())
        return Fail("unterminated escape");
      const char esc = Peek();
      m_pos += 1;
      switch (esc)
      {
      case '"': _out.push_back('"');
        break;
      case '\\': _out.push_back('\\');
        break;
      case '/': _out.push_back('/');
        break;
      case 'b': _out.push_back('\b');
        break;
      case 'f': _out.push_back('\f');
        break;
      case 'n': _out.push_back('\n');
        break;
      case 'r': _out.push_back('\r');
        break;
      case 't': _out.push_back('\t');
        break;
      case 'u':
      {
        unsigned codepoint;
        if (!HexQuad(codepoint))
          return false;
        if (codepoint >= 0xD800 && codepoint <= 0xDBFF)
        {
          // high surrogate - a low surrogate must follow
          if (m_text.size() - m_pos < 2 || Peek() != '\\' || m_text[m_pos + 1] != 'u')
            return Fail("high surrogate without a low surrogate");
          m_pos += 2;
          unsigned low;
          if (!HexQuad(low))
            return false;
          if (low < 0xDC00 || low > 0xDFFF)
            return Fail("invalid low surrogate");
          codepoint = 0x10000 + ((codepoint - 0xD800) << 10) + (low - 0xDC00);
        }
        else if (codepoint >= 0xDC00 && codepoint <= 0xDFFF)
          return Fail("stray low surrogate");
        AppendUtf8(_out, codepoint);
        break;
      }
      default: return Fail("invalid escape character");
      }
    }
  }

  bool ParseNumber(Json& _out)
  {
    const std::size_t start = m_pos;
    if (!AtEnd() && Peek() == '-')
      m_pos += 1;
    // integer part: 0, or 1-9 followed by digits
    if (AtEnd() || !isdigit(static_cast<unsigned char>(Peek())))
      return Fail("invalid value");
    if (Peek() == '0')
      m_pos += 1;
    else
    {
      while (!AtEnd() && isdigit(static_cast<unsigned char>(Peek())))
        m_pos += 1;
    }
    // fraction
    if (!AtEnd() && Peek() == '.')
    {
      m_pos += 1;
      if (AtEnd() || !isdigit(static_cast<unsigned char>(Peek())))
        return Fail("digit expected after decimal point");
      while (!AtEnd() && isdigit(static_cast<unsigned char>(Peek())))
        m_pos += 1;
    }
    // exponent
    if (!AtEnd() && (Peek() == 'e' || Peek() == 'E'))
    {
      m_pos += 1;
      if (!AtEnd() && (Peek() == '+' || Peek() == '-'))
        m_pos += 1;
      if (AtEnd() || !isdigit(static_cast<unsigned char>(Peek())))
        return Fail("digit expected in exponent");
      while (!AtEnd() && isdigit(static_cast<unsigned char>(Peek())))
        m_pos += 1;
    }

    const std::string number(m_text.substr(start, m_pos - start));
    _out.m_kind = Json::Kind::Number;
    _out.m_number = strtod(number.c_str(), nullptr);
    return true;
  }
};

std::expected<Json, Json::Error> Json::Parse(std::string_view _text)
{
  return JsonParser(_text).Run();
}

} // namespace Neuron
