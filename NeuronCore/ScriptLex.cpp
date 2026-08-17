#include "pch.h"

#include "Frame.h"
#include "ScriptLex.h"

#include <cctype>

namespace Neuron
{
namespace
{
struct Keyword
{
  const char* name;
  ScriptTok tok;
};

/* Matched case-insensitively.  The corpus was measured before choosing
   that: no identifier in any shipped script collides with a keyword under
   case folding, while and/AND and true/TRUE are both in live use. */
constexpr Keyword kKeywords[] = {
  {"public", ScriptTok::KwPublic},
  {"private", ScriptTok::KwPrivate},
  {"trigger", ScriptTok::KwTrigger},
  {"event", ScriptTok::KwEvent},
  {"wait", ScriptTok::KwWait},
  {"every", ScriptTok::KwEvery},
  {"inactive", ScriptTok::KwInactive},
  {"init", ScriptTok::KwInit},
  {"initialise", ScriptTok::KwInit},
  {"link", ScriptTok::KwLink},
  {"ref", ScriptTok::KwRef},
  {"while", ScriptTok::KwWhile},
  {"if", ScriptTok::KwIf},
  {"else", ScriptTok::KwElse},
  {"exit", ScriptTok::KwExit},
  {"pause", ScriptTok::KwPause},
  {"and", ScriptTok::KwAnd},
  {"or", ScriptTok::KwOr},
  {"not", ScriptTok::KwNot},
  {"true", ScriptTok::KwTrue},
  {"false", ScriptTok::KwFalse},
  {"function", ScriptTok::KwFunction},
  {"return", ScriptTok::KwReturn},
  {"void", ScriptTok::KwVoid},
  {"run", ScriptTok::KwRun},
  {"store", ScriptTok::KwStore},
  {"script", ScriptTok::KwScript},
};

bool EqualNoCase(const std::string& _word, const char* _keyword)
{
  const char* p = _keyword;
  for (const char c : _word)
  {
    if (*p == '\0' || std::tolower(static_cast<unsigned char>(c)) != *p)
      return false;
    p += 1;
  }
  return *p == '\0';
}
} // namespace

const char* ScriptTokName(ScriptTok _type)
{
  switch (_type)
  {
  case ScriptTok::End: return "end of file";
  case ScriptTok::Ident: return "identifier";
  case ScriptTok::Integer: return "integer";
  case ScriptTok::Text: return "string";
  case ScriptTok::KwPublic: return "public";
  case ScriptTok::KwPrivate: return "private";
  case ScriptTok::KwTrigger: return "trigger";
  case ScriptTok::KwEvent: return "event";
  case ScriptTok::KwWait: return "wait";
  case ScriptTok::KwEvery: return "every";
  case ScriptTok::KwInactive: return "inactive";
  case ScriptTok::KwInit: return "init";
  case ScriptTok::KwLink: return "link";
  case ScriptTok::KwRef: return "ref";
  case ScriptTok::KwWhile: return "while";
  case ScriptTok::KwIf: return "if";
  case ScriptTok::KwElse: return "else";
  case ScriptTok::KwExit: return "exit";
  case ScriptTok::KwPause: return "pause";
  case ScriptTok::KwAnd: return "and";
  case ScriptTok::KwOr: return "or";
  case ScriptTok::KwNot: return "not";
  case ScriptTok::KwTrue: return "true";
  case ScriptTok::KwFalse: return "false";
  case ScriptTok::KwFunction: return "function";
  case ScriptTok::KwReturn: return "return";
  case ScriptTok::KwVoid: return "void";
  case ScriptTok::KwRun: return "run";
  case ScriptTok::KwStore: return "store";
  case ScriptTok::KwScript: return "script";
  case ScriptTok::Assign: return "=";
  case ScriptTok::Equal: return "==";
  case ScriptTok::NotEqual: return "!=";
  case ScriptTok::GreaterEqual: return ">=";
  case ScriptTok::LessEqual: return "<=";
  case ScriptTok::Greater: return ">";
  case ScriptTok::Less: return "<";
  case ScriptTok::Plus: return "+";
  case ScriptTok::Minus: return "-";
  case ScriptTok::Star: return "*";
  case ScriptTok::Slash: return "/";
  case ScriptTok::Dot: return ".";
  case ScriptTok::Comma: return ",";
  case ScriptTok::Semicolon: return ";";
  case ScriptTok::LParen: return "(";
  case ScriptTok::RParen: return ")";
  case ScriptTok::LBrace: return "{";
  case ScriptTok::RBrace: return "}";
  case ScriptTok::LBracket: return "[";
  case ScriptTok::RBracket: return "]";
  }
  return "?";
}

std::vector<ScriptToken> ScriptTokenise(const UBYTE* _pData, UDWORD _size)
{
  std::vector<ScriptToken> aTokens;
  const char* p = reinterpret_cast<const char*>(_pData);
  const char* const end = p + _size;
  UDWORD line = 1;
  UDWORD col = 1;

  const auto advance = [&](UDWORD _count)
  {
    for (UDWORD i = 0; i < _count && p < end; i += 1)
    {
      if (*p == '\n')
      {
        line += 1;
        col = 1;
      }
      else
        col += 1;
      p += 1;
    }
  };

  const auto push = [&](ScriptTok _type, UDWORD _tokLine, UDWORD _tokCol)
  {
    ScriptToken sTok;
    sTok.type = _type;
    sTok.line = _tokLine;
    sTok.col = _tokCol;
    aTokens.push_back(sTok);
    return &aTokens.back();
  };

  while (p < end)
  {
    const char c = *p;

    // Whitespace
    if (c == ' ' || c == '\t' || c == '\r' || c == '\n')
    {
      advance(1);
      continue;
    }

    // Comments
    if (c == '/' && p + 1 < end && p[1] == '/')
    {
      while (p < end && *p != '\n')
        advance(1);
      continue;
    }
    if (c == '/' && p + 1 < end && p[1] == '*')
    {
      const UDWORD startLine = line;
      advance(2);
      while (p + 1 < end && !(p[0] == '*' && p[1] == '/'))
        advance(1);
      if (p + 1 >= end)
        Neuron::Fatal("script lex error: unterminated /* comment opened at line {}", startLine);
      advance(2);
      continue;
    }

    const UDWORD tokLine = line;
    const UDWORD tokCol = col;

    // Identifiers and keywords
    if (std::isalpha(static_cast<unsigned char>(c)) || c == '_')
    {
      const char* start = p;
      while (p < end && (std::isalnum(static_cast<unsigned char>(*p)) || *p == '_'))
        advance(1);
      std::string word(start, p);

      ScriptTok type = ScriptTok::Ident;
      for (const Keyword& sKeyword : kKeywords)
      {
        if (EqualNoCase(word, sKeyword.name))
        {
          type = sKeyword.tok;
          break;
        }
      }
      ScriptToken* psTok = push(type, tokLine, tokCol);
      if (type == ScriptTok::Ident)
        psTok->text = std::move(word);
      continue;
    }

    // Integers
    if (std::isdigit(static_cast<unsigned char>(c)))
    {
      SDWORD value = 0;
      while (p < end && std::isdigit(static_cast<unsigned char>(*p)))
      {
        const SDWORD digit = *p - '0';
        if (value > (SDWORD_MAX - digit) / 10)
          Neuron::Fatal("script lex error: integer overflows at line {} col {}", tokLine, tokCol);
        value = value * 10 + digit;
        advance(1);
      }
      push(ScriptTok::Integer, tokLine, tokCol)->ival = value;
      continue;
    }

    // Strings.  No escape sequences, as before; a newline inside is legal
    // in the old lexer only via the terminating quote being found first.
    if (c == '"')
    {
      advance(1);
      const char* start = p;
      while (p < end && *p != '"')
        advance(1);
      if (p >= end)
        Neuron::Fatal("script lex error: unterminated string at line {} col {}", tokLine, tokCol);
      push(ScriptTok::Text, tokLine, tokCol)->text.assign(start, p);
      advance(1);
      continue;
    }

    // Operators and punctuation
    const auto twoChar = [&](char _second) { return p + 1 < end && p[1] == _second; };
    ScriptTok type;
    UDWORD width = 1;
    switch (c)
    {
    case '=':
      if (twoChar('='))
      {
        type = ScriptTok::Equal;
        width = 2;
      }
      else
        type = ScriptTok::Assign;
      break;
    case '!':
      if (twoChar('='))
      {
        type = ScriptTok::NotEqual;
        width = 2;
      }
      else
        Neuron::Fatal("script lex error: stray '!' at line {} col {}", tokLine, tokCol);
      break;
    case '>':
      if (twoChar('='))
      {
        type = ScriptTok::GreaterEqual;
        width = 2;
      }
      else
        type = ScriptTok::Greater;
      break;
    case '<':
      if (twoChar('='))
      {
        type = ScriptTok::LessEqual;
        width = 2;
      }
      else
        type = ScriptTok::Less;
      break;
    case '+': type = ScriptTok::Plus;
      break;
    case '-': type = ScriptTok::Minus;
      break;
    case '*': type = ScriptTok::Star;
      break;
    case '/': type = ScriptTok::Slash;
      break;
    case '.': type = ScriptTok::Dot;
      break;
    case ',': type = ScriptTok::Comma;
      break;
    case ';': type = ScriptTok::Semicolon;
      break;
    case '(': type = ScriptTok::LParen;
      break;
    case ')': type = ScriptTok::RParen;
      break;
    case '{': type = ScriptTok::LBrace;
      break;
    case '}': type = ScriptTok::RBrace;
      break;
    case '[': type = ScriptTok::LBracket;
      break;
    case ']': type = ScriptTok::RBracket;
      break;
    default:
      Neuron::Fatal("script lex error: unexpected character '{}' at line {} col {}", c, tokLine, tokCol);
      return aTokens; // unreachable, Fatal throws
    }
    push(type, tokLine, tokCol);
    advance(width);
  }

  ScriptToken sEnd;
  sEnd.type = ScriptTok::End;
  sEnd.line = line;
  sEnd.col = col;
  aTokens.push_back(sEnd);

  return aTokens;
}
} // namespace Neuron
