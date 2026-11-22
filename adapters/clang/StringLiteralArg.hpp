#pragma once

// Strängliteral som argument till en `const char*`-parameter:
// `print_line("klart")`. Delas av LocalCheck (Stone) och CTranslate (Silver).
//
// Literalen är oföränderlig och lever hela programmet, så att skicka den
// läser bara konstant data. Pekaren tillåts bara här, direkt som argument:
// inte i variabler, aritmetik, jämförelser eller ?:. En Stone-funktion kan
// inte själv ha en `const char*`-parameter, så mottagaren är betrodd (eller
// inte Stone).

#include <clang/AST/Expr.h>
#include <clang/AST/Type.h>

#include <string>

namespace pork {

// `const char*` (vanlig char, inte signed/unsigned char).
inline bool isConstCharPointer(clang::QualType t) {
  using namespace clang;
  const auto* p = t.getCanonicalType()->getAs<PointerType>();
  if (p == nullptr) return false;
  QualType pointee = p->getPointeeType();
  if (!pointee.isConstQualified() || pointee.isVolatileQualified()) return false;
  return pointee->isSpecificBuiltinType(BuiltinType::Char_S) || pointee->isSpecificBuiltinType(BuiltinType::Char_U);
}

// Literalen om `arg` är en vanlig strängliteral (inte u8/u/U/L) som omvandlas
// till parametertypen `param` = `const char*`, annars nullptr. I C++ är
// literalen `const char[N]` (ArrayToPointerDecay); i C är den `char[N]`
// (ArrayToPointerDecay + NoOp till const).
inline const clang::StringLiteral* stringLiteralArg(const clang::Expr* arg, clang::QualType param) {
  using namespace clang;
  if (arg == nullptr || !isConstCharPointer(param)) return nullptr;
  const Expr* e = arg->IgnoreParens();
  if (const auto* noop = dyn_cast<ImplicitCastExpr>(e); noop != nullptr && noop->getCastKind() == CK_NoOp)
    e = noop->getSubExpr()->IgnoreParens();
  const auto* decay = dyn_cast<ImplicitCastExpr>(e);
  if (decay == nullptr || decay->getCastKind() != CK_ArrayToPointerDecay) return nullptr;
  const auto* lit = dyn_cast<StringLiteral>(decay->getSubExpr()->IgnoreParens());
  if (lit == nullptr || !lit->isOrdinary()) return nullptr;
  return lit;
}

// Literalens innehåll som C-literal; allt utom skrivbar ASCII som oktalt
// escape, så att resultatet alltid är giltig C.
inline std::string cStringLiteral(const clang::StringLiteral& lit) {
  std::string out = "\"";
  for (char ch : lit.getString()) {
    auto c = static_cast<unsigned char>(ch);
    if (c == '"' || c == '\\') {
      out += '\\';
      out += static_cast<char>(c);
    } else if (c >= 0x20 && c < 0x7f) {
      out += static_cast<char>(c);
    } else {
      out += '\\';
      out += static_cast<char>('0' + ((c >> 6) & 7));
      out += static_cast<char>('0' + ((c >> 3) & 7));
      out += static_cast<char>('0' + (c & 7));
    }
  }
  return out + "\"";
}

}  // namespace pork
