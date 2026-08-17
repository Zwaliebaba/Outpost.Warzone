/*
 * ScriptLex.h
 *
 * The lexer shared by the .slo script compiler and the .vlo value-file
 * parser.  Tokenises a whole in-memory buffer; identifiers are classified
 * by the parsers, not here.  Keywords are case-insensitive (the shipped
 * scripts use and/AND, true/TRUE interchangeably); identifiers are
 * case-sensitive.
 */
#ifndef _scriptlex_h
#define _scriptlex_h

#include <string>
#include <vector>

namespace Neuron
{
enum class ScriptTok
{
  End, // end of input

  Ident,
  Integer,
  Text, // "quoted string"

  // Keywords
  KwPublic,
  KwPrivate,
  KwTrigger,
  KwEvent,
  KwWait,
  KwEvery,
  KwInactive,
  KwInit, // "init" and "initialise"
  KwLink,
  KwRef,
  KwWhile,
  KwIf,
  KwElse,
  KwExit,
  KwPause,
  KwAnd,
  KwOr,
  KwNot,
  KwTrue,
  KwFalse,
  KwFunction,
  KwReturn,
  KwVoid,
  KwInt, // base type: int / INT
  KwBool, // base type: bool / BOOL
  KwRun, // .vlo only
  KwStore, // .vlo only
  KwScript, // .vlo only

  // Operators and punctuation
  Assign, // =
  Equal, // ==
  NotEqual, // !=
  GreaterEqual, // >=
  LessEqual, // <=
  Greater, // >
  Less, // <
  Plus,
  Minus,
  Star,
  Slash,
  Dot,
  Comma,
  Semicolon,
  LParen,
  RParen,
  LBrace,
  RBrace,
  LBracket,
  RBracket,
};

struct ScriptToken
{
  ScriptTok type = ScriptTok::End;
  SDWORD ival = 0; // Integer value
  std::string text; // Ident spelling / Text contents
  UDWORD line = 0; // 1-based
  UDWORD col = 0; // 1-based
};

/* Tokenises the whole buffer.  On a malformed token (unterminated string
   or comment, integer overflow, stray character) reports through
   Neuron::Fatal naming the line and column. */
std::vector<ScriptToken> ScriptTokenise(const UBYTE* _pData, UDWORD _size);

/* The spelling of a token type, for error messages */
const char* ScriptTokName(ScriptTok _type);
} // namespace Neuron

#endif
