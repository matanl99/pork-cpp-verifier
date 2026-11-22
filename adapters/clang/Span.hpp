#pragma once

// std::span<const T> (dynamisk storlek) som parameter: en läsvy över en
// std::vector. Delas av LocalCheck (Stone) och CTranslate (Silver).
//
// En span är ingen värdetyp (den pekar in i någon annans minne), så den tillåts
// bara som parameter. Vid anropet får den bara skapas från en std::vector med
// samma elementtyp, eller kopieras från en annan span-parameter (spanSource).
// Anroparen behandlar källan som ett const&-argument.

#include <clang/AST/DeclCXX.h>
#include <clang/AST/DeclTemplate.h>
#include <clang/AST/Expr.h>
#include <clang/AST/ExprCXX.h>

namespace pork {

// std::span<const T, dynamic_extent>, annars nullptr.
inline const clang::ClassTemplateSpecializationDecl* constSpanSpec(clang::QualType t) {
  using namespace clang;
  if (t.isNull()) return nullptr;
  const auto* spec = dyn_cast_or_null<ClassTemplateSpecializationDecl>(
      t.getNonReferenceType().getCanonicalType()->getAsCXXRecordDecl());
  if (spec == nullptr || spec->getSpecializedTemplate()->getQualifiedNameAsString() != "std::span") return nullptr;
  const TemplateArgumentList& args = spec->getTemplateArgs();
  if (args.size() != 2 || args[0].getKind() != TemplateArgument::Type || args[1].getKind() != TemplateArgument::Integral)
    return nullptr;
  if (!args[0].getAsType().isConstQualified()) return nullptr;
  if (!args[1].getAsIntegral().isMaxValue()) return nullptr;  // dynamic_extent = size_t(-1)
  return spec;
}

// Elementtypen T (utan const).
inline clang::QualType spanElement(const clang::ClassTemplateSpecializationDecl& spec) {
  return spec.getTemplateArgs()[0].getAsType().getUnqualifiedType();
}

inline bool isStdVectorOf(clang::QualType t, clang::QualType element, const clang::ASTContext& ctx) {
  using namespace clang;
  const auto* spec = dyn_cast_or_null<ClassTemplateSpecializationDecl>(
      t.getNonReferenceType().getCanonicalType()->getAsCXXRecordDecl());
  if (spec == nullptr || spec->getSpecializedTemplate()->getQualifiedNameAsString() != "std::vector") return false;
  return ctx.hasSameUnqualifiedType(spec->getTemplateArgs()[0].getAsType(), element);
}

// En span-parameter (DeclRefExpr till en ParmVarDecl av typen span<const T>).
inline const clang::ParmVarDecl* spanParam(const clang::Expr* e) {
  using namespace clang;
  e = e->IgnoreParens();
  if (const auto* c = dyn_cast<ImplicitCastExpr>(e); c != nullptr && c->getCastKind() == CK_NoOp)
    e = c->getSubExpr()->IgnoreParens();
  const auto* dr = dyn_cast<DeclRefExpr>(e);
  const auto* p = dr ? dyn_cast<ParmVarDecl>(dr->getDecl()) : nullptr;
  return p != nullptr && constSpanSpec(p->getType()) != nullptr ? p : nullptr;
}

// Uttrycket som skapar en span<const T> vid ett anrop: källan (en std::vector
// med samma elementtyp, eller en span-parameter), annars nullptr. `construct`
// får konstruktoranropet (för att registrera konstruktorn som anropad).
inline const clang::Expr* spanSource(const clang::Expr* e, const clang::ASTContext& ctx,
                                     const clang::CXXConstructExpr** construct = nullptr) {
  using namespace clang;
  while (true) {
    e = e->IgnoreParens();
    if (const auto* w = dyn_cast<ExprWithCleanups>(e)) e = w->getSubExpr();
    else if (const auto* m = dyn_cast<MaterializeTemporaryExpr>(e)) e = m->getSubExpr();
    else if (const auto* b = dyn_cast<CXXBindTemporaryExpr>(e)) e = b->getSubExpr();
    else if (const auto* c = dyn_cast<ImplicitCastExpr>(e);
             c != nullptr && (c->getCastKind() == CK_ConstructorConversion || c->getCastKind() == CK_NoOp))
      e = c->getSubExpr();
    else break;
  }
  const auto* ce = dyn_cast<CXXConstructExpr>(e);
  if (ce == nullptr || ce->getNumArgs() != 1) return nullptr;
  const auto* spec = constSpanSpec(ce->getType());
  if (spec == nullptr) return nullptr;
  const Expr* src = ce->getArg(0);
  if (construct != nullptr) *construct = ce;
  if (spanParam(src) != nullptr) return src;
  if (isStdVectorOf(src->getType(), spanElement(*spec), ctx)) return src;
  return nullptr;
}

}  // namespace pork
