#include "CTranslate.hpp"

#include "Assert.hpp"
#include "Dispatch.hpp"
#include "Move.hpp"
#include "LocalCheck.hpp"
#include "Span.hpp"
#include "StringLiteralArg.hpp"
#include "VectorBool.hpp"

#include <cctype>
#include <clang/AST/ASTContext.h>
#include <clang/AST/Attr.h>
#include <clang/AST/DeclCXX.h>
#include <clang/AST/DeclTemplate.h>
#include <clang/AST/Expr.h>
#include <clang/AST/ExprCXX.h>
#include <clang/AST/RecursiveASTVisitor.h>
#include <clang/AST/Stmt.h>
#include <clang/AST/StmtCXX.h>
#include <clang/Basic/SourceManager.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <regex>
#include <set>

using namespace clang;
using llvm::dyn_cast;
using llvm::isa;

namespace pork {

namespace {

using Text = std::expected<std::string, std::string>;

// Sprid fel uppåt: TRY(x, uttryck) ger std::string x eller returnerar felet.
#define PORK_CAT2(a, b) a##b
#define PORK_CAT(a, b) PORK_CAT2(a, b)
#define TRY(var, ...)                                                  \
  auto PORK_CAT(var, _r) = (__VA_ARGS__);                              \
  if (!PORK_CAT(var, _r)) return std::unexpected(PORK_CAT(var, _r).error()); \
  std::string var = std::move(*PORK_CAT(var, _r))

std::unexpected<std::string> fail(std::string why) { return std::unexpected(std::move(why)); }

std::string hash8(const std::string& s) {
  std::uint32_t h = 2166136261u;  // FNV-1a: samma namn i alla översättningsenheter
  for (unsigned char c : s) {
    h ^= c;
    h *= 16777619u;
  }
  char buf[9];
  std::snprintf(buf, sizeof buf, "%08x", h);
  return buf;
}

// "rugby::operator+" -> "rugby__operator_"
std::string ident(const std::string& s) {
  std::string out;
  for (std::size_t i = 0; i < s.size(); ++i) {
    char c = s[i];
    if (c == ':' && i + 1 < s.size() && s[i + 1] == ':') {
      out += "__";
      ++i;
    } else {
      out += std::isalnum(static_cast<unsigned char>(c)) ? c : '_';
    }
  }
  return out;
}

std::string quoted(const std::string& s) {
  std::string out = "\"";
  for (char c : s) {
    if (c == '"' || c == '\\') out += '\\';
    out += c;
  }
  return out + "\"";
}

// PORK_INVARIANT för en typ (värde eller referens), om den har en.
const CXXMethodDecl* invariantOf(QualType t) {
  const auto* rd = t.getNonReferenceType().getCanonicalType()->getAsCXXRecordDecl();
  if (rd == nullptr || rd->getDefinition() == nullptr) return nullptr;
  for (const CXXMethodDecl* m : rd->getDefinition()->methods())
    if (hasAnnotation(*m, "pork::invariant")) return m;
  return nullptr;
}

// Invarianten och allt den anropar, transitivt (kanoniska deklarationer).
// De får inte själva invarianten som villkor: det blir rekursion.
bool invariantHelper(const CXXMethodDecl& inv, const FunctionDecl& fd) {
  struct Calls : RecursiveASTVisitor<Calls> {
    std::vector<const FunctionDecl*> found;
    bool VisitCallExpr(CallExpr* c) {
      if (const FunctionDecl* f = c->getDirectCallee()) found.push_back(f);
      return true;
    }
  };
  std::set<const FunctionDecl*> seen;
  std::vector<const FunctionDecl*> todo{&inv};
  while (!todo.empty()) {
    const FunctionDecl* f = todo.back();
    todo.pop_back();
    if (!seen.insert(f->getCanonicalDecl()).second) continue;
    const FunctionDecl* def = nullptr;
    if (!f->hasBody(def)) continue;
    Calls calls;
    calls.TraverseStmt(const_cast<Stmt*>(def->getBody()));
    todo.insert(todo.end(), calls.found.begin(), calls.found.end());
  }
  return seen.contains(fd.getCanonicalDecl());
}

// Gränssnittskontraktet för en virtuell metod: samma C-namn i alla TU:er
// (anroparen, kontraktsfilen, implementationernas Liskov-bevis).
std::string interfaceContractName(const CXXMethodDecl& md) {
  return "pork_ic_" + ident(displayName(md)) + "_" + hash8(usrOf(md));
}

std::string cFunctionName(const FunctionDecl& fd) {
  return "f_" + ident(displayName(fd)) + "_" + hash8(usrOf(fd));
}

const FieldDecl* asField(const ValueDecl* d) { return dyn_cast<FieldDecl>(d); }

bool isMemberwiseAssign(const CXXOperatorCallExpr& oc) {
  const auto* md = dyn_cast_or_null<CXXMethodDecl>(oc.getDirectCallee());
  return md != nullptr && (md->isCopyAssignmentOperator() || md->isMoveAssignmentOperator()) &&
         (md->isTrivial() || !md->isUserProvided());
}

const Expr* strip(const Expr* e) {
  while (true) {
    e = e->IgnoreParens();
    if (const auto* w = dyn_cast<ExprWithCleanups>(e)) {
      e = w->getSubExpr();
    } else if (const auto* c = dyn_cast<ImplicitCastExpr>(e);
               c != nullptr && c->getCastKind() == CK_NoOp) {
      e = c->getSubExpr();
    } else {
      return e;
    }
  }
}

// std::optional<T>: specialiseringen och T, annars nullptr.
const ClassTemplateSpecializationDecl* optionalSpec(const CXXRecordDecl* rd) {
  const auto* spec = dyn_cast_or_null<ClassTemplateSpecializationDecl>(rd);
  if (spec == nullptr || spec->getSpecializedTemplate()->getQualifiedNameAsString() != "std::optional")
    return nullptr;
  return spec;
}
const ClassTemplateSpecializationDecl* optionalSpec(QualType t) {
  return optionalSpec(t.getNonReferenceType().getCanonicalType()->getAsCXXRecordDecl());
}
const ClassTemplateSpecializationDecl* vectorSpec(const CXXRecordDecl* rd) {
  const auto* spec = dyn_cast_or_null<ClassTemplateSpecializationDecl>(rd);
  if (spec == nullptr || spec->getSpecializedTemplate()->getQualifiedNameAsString() != "std::vector")
    return nullptr;
  return spec;
}
const ClassTemplateSpecializationDecl* vectorSpec(QualType t) {
  return vectorSpec(t.getNonReferenceType().getCanonicalType()->getAsCXXRecordDecl());
}
// std::basic_string<char> (std::string): modelleras som en vektor av char.
const ClassTemplateSpecializationDecl* stringSpec(const CXXRecordDecl* rd) {
  const auto* spec = dyn_cast_or_null<ClassTemplateSpecializationDecl>(rd);
  if (spec == nullptr || spec->getName() != "basic_string" || !spec->isInStdNamespace()) return nullptr;
  const TemplateArgumentList& args = spec->getTemplateArgs();
  if (args.size() == 0 || args[0].getKind() != TemplateArgument::Type) return nullptr;
  QualType ch = args[0].getAsType();
  if (!ch->isSpecificBuiltinType(BuiltinType::Char_S) && !ch->isSpecificBuiltinType(BuiltinType::Char_U)) return nullptr;
  return spec;
}
const ClassTemplateSpecializationDecl* stringSpec(QualType t) {
  return stringSpec(t.getNonReferenceType().getCanonicalType()->getAsCXXRecordDecl());
}
// Sekvens: std::vector<T> eller std::string, samma modell { size, data[N] }.
const ClassTemplateSpecializationDecl* sequenceSpec(const CXXRecordDecl* rd) {
  if (const auto* v = vectorSpec(rd)) return v;
  return stringSpec(rd);
}
const ClassTemplateSpecializationDecl* sequenceSpec(QualType t) {
  return sequenceSpec(t.getNonReferenceType().getCanonicalType()->getAsCXXRecordDecl());
}
// Namnet i meddelanden: "std::vector" eller "std::string".
std::string sequenceName(const ClassTemplateSpecializationDecl& spec) {
  return stringSpec(&spec) != nullptr ? "std::string" : "std::vector";
}

const ClassTemplateSpecializationDecl* variantSpec(const CXXRecordDecl* rd) {
  const auto* spec = dyn_cast_or_null<ClassTemplateSpecializationDecl>(rd);
  if (spec == nullptr || spec->getSpecializedTemplate()->getQualifiedNameAsString() != "std::variant")
    return nullptr;
  return spec;
}
const ClassTemplateSpecializationDecl* variantSpec(QualType t) {
  return variantSpec(t.getNonReferenceType().getCanonicalType()->getAsCXXRecordDecl());
}

// Medlemsnamn för meddelanden: "operator*", "operator[]", "operator bool", "size".
std::string memberName(const CXXMethodDecl& md) {
  if (const auto* cd = dyn_cast<CXXConversionDecl>(&md))
    return cd->getConversionType()->isBooleanType() ? "operator bool" : "operator " + cd->getConversionType().getAsString();
  if (md.isOverloadedOperator())
    return std::string("operator") + getOperatorSpelling(md.getOverloadedOperator());
  return md.getNameAsString();
}

QualType optionalValue(const ClassTemplateSpecializationDecl& spec) {
  return spec.getTemplateArgs()[0].getAsType();
}
bool isNullopt(QualType t) {
  const CXXRecordDecl* rd = t.getNonReferenceType().getCanonicalType()->getAsCXXRecordDecl();
  return rd != nullptr && rd->getQualifiedNameAsString() == "std::nullopt_t";
}

// Ett uttryck som är en väg (variabel, fält, *this, eller ett element via
// v[i] / *o / o-> på en väg): ett lvalue utan sidoeffekter, som kan
// utvärderas två gånger. (Stone har redan underkänt sidoeffekter i index.)
bool isPath(const Expr* e) {
  e = strip(e);
  if (const auto* c = dyn_cast<ImplicitCastExpr>(e); c != nullptr && c->getCastKind() == CK_LValueToRValue)
    e = strip(c->getSubExpr());
  if (isa<DeclRefExpr, CXXThisExpr>(e)) return true;
  if (const auto* u = dyn_cast<UnaryOperator>(e))
    return u->getOpcode() == UO_Deref && isa<CXXThisExpr>(strip(u->getSubExpr()));
  if (const auto* m = dyn_cast<MemberExpr>(e)) {
    if (!m->isArrow()) return isPath(m->getBase());
    const Expr* base = strip(m->getBase());
    if (isa<CXXThisExpr>(base)) return true;
    if (const auto* c = dyn_cast<ImplicitCastExpr>(base); c != nullptr && c->getCastKind() == CK_LValueToRValue &&
                                                          isa<DeclRefExpr>(strip(c->getSubExpr())))
      return true;  // x->f, get_if-pekare
    const auto* oc = dyn_cast<CXXOperatorCallExpr>(base);
    return oc != nullptr && oc->getOperator() == OO_Arrow && isPath(oc->getArg(0));
  }
  if (const auto* c = dyn_cast<CallExpr>(e)) {  // element via accessor
    const auto* md = dyn_cast_or_null<CXXMethodDecl>(c->getDirectCallee());
    if (md == nullptr || !md->isInstance() || !(optionalSpec(md->getParent()) || sequenceSpec(md->getParent())))
      return false;
    std::string member = memberName(*md);
    if (member != "operator*" && member != "value" && member != "operator[]" && member != "at" &&
        member != "front" && member != "back")
      return false;
    const Expr* object = isa<CXXMemberCallExpr>(c) ? llvm::cast<CXXMemberCallExpr>(c)->getImplicitObjectArgument()
                                                     : c->getArg(0);
    return isPath(object);
  }
  return false;
}

class Translator {
 public:
  Translator(const FunctionDecl& fd, const TranslateOptions& options)
      : fd_(fd), ctx_(fd.getASTContext()), sm_(ctx_.getSourceManager()), options_(options) {}

  std::expected<CFunction, std::string> run() {
    out_.name = cFunctionName(fd_);
    if (isInterfaceContract(fd_)) {
      contractOf_ = contractedMethod(fd_);
      if (contractOf_ == nullptr) return fail("interface contract without a method");
      out_.name = interfaceContractName(*contractOf_);
      out_.contracts.emplace(out_.name, displayName(*contractOf_));  // metodens namn i meddelanden
    }

    // STN-68: destruktorer översätts inte än (och därmed inte heller det som kör dem).
    if (const auto* d = dyn_cast<CXXDestructorDecl>(&fd_); d != nullptr && d->isUserProvided())
      return fail("destructor (not translated yet)");

    // SLV-39: en konstruktor är en metod på ett objekt som inte är giltigt än.
    const auto* ctor = dyn_cast<CXXConstructorDecl>(&fd_);

    // Signatur.
    std::string params;
    auto add = [&](const std::string& p) { params += (params.empty() ? "" : ", ") + p; };
    std::vector<std::string> harnessArgs;
    std::string harness;
    std::string traceInputs;
    if (const auto* md = dyn_cast<CXXMethodDecl>(&fd_); md != nullptr && md->isInstance()) {
      TRY(t, cType(ctx_.getCanonicalTagType(md->getParent())));
      self_ = md->isConst() ? Self::value : Self::pointer;
      add(t + (self_ == Self::pointer ? " *self" : " self"));
      harness += "  " + t + " self = " + nondet(t) + ";\n" +
                 (ctor ? "" : assumeValid(ctx_.getCanonicalTagType(md->getParent()), "self"));
      harnessArgs.push_back(self_ == Self::pointer ? "&self" : "self");
    }
    for (unsigned i = 0; i < fd_.getNumParams(); ++i) {
      const ParmVarDecl* p = fd_.getParamDecl(i);
      std::string name = varName(*p, i);
      QualType t = p->getType();
      bool pointer = false;
      if (const auto* ref = t->getAs<ReferenceType>()) {
        if (!isa<LValueReferenceType>(ref)) return fail("rvalue reference parameter");
        t = ref->getPointeeType();
        pointer = !t.isConstQualified();  // const T& blir ett värde
      }
      TRY(ct, cType(t));
      if (pointer) pointers_.insert(p);
      add(ct + (pointer ? " *" : " ") + name);
      std::string h = "pork_a" + std::to_string(i);
      if (const auto* spec = constSpanSpec(t)) {
        // En egen array med N element; size <= N eftersom spanen alltid kommer
        // från en vektor (Stone).
        QualType et = spanElement(*spec);
        TRY(ect, cType(et));
        // Arrayen i en omslagsstruct, så att den kan komma från en
        // nondet-funktion (en funktion kan inte returnera en array).
        std::string wrap = "struct pork_arr_" + mangled(ect) + "_" + bound();
        out_.decls.emplace_back(wrap, wrap + " {\n  " + ect + " v[" + bound() + "];\n};\n");
        std::string d = h + "__data";
        std::string k = "pork_k" + std::to_string(++loops_);
        std::string inner = assumeValid(et, d + ".v[" + k + "]");
        harness += "  " + wrap + " " + d + " = " + nondet(wrap) + ";\n";
        if (!inner.empty())
          harness += "  for (unsigned long " + k + " = 0; " + k + " < " + bound() + "; ++" + k + ") {\n" + inner + "  }\n";
        harness += "  " + ct + " " + h + " = " + nondet(ct) + ";\n  " + h + ".data = " + d + ".v;\n  __CPROVER_assume(" +
                   h + ".size <= " + bound() + ");\n";
        out_.assumptions.insert("std::vector size <= " + std::to_string(options_.vectorBound));
        harnessArgs.push_back(h);
        continue;
      }
      harness += "  " + ct + " " + h + " = " + nondet(ct) + ";\n" + assumeValid(t, h);
      if (options_.counterexample && (t->isIntegralOrEnumerationType() || t->isRealFloatingType())) {
        std::string label = p->getNameAsString();
        if (label.empty()) label = "argument " + std::to_string(i + 1);
        traceInputs += "  __CPROVER_input(" + quoted(label) + ", " + h + ");\n";
      }
      harnessArgs.push_back((pointer ? "&" : "") + h);
    }
    if (params.empty()) params = "void";

    // `const T&` (Stone: pekar in i *this eller en const&-parameter) returneras
    // som värde: inget kan ändra objektet medan anroparen använder resultatet.
    std::string ret = "void";
    if (!fd_.getReturnType()->isVoidType()) {
      TRY(r, returnCType());
      ret = r;
    }

    // Kropp: inledande PORK_PRE/PORK_POST och assert (förvillkor, som GNAT:s
    // pragma Precondition först i en kropp), sedan resten.
    struct Cond {
      std::string line, text, what;
    };
    std::vector<Cond> pre;
    const std::string shownName = contractOf_ ? displayName(*contractOf_) : displayName(fd_);
    const std::string precondition = "precondition of '" + shownName + "'";
    std::vector<const LambdaExpr*> posts;
    std::string rest;
    const auto* body = dyn_cast_or_null<CompoundStmt>(fd_.getBody());
    if (body == nullptr) return fail("function body is not a block");
    auto it = body->body_begin();
    for (; it != body->body_end(); ++it) {
      if (const Expr* cond = preCondition(*it)) {
        inContract_ = true;
        auto c = expr(cond);
        inContract_ = false;
        if (!c) return std::unexpected(c.error());
        pre.push_back({lineOf(cond->getBeginLoc()), *c, precondition});
      } else if (auto a = assertMacro(*it, sm_, ctx_.getLangOpts())) {
        inContract_ = true;
        auto c = expr(a->condition);
        inContract_ = false;
        if (!c) return std::unexpected(c.error());
        pre.push_back({lineOf((*it)->getBeginLoc()), *c, precondition + ": assert(" + a->text + ")"});
      } else if (disabledAssert(*it, sm_, ctx_.getLangOpts())) {
        // assert med NDEBUG: inget förvillkor, men inledningen fortsätter.
      } else if (const LambdaExpr* post = postCondition(*it)) {
        posts.push_back(post);
      } else if (flowRequirement(*it) != nullptr) {
        // Bronze-krav (BRZ-1): inget att översätta.
      } else {
        break;
      }
    }

    // Typinvarianter: förvillkor för parametrar och *this, eftervillkor för
    // T&-parametrar, *this i en icke-const-metod och resultatet.
    std::vector<Cond> invPost;
    auto invariant = [&](QualType t, const std::string& value, bool before, bool after) {
      const CXXMethodDecl* inv = invariantOf(t);
      if (inv == nullptr || invariantHelper(*inv, fd_)) return;
      out_.contractCallees.insert(usrOf(*inv));
      std::string call = cFunctionName(*inv) + "(" + value + ")";
      std::string type = inv->getParent()->getQualifiedNameAsString();
      if (before)
        pre.push_back({lineOf(fd_.getLocation()), call,
                       "invariant of '" + type + "' on entry to '" + displayName(fd_) + "'"});
      if (after) invPost.push_back({"", call, "invariant of '" + type + "' on exit from '" + displayName(fd_) + "'"});
    };
    if (const auto* md = dyn_cast<CXXMethodDecl>(&fd_); md != nullptr && md->isInstance())
      invariant(ctx_.getCanonicalTagType(md->getParent()), self_ == Self::pointer ? "*self" : "self", ctor == nullptr,
                self_ == Self::pointer);
    for (unsigned i = 0; i < fd_.getNumParams(); ++i) {
      const ParmVarDecl* p = fd_.getParamDecl(i);
      bool pointer = pointers_.contains(p);
      invariant(p->getType(), (pointer ? "*" : "") + varName(*p, i), true, pointer);
    }
    if (!fd_.getReturnType()->isVoidType() && !fd_.getReturnType()->isReferenceType() &&
        !fd_.getReturnType()->isPointerType())
      invariant(fd_.getReturnType(), "pork_result", false, true);

    // En override (eller en definierad virtuell metod) ärver gränssnittets
    // förvillkor: antas i beviset, kontrolleras vid direkta anrop.
    if (const auto* md = dyn_cast<CXXMethodDecl>(&fd_); md != nullptr && md->isVirtual() && contractOf_ == nullptr) {
      std::vector<const CXXMethodDecl*> todo{md};
      std::set<const CXXMethodDecl*> seen;
      while (!todo.empty()) {
        const CXXMethodDecl* u = todo.back();
        todo.pop_back();
        if (!seen.insert(u).second) continue;
        todo.insert(todo.end(), u->begin_overridden_methods(), u->end_overridden_methods());
        TRY(it, cType(ctx_.getCanonicalTagType(u->getParent())));
        std::string macro = interfaceContractName(*u) + "__inh";
        out_.inherits[macro] = usrOf(*u);
        std::string call = macro + "(" + (u->isConst() ? "((" + it + "){0})" : "&(" + it + "){0}");
        for (unsigned i = 0; i < fd_.getNumParams(); ++i) call += ", " + varName(*fd_.getParamDecl(i), i);
        pre.push_back({lineOf(fd_.getLocation()), call + ")", "precondition of the contract of '" + displayName(*u) + "'"});
      }
    }

    // Eftervillkoret i funktionen: värdeparametrar är ingångsvärdena (kopior
    // tas först), T& och *this slutvärdena; kontrolleras vid varje return.
    std::string old;
    if (!posts.empty()) {
      for (unsigned i = 0; i < fd_.getNumParams(); ++i) {
        const ParmVarDecl* p = fd_.getParamDecl(i);
        if (pointers_.contains(p) || p->getName().empty()) continue;
        TRY(ct, cType(p->getType().getNonReferenceType()));
        std::string copy = "pork_old_" + varName(*p, i);
        old += ct + " " + copy + " = " + varName(*p, i) + ";\n";
        overrides_[p] = copy;
      }
      TRY(check, postText(posts, "pork_result"));
      postCheck_ = "__CPROVER_assert(" + check + ", " + quoted("postcondition of '" + displayName(fd_) + "'") + ");";
      overrides_.clear();
    }
    for (const Cond& c : invPost)
      postCheck_ += (postCheck_.empty() ? "" : "\n") + std::string("__CPROVER_assert(") + c.text + ", " + quoted(c.what) + ");";
    if (ctor != nullptr) {
      TRY(inits, ctorInits(*ctor));
      rest += inits;
    }
    for (; it != body->body_end(); ++it) {
      TRY(s, stmt(*it));
      rest += s;
    }
    if (!postCheck_.empty() && fd_.getReturnType()->isVoidType())
      rest += lineOf(body->getRBracLoc()) + postCheck_ + "\n";  // slutet av en void-funktion
    rest = old + rest;
    postCheck_.clear();

    std::string where = lineOf(fd_.getLocation());
    auto define = [&](const std::string& name, bool assume) {
      std::string text = where + ret + " " + name + "(" + params + ")\n{\n";
      for (const Cond& c : pre)
        text += c.line + (assume ? "__CPROVER_assume(" + c.text + ");\n"
                                 : "__CPROVER_assert(" + c.text + ", " + quoted(c.what) + ");\n");
      return text + rest + "}\n";
    };
    out_.prototype = ret + " " + out_.name + "(" + params + ");\n";
    out_.definition = lambdaDefs_ + define(out_.name, false);

    // Kontraktsversionen (modulärt bevis): förvillkoret kontrolleras, sedan
    // godtyckliga nya värden för T&-parametrar och *this, och ett godtyckligt
    // resultat; allt antas giltigt (Silver-funktioner ger giltiga värden) och
    // uppfylla eftervillkoret.
    if (!posts.empty() || contractOf_ != nullptr) {
      std::string c = where + ret + " " + out_.name + "(" + params + ")\n{\n";
      for (const Cond& p : pre) c += p.line + "__CPROVER_assert(" + p.text + ", " + quoted(p.what) + ");\n";
      // *this och T&: nya värden; const& till en objekttyp: det dess
      // kontextreferenser pekar på (även en const-metod kan skriva genom dem).
      if (self_ != Self::none) {
        TRY(h, havocInto(ctx_.getCanonicalTagType(llvm::cast<CXXMethodDecl>(fd_).getParent()),
                         self_ == Self::pointer ? "(*self)" : "self", self_ == Self::pointer));
        c += h;
      }
      for (unsigned i = 0; i < fd_.getNumParams(); ++i) {
        const ParmVarDecl* p = fd_.getParamDecl(i);
        if (!p->getType()->isReferenceType()) continue;
        bool pointer = pointers_.contains(p);
        TRY(h, havocInto(p->getType().getNonReferenceType(), pointer ? "(*" + varName(*p, i) + ")" : varName(*p, i),
                         pointer));
        c += h;
      }
      if (!fd_.getReturnType()->isVoidType()) {
        c += "  " + ret + " pork_result;\n";
        if (!fd_.getReturnType()->isPointerType())
          c += assumeValid(fd_.getReturnType().getNonReferenceType(), "pork_result");
      }
      for (const Cond& p : invPost) c += "  __CPROVER_assume(" + p.text + ");\n";
      if (!posts.empty()) {
        TRY(post, postText(posts, "pork_result"));
        c += "  __CPROVER_assume(" + post + ");\n";
      }
      c += fd_.getReturnType()->isVoidType() ? "}\n" : "  return pork_result;\n}\n";
      out_.contract = c;
    }

    // Gränssnittskontrakt: villkoren som egna funktioner, för Liskov-bevisen.
    // Parametrarna är som i kontraktet; i eftervillkoret är värde- och
    // const&-parametrar ingångsvärdena, T& slutvärdena (anroparen skickar så).
    if (contractOf_ != nullptr) {
      std::string all;
      for (const Cond& c : pre) all += (all.empty() ? "" : " && ") + std::string("(") + c.text + ")";
      std::string preFn = "_Bool " + out_.name + "__pre(" + params + ")";
      std::string postParams = fd_.getReturnType()->isVoidType() ? params : ret + " pork_result, " + params;
      std::string postFn = "_Bool " + out_.name + "__post(" + postParams + ")";
      std::string postCond = "1";
      if (!posts.empty()) {
        TRY(pc, postText(posts, "pork_result"));
        postCond = pc;
      }
      out_.predicates = preFn + "\n{\n  return " + (all.empty() ? "1" : all) + ";\n}\n" + postFn + "\n{\n  return " +
                        postCond + ";\n}\n";
    }

    std::string args;
    for (const auto& a : harnessArgs) args += (args.empty() ? "" : ", ") + a;
    // Liskov: metoden (och det den överskuggar) mot gränssnittskontraktet.
    if (const auto* md = dyn_cast<CXXMethodDecl>(&fd_); md != nullptr && md->isVirtual() && contractOf_ == nullptr) {
      std::vector<const CXXMethodDecl*> todo{md};
      std::set<const CXXMethodDecl*> seen;
      while (!todo.empty()) {
        const CXXMethodDecl* u = todo.back();
        todo.pop_back();
        if (!seen.insert(u).second) continue;
        todo.insert(todo.end(), u->begin_overridden_methods(), u->end_overridden_methods());
        TRY(it, cType(ctx_.getCanonicalTagType(u->getParent())));
        std::string ic = interfaceContractName(*u);
        std::string selfArg = u->isConst() ? "pork_if" : "&pork_if";
        std::string rest;
        for (std::size_t k = 1; k < harnessArgs.size(); ++k) rest += ", " + harnessArgs[k];
        std::string text = "void pork_harness(void)\n{\n" + harness + "  " + it + " pork_if = {0};\n  __CPROVER_assume(" +
                           ic + "__pre(" + selfArg + rest + "));\n";
        std::string post;
        if (fd_.getReturnType()->isVoidType()) {
          text += "  " + out_.name + "(" + args + ");\n";
          post = ic + "__post(" + selfArg + rest + ")";
        } else {
          text += "  " + ret + " pork_lr = " + out_.name + "(" + args + ");\n";
          post = ic + "__post(pork_lr, " + selfArg + rest + ")";
        }
        text += lineOf(fd_.getLocation()) + "  __CPROVER_assert(" + post + ", " +
                quoted("postcondition of the contract of '" + displayName(*u) + "'") + ");\n}\n";
        out_.lsp[usrOf(*u)] = text;
      }
    }
    out_.entry = define(out_.name + "__entry", true) + "void pork_harness(void)\n{\n" + harness +
                 traceInputs + "  " + out_.name + "__entry(" + args + ");\n}\n";
    buildOracle(harness, harnessArgs, ret);
    // Självtestet behövs inte när de enda förvillkoren är ärvda (kärnan avgör
    // om kontraktet finns: CFunction::inheritsOnly).
    out_.inheritsOnly = std::ranges::all_of(pre, [](const Cond& c) { return c.text.find("__inh(") != std::string::npos; });
    if (!pre.empty()) {
      std::string sat = where + "void " + out_.name + "__sat(" + params + ")\n{\n";
      for (const Cond& c : pre) sat += c.line + "__CPROVER_assume(" + c.text + ");\n";
      sat += where + "__CPROVER_assert(0, " + quoted(kSatisfiableMarker) + ");\n}\n";
      out_.satisfiable = sat + "void pork_harness(void)\n{\n" + harness + "  " + out_.name + "__sat(" + args + ");\n}\n";
    }
    return out_;
  }

 private:
  enum class Self { none, value, pointer };

  // --- Beroendeoraklet (--flow-oracle) -----------------------------------------
  // Två körningar av samma funktion i ett program: körning b:s indata heter
  // pork_b<i> (och pork_bself) i stället för pork_a<i> (self). Löven namnges
  // som platserna i --flow (FlowBuild.cpp): parameternamn, "this",
  // "f@Klass" för fält, "[*]" för element.

  static std::string forAll(const std::string& k, const std::string& limit, const std::string& cond) {
    return "({ _Bool pork_ok = 1; for (unsigned long " + k + " = 0; " + k + " < " + limit + "; ++" + k + ") if (!(" +
           cond + ")) pork_ok = 0; pork_ok; })";
  }

  // Lika värde i hela värdet. compare: bara det som finns (k < size).
  std::optional<std::string> sameValue(QualType t, const std::string& a, const std::string& b, bool compare) {
    t = t.getCanonicalType().getUnqualifiedType();
    if (const auto* bt = t->getAs<BuiltinType>()) {
      if (bt->isFloatingPoint()) return "(" + a + " == " + b + " || (" + a + " != " + a + " && " + b + " != " + b + "))";
      return "(" + a + " == " + b + ")";
    }
    if (t->isEnumeralType()) return "(" + a + " == " + b + ")";
    const auto* rd = t->getAsCXXRecordDecl();
    if (rd == nullptr || !rd->hasDefinition()) return std::nullopt;
    if (const auto* spec = optionalSpec(rd)) {
      auto v = sameValue(optionalValue(*spec), a + ".v", b + ".v", compare);
      if (!v) return std::nullopt;
      return "(" + a + ".has == " + b + ".has && (!" + a + ".has || " + *v + "))";
    }
    if (const auto* spec = variantSpec(t)) {
      std::vector<QualType> alts = variantAlternatives(*spec);
      std::string out = "(" + a + ".index == " + b + ".index";
      for (std::size_t k = 0; k < alts.size(); ++k) {
        std::string u = ".u.a" + std::to_string(k);
        auto v = sameValue(alts[k], a + u, b + u, compare);
        if (!v) return std::nullopt;
        out += " && (" + a + ".index != " + std::to_string(k) + "ul || " + *v + ")";
      }
      return out + ")";
    }
    if (const auto* spec = sequenceSpec(rd)) {
      std::string k = "pork_q" + std::to_string(++loops_);
      auto v = sameValue(optionalValue(*spec), a + ".data[" + k + "]", b + ".data[" + k + "]", compare);
      if (!v) return std::nullopt;
      return "(" + a + ".size == " + b + ".size && " + forAll(k, compare ? a + ".size" : bound(), *v) + ")";
    }
    if (sm_.isInSystemHeader(rd->getLocation())) return std::nullopt;
    std::string out = "1";
    for (const FieldDecl* f : allFields(*rd->getDefinition())) {
      if (f->getType()->isReferenceType()) return std::nullopt;
      auto v = sameValue(f->getType(), a + "." + f->getNameAsString(), b + "." + f->getNameAsString(), compare);
      if (!v) return std::nullopt;
      out += " && " + *v;
    }
    return "(" + out + ")";
  }

  // Löven i ett värde: fält och element för sig, så att oraklet kan låta
  // just den delen variera.
  bool oracleLeaves(QualType t, const std::string& path, const std::string& a, const std::string& b, bool output,
                    std::vector<OracleLeaf>& out) {
    t = t.getCanonicalType().getUnqualifiedType();
    const auto* rd = t->getAsCXXRecordDecl();
    if (rd != nullptr && rd->hasDefinition() && optionalSpec(rd) == nullptr && variantSpec(t) == nullptr) {
      if (const auto* spec = sequenceSpec(rd)) {
        out.push_back({path, "(" + a + ".size == " + b + ".size)", "(" + a + ".size == " + b + ".size)", output});
        std::vector<OracleLeaf> inner;
        std::string k = "pork_q" + std::to_string(++loops_);
        if (!oracleLeaves(optionalValue(*spec), path + "[*]", a + ".data[" + k + "]", b + ".data[" + k + "]", output,
                          inner))
          return false;
        for (OracleLeaf& l : inner)
          out.push_back({l.path, forAll(k, bound(), l.assume), forAll(k, a + ".size", l.compare), output});
        return true;
      }
      if (sm_.isInSystemHeader(rd->getLocation())) return false;
      for (const FieldDecl* f : allFields(*rd->getDefinition())) {
        if (f->getType()->isReferenceType()) return false;
        std::string name = f->getNameAsString();
        if (!oracleLeaves(f->getType(), path + "." + name + "@" + f->getParent()->getQualifiedNameAsString(),
                          a + "." + name, b + "." + name, output, out))
          return false;
      }
      return true;
    }
    auto assume = sameValue(t, a, b, false);
    auto compare = sameValue(t, a, b, true);
    if (!assume || !compare) return false;
    out.push_back({path, *assume, *compare, output});
    return true;
  }

  static std::string renamedForB(const std::string& text) {
    static const std::regex selfName(R"(\bself\b)");
    static const std::regex inputName(R"(\bpork_a(\d))");
    return std::regex_replace(std::regex_replace(text, selfName, "pork_bself"), inputName, "pork_b$1");
  }

  void buildOracle(const std::string& harness, const std::vector<std::string>& harnessArgs, const std::string& ret) {
    if (harness.find("pork_r") != std::string::npos) {  // kontextobjekt (egna lokaler i harnessen)
      out_.oracleWhy = "context reference";
      return;
    }
    FlowOracleHarness h;
    std::size_t arg = 0;
    if (const auto* md = dyn_cast<CXXMethodDecl>(&fd_); md != nullptr && md->isInstance()) {
      if (isa<CXXConstructorDecl>(md)) {
        out_.oracleWhy = "constructor";
        return;
      }
      if (!oracleLeaves(ctx_.getCanonicalTagType(md->getParent()), "this", "self", "pork_bself", !md->isConst(),
                        h.leaves)) {
        out_.oracleWhy = "type of *this";
        return;
      }
      ++arg;
    }
    for (unsigned i = 0; i < fd_.getNumParams(); ++i) {
      const ParmVarDecl* p = fd_.getParamDecl(i);
      std::string path = p->getName().empty() ? "#param" + std::to_string(i) : p->getNameAsString();
      QualType t = p->getType();
      bool output = t->isReferenceType() && !t->getPointeeType().isConstQualified();
      std::string a = "pork_a" + std::to_string(i), b = "pork_b" + std::to_string(i);
      if (const auto* spec = constSpanSpec(t.getNonReferenceType())) {
        std::string k = "pork_q" + std::to_string(++loops_);
        auto v = sameValue(spanElement(*spec), a + ".data[" + k + "]", b + ".data[" + k + "]", false);
        if (!v) {
          out_.oracleWhy = "type of '" + path + "'";
          return;
        }
        std::string same = "(" + a + ".size == " + b + ".size && " + forAll(k, bound(), *v) + ")";
        h.leaves.push_back({path, same, same, false});
        continue;
      }
      if (!oracleLeaves(t.getNonReferenceType(), path, a, b, output, h.leaves)) {
        out_.oracleWhy = "type of '" + path + "'";
        return;
      }
    }
    (void)arg;
    if (!fd_.getReturnType()->isVoidType()) {
      std::vector<OracleLeaf> r;
      if (!oracleLeaves(fd_.getReturnType().getNonReferenceType(), "return", "pork_ra", "pork_rb", true, r)) {
        out_.oracleWhy = "return type";
        return;
      }
      h.leaves.insert(h.leaves.end(), r.begin(), r.end());
    }
    std::string args, argsB;
    for (const auto& x : harnessArgs) {
      args += (args.empty() ? "" : ", ") + x;
      argsB += (argsB.empty() ? "" : ", ") + renamedForB(x);
    }
    h.setup = harness + renamedForB(harness);
    std::string call = out_.name + "__entry(";
    if (ret == "void")
      h.run = "  " + call + args + ");\n  " + call + argsB + ");\n";
    else
      h.run = "  " + ret + " pork_ra = " + call + args + ");\n  " + ret + " pork_rb = " + call + argsB + ");\n";
    out_.oracle = std::move(h);
  }

  // --- Typer ---------------------------------------------------------------

  Text cType(QualType t) {
    t = t.getCanonicalType().getUnqualifiedType();
    if (const auto* bt = t->getAs<BuiltinType>()) {
      switch (bt->getKind()) {
        case BuiltinType::Bool: return "_Bool";
        case BuiltinType::Char_S:
        case BuiltinType::Char_U: return "char";
        case BuiltinType::SChar: return "signed char";
        case BuiltinType::UChar: return "unsigned char";
        case BuiltinType::Short: return "short";
        case BuiltinType::UShort: return "unsigned short";
        case BuiltinType::Int: return "int";
        case BuiltinType::UInt: return "unsigned int";
        case BuiltinType::Long: return "long";
        case BuiltinType::ULong: return "unsigned long";
        case BuiltinType::LongLong: return "long long";
        case BuiltinType::ULongLong: return "unsigned long long";
        case BuiltinType::Float: return "float";
        case BuiltinType::Double: return "double";
        case BuiltinType::LongDouble: return "long double";
        default: break;
      }
      return fail("type " + t.getAsString() + " is not supported");
    }
    const TagDecl* tag = t->getAsTagDecl();
    if (const auto* ed = dyn_cast_or_null<EnumDecl>(tag)) return cType(ed->getIntegerType());
    if (const auto* rd = dyn_cast_or_null<CXXRecordDecl>(tag)) return record(*rd);
    return fail("type " + t.getAsString() + " is not supported");
  }

  Text record(const CXXRecordDecl& decl) {
    const CXXRecordDecl* rd = decl.getDefinition();
    if (rd == nullptr) return fail("incomplete type " + recordName(decl));
    if (const auto* spec = optionalSpec(rd)) return optionalStruct(*spec);
    if (const auto* spec = sequenceSpec(rd)) return vectorStruct(*spec);
    if (const auto* spec = variantSpec(rd)) return variantStruct(*spec);
    if (const auto* spec = constSpanSpec(ctx_.getCanonicalTagType(rd))) return spanStruct(*spec);
    if (sm_.isInSystemHeader(rd->getLocation()))
      return fail("type " + recordName(*rd) + " (trusted type) is not supported yet");
    // SLV-40: i C är en kopia alltid bitvis. En egen kopierings-/flyttfunktion
    // (STN-72) körs i C++ vid varje kopia, också i ett yttre objekts implicita
    // kopiering; översättningen vore inte exakt (SLV-2).
    for (const CXXConstructorDecl* c : rd->ctors())
      if (c->isCopyOrMoveConstructor() && c->isUserProvided())
        return fail("type " + recordName(*rd) + " with a user-provided copy or move (not translated yet)");
    for (const CXXMethodDecl* m : rd->methods())
      if ((m->isCopyAssignmentOperator() || m->isMoveAssignmentOperator()) && m->isUserProvided())
        return fail("type " + recordName(*rd) + " with a user-provided copy or move (not translated yet)");
    std::string name = "struct s_" + ident(recordName(*rd)) + "_" + hash8(usrOf(*rd));
    if (recordsInProgress_.contains(name)) return fail("recursive type " + recordName(*rd) + " (not translated yet)");
    if (!types_.insert(name).second) return name;
    recordsInProgress_.insert(name);
    struct Leave {
      std::set<std::string>& s;
      std::string n;
      ~Leave() { s.erase(n); }
    } leave{recordsInProgress_, name};
    std::string fields;
    std::set<std::string> names;
    for (const FieldDecl* f : allFields(*rd)) {
      if (f->isBitField()) return fail("bit-field '" + f->getNameAsString() + "'");
      if (!names.insert(f->getNameAsString()).second)
        return fail("field '" + f->getNameAsString() + "' of " + recordName(*rd) + " hides a base class field");
      std::string ft;
      if (const auto* ref = f->getType()->getAs<LValueReferenceType>()) {
        // Referensfält: en kontextreferens blir en pekare (harnessen pekar på
        // ett eget objekt), en portreferens en pekare som aldrig används
        // (anrop genom den översätts inte, SLV-34).
        const CXXRecordDecl* target = ref->getPointeeType()->getAsCXXRecordDecl();
        if (target != nullptr && target->hasDefinition() && target->getDefinition()->isPolymorphic()) {
          ft = "void*";
        } else {
          TRY(pt, cType(ref->getPointeeType().getUnqualifiedType()));
          ft = pt + "*";
        }
      } else {
        TRY(t, cType(f->getType()));
        ft = t;
      }
      fields += "  " + ft + " " + f->getNameAsString() + ";\n";
    }
    if (fields.empty()) fields = "  char pork_empty;\n";
    out_.decls.emplace_back(name, name + " {\n" + fields + "};\n");
    return name;
  }

  // Fälten i en klass med baserna först (C-structen är platt: en metod i den
  // härledda klassen når basens fält som self->f).
  static std::vector<const FieldDecl*> allFields(const CXXRecordDecl& rd) {
    std::vector<const FieldDecl*> out;
    for (const CXXBaseSpecifier& b : rd.bases())
      if (const CXXRecordDecl* base = b.getType()->getAsCXXRecordDecl(); base != nullptr && base->hasDefinition())
        for (const FieldDecl* f : allFields(*base->getDefinition())) out.push_back(f);
    for (const FieldDecl* f : rd.fields()) out.push_back(f);
    return out;
  }

  // std::optional<T> -> struct { _Bool has; T v; } plus value_or-hjälpare.
  Text optionalStruct(const ClassTemplateSpecializationDecl& spec) {
    std::string name = "struct o_" + ident(recordName(spec)) + "_" + hash8(usrOf(spec));
    if (types_.contains(name)) return name;
    TRY(vt, cType(optionalValue(spec)));
    types_.insert(name);
    out_.decls.emplace_back(name, name + " {\n  _Bool has;\n  " + vt + " v;\n};\n");
    std::string helper = optionalHelper(name);
    out_.decls.emplace_back(helper + "__value_or",
                            "static " + vt + " " + helper + "__value_or(" + name + " o, " + vt +
                                " d) { return o.has ? o.v : d; }\n");
    for (const char* op : {"operator*", "value"})
      out_.decls.emplace_back(
          helper + "__" + ident(op),
          "static " + vt + " " + helper + "__" + ident(op) + "(" + name + " o)\n{\n  __CPROVER_assert(o.has, " +
              quoted(std::string("precondition of 'std::optional::") + op + "': has_value()") +
              ");\n  return o.v;\n}\n");
    return name;
  }

  // *o på en väg: ett lvalue med kontrollen vid anropet, så att fyndet får
  // anropets rad och skrivningar (*o = x, o->f = x) fungerar.
  static std::string optionalDeref(const std::string& o, const std::string& member) {
    return "(*(__CPROVER_assert(" + o + ".has, " +
           quoted("precondition of 'std::optional::" + member + "': has_value()") + "), &" + o + ".v))";
  }

  static std::string optionalHelper(const std::string& structName) {
    return ident(structName.substr(std::string("struct ").size()));
  }

  // Värde av typen T i en optional<T>: skalärer castas (int -> long), structar inte.
  Text optionalPayload(const Expr* arg, QualType valueType) {
    if (optionalSpec(arg->getType())) return fail("std::optional converted from another std::optional");
    TRY(x, expr(arg));
    if (valueType.getCanonicalType()->getAsCXXRecordDecl() != nullptr) return x;
    TRY(vt, cType(valueType));
    return "((" + vt + ")" + x + ")";
  }

  Text optionalConstruct(const CXXConstructExpr& c, const ClassTemplateSpecializationDecl& spec) {
    TRY(name, cType(c.getType()));
    if (c.getNumArgs() == 0 || isNullopt(c.getArg(0)->getType())) return "(" + name + "){0}";
    if (c.getNumArgs() != 1) return fail("constructor of " + recordName(spec));
    TRY(v, optionalPayload(c.getArg(0), optionalValue(spec)));
    return "(" + name + "){1, " + v + "}";
  }

  // Anrop till en medlem i std::optional, modellerat direkt.
  Text optionalCall(const CallExpr& c, const CXXMethodDecl& md, const Expr* object, unsigned first,
                    const ClassTemplateSpecializationDecl& spec) {
    TRY(name, cType(ctx_.getCanonicalTagType(&spec)));
    std::string helper = optionalHelper(name);
    auto objectValue = [&]() -> Text {
      if (isThis(object)) return fail("std::optional as 'this'");
      return expr(object);
    };
    std::string member = memberName(md);
    if (member == "operator bool" || member == "has_value") {
      TRY(o, objectValue());
      return o + ".has";
    }
    if (member == "operator*" || member == "value") {
      TRY(o, objectValue());
      if (!isPath(object)) return helper + "__" + ident(member) + "(" + o + ")";
      return optionalDeref(o, member);
    }
    if (member == "value_or" && c.getNumArgs() == first + 1) {
      TRY(o, objectValue());
      TRY(d, optionalPayload(c.getArg(first), optionalValue(spec)));
      return helper + "__value_or(" + o + ", " + d + ")";
    }
    if (member == "reset" && isPath(object)) {
      TRY(o, objectValue());
      return "(" + o + " = (" + name + "){0})";
    }
    if (member == "operator=" && c.getNumArgs() == first + 1 && isPath(object)) {
      TRY(o, objectValue());
      if (isNullopt(c.getArg(first)->getType())) return "(" + o + " = (" + name + "){0})";
      if (optionalSpec(c.getArg(first)->getType())) {  // optional = optional (samma typ)
        TRY(x, expr(c.getArg(first)));
        return "(" + o + " = " + x + ")";
      }
      TRY(v, optionalPayload(c.getArg(first), optionalValue(spec)));
      return "(" + o + " = (" + name + "){1, " + v + "})";
    }
    return fail("std::optional::" + member + " is not supported");
  }

  // --- std::vector<T> ---------------------------------------------------------

  std::string bound() const { return std::to_string(options_.vectorBound) + "ul"; }

  Text vectorStruct(const ClassTemplateSpecializationDecl& spec) {
    std::string name = "struct v_" + ident(recordName(spec)) + "_" + hash8(usrOf(spec));
    if (types_.contains(name)) return name;
    TRY(et, cType(optionalValue(spec)));  // första templateargumentet: elementtypen
    types_.insert(name);
    const std::string seq = sequenceName(spec);
    out_.assumptions.insert(seq + " size <= " + std::to_string(options_.vectorBound));
    out_.decls.emplace_back(name, name + " {\n  unsigned long size;\n  " + et + " data[" +
                                      std::to_string(options_.vectorBound) + "];\n};\n");
    // Hjälpare för objekt som inte är en väg (t.ex. ett returvärde).
    std::string h = optionalHelper(name);
    auto helper = [&](const std::string& op, const std::string& params, const std::string& cond,
                      const std::string& index) {
      out_.decls.emplace_back(
          h + "__" + ident(op),
          "static " + et + " " + h + "__" + ident(op) + "(" + name + " o" + params + ")\n{\n  __CPROVER_assert(" +
              cond + ", " + quoted(vectorMessage(seq, op)) + ");\n  return o.data[" + index + "];\n}\n");
    };
    helper("operator[]", ", unsigned long i", "i < o.size", "i");
    helper("at", ", unsigned long i", "i < o.size", "i");
    helper("front", "", "o.size > 0", "0");
    helper("back", "", "o.size > 0", "o.size - 1");
    // assign(n, x) utan kontroll (den står vid anropet, med C++-raden). Loopen
    // stannar vid N även om kontrollen fallerar (cbmc fortsätter efter en
    // assert), så data aldrig skrivs utanför.
    out_.decls.emplace_back(
        h + "__assign",
        "static void " + h + "__assign(" + name + "* o, unsigned long n, " + et + " x)\n{\n" +
            "  for (unsigned long i = 0; i < n && i < " + bound() + "; ++i)\n    o->data[i] = x;\n  o->size = n <= " +
            bound() + " ? n : " + bound() + ";\n}\n");
    // resize(n, x): nya element [size, n) får x. Samma gräns som assign.
    out_.decls.emplace_back(
        h + "__resize",
        "static void " + h + "__resize(" + name + "* o, unsigned long n, " + et + " x)\n{\n" +
            "  for (unsigned long i = o->size; i < n && i < " + bound() + "; ++i)\n    o->data[i] = x;\n  o->size = n <= " +
            bound() + " ? n : " + bound() + ";\n}\n");
    if (seq == "std::string") {
      // Lika: samma längd och samma tecken. append utan kontroll (den står
      // vid anropet, med C++-raden); loopen stannar vid N.
      out_.decls.emplace_back(
          h + "__eq", "static _Bool " + h + "__eq(" + name + " a, " + name + " b)\n{\n" +
                          "  if (a.size != b.size) return 0;\n  for (unsigned long i = 0; i < a.size && i < " + bound() +
                          "; ++i)\n    if (a.data[i] != b.data[i]) return 0;\n  return 1;\n}\n");
      out_.decls.emplace_back(
          h + "__append", "static void " + h + "__append(" + name + "* o, " + name + " x)\n{\n" +
                              "  for (unsigned long i = 0; i < x.size && o->size < " + bound() +
                              "; ++i)\n    o->data[o->size++] = x.data[i];\n}\n");
    }
    return name;
  }

  // "abc" som std::string: en sammansatt literal. Längre än N: fyndet att
  // strängen inte ryms, och bara de första N tecknen. Literalen når strängen
  // genom en const char*-parameter, så den slutar vid första NUL ("a\0b" är "a").
  Text stringLiteral(const ClassTemplateSpecializationDecl& spec, const StringLiteral& lit) {
    TRY(name, cType(ctx_.getCanonicalTagType(&spec)));
    llvm::StringRef bytes = lit.getBytes();
    bytes = bytes.take_until([](char ch) { return ch == '\0'; });
    std::size_t n = std::min<std::size_t>(bytes.size(), options_.vectorBound);
    std::string data;
    for (std::size_t i = 0; i < n; ++i)
      data += (i ? ", " : "") + std::to_string(static_cast<int>(static_cast<signed char>(bytes[i])));
    std::string value = "((" + name + "){" + std::to_string(n) + "ul, {" + (data.empty() ? "0" : data) + "}})";
    if (bytes.size() <= options_.vectorBound) return value;
    return "(__CPROVER_assert(0, " + quoted(vectorGrowMessage("std::string")) + "), " + value + ")";
  }

  // Ett argument som std::string: en sträng, eller en literal (till const char*).
  Text stringArg(const ClassTemplateSpecializationDecl& spec, const Expr* e) {
    if (const auto* lit = dyn_cast<StringLiteral>(e->IgnoreParenImpCasts())) return stringLiteral(spec, *lit);
    if (stringSpec(e->getType()) == nullptr) return fail("std::string operand of type " + e->getType().getAsString());
    return expr(e);
  }

  // o += x: längden kontrolleras vid anropet (C++-raden).
  Text stringAppend(const ClassTemplateSpecializationDecl& spec, const std::string& o, const std::string& x) {
    TRY(name, cType(ctx_.getCanonicalTagType(&spec)));
    std::string h = optionalHelper(name);
    return "({ " + name + " pork_sx = " + x + "; __CPROVER_assert(" + o + ".size + pork_sx.size <= " + bound() +
           ", " + quoted(vectorGrowMessage("std::string")) + "); " + h + "__append(&" + o + ", pork_sx); })";
  }

  Text stringCall(const CallExpr& c, const CXXMethodDecl& md, const Expr* object, unsigned first,
                  const ClassTemplateSpecializationDecl& spec) {
    std::string member = memberName(md);
    unsigned nargs = c.getNumArgs() - first;
    if (member == "length" && nargs == 0) {
      TRY(o, expr(object));
      return o + ".size";
    }
    if ((member == "operator=" || member == "operator+=") && nargs == 1) {
      const Expr* a = c.getArg(first);
      bool isString = stringSpec(a->getType()) != nullptr;
      if (member == "operator=" && isString) return vectorCall(c, md, object, first, spec);
      if (!isPath(object)) return fail("std::string::" + member + " on a temporary");
      TRY(o, expr(object));
      if (member == "operator+=" && a->getType()->isAnyCharacterType()) {  // s += 'c': push_back
        TRY(x, expr(a));
        return "(__CPROVER_assert(" + o + ".size < " + bound() + ", " + quoted(vectorGrowMessage("std::string")) +
               "), " + o + ".data[" + o + ".size] = " + x + ", " + o + ".size = " + o + ".size + 1)";
      }
      TRY(x, stringArg(spec, a));
      if (member == "operator=") return "(" + o + " = " + x + ")";
      return stringAppend(spec, o, x);
    }
    return vectorCall(c, md, object, first, spec);
  }

  // Fria funktioner på std::string: ==, != och +.
  std::optional<Text> stringFunction(const CallExpr& c) {
    const auto* oc = dyn_cast<CXXOperatorCallExpr>(&c);
    if (oc == nullptr || c.getNumArgs() != 2) return std::nullopt;
    const Expr* a = c.getArg(0);
    const Expr* b = c.getArg(1);
    const ClassTemplateSpecializationDecl* spec = stringSpec(a->getType());
    if (spec == nullptr) spec = stringSpec(b->getType());
    if (spec == nullptr) return std::nullopt;
    OverloadedOperatorKind op = oc->getOperator();
    if (op != OO_EqualEqual && op != OO_ExclaimEqual && op != OO_Plus) return std::nullopt;
    auto name = cType(ctx_.getCanonicalTagType(spec));
    if (!name) return Text(std::unexpected(name.error()));
    auto x = stringArg(*spec, a);
    if (!x) return x;
    auto y = stringArg(*spec, b);
    if (!y) return y;
    if (op == OO_Plus) {
      auto app = stringAppend(*spec, "pork_sa", *y);
      if (!app) return app;
      return Text("({ " + *name + " pork_sa = " + *x + "; " + *app + "; pork_sa; })");
    }
    return Text(std::string(op == OO_ExclaimEqual ? "!" : "") + optionalHelper(*name) + "__eq(" + *x + ", " + *y + ")");
  }

  // Värdeinitiering T{} (resize(n)): fältinitierare, annars värdeinitierade fält.
  Text valueInit(QualType t) {
    t = t.getCanonicalType().getUnqualifiedType();
    if (optionalSpec(t) || sequenceSpec(t)) return zero(t);
    if (const auto* spec = variantSpec(t)) return variantDefault(*spec);
    const CXXRecordDecl* rd = t->getAsCXXRecordDecl();
    if (rd == nullptr) return zero(t);
    rd = rd->getDefinition();
    if (rd == nullptr) return fail("incomplete type " + t.getAsString());
    if (sm_.isInSystemHeader(rd->getLocation())) return fail("value of " + recordName(*rd));
    if (rd->hasUserProvidedDefaultConstructor()) return fail("user-provided constructor of " + recordName(*rd));
    TRY(ct, cType(t));
    std::string parts;
    for (const FieldDecl* f : rd->fields()) {
      std::string v;
      if (f->hasInClassInitializer()) {
        TRY(x, expr(f->getInClassInitializer()));
        v = x;
      } else {
        TRY(x, valueInit(f->getType()));
        v = x;
      }
      parts += (parts.empty() ? "" : ", ") + v;
    }
    if (parts.empty()) return "(" + ct + "){0}";
    return "(" + ct + "){" + parts + "}";
  }

  // std::span<const T>: { size, const T* data }. Namnet från spanens USR, så
  // att anropare och anropad i olika TU:er får samma typ.
  Text spanStruct(const ClassTemplateSpecializationDecl& spec) {
    std::string name = "struct p_" + ident(recordName(spec)) + "_" + hash8(usrOf(spec));
    if (types_.contains(name)) return name;
    TRY(et, cType(spanElement(spec)));
    types_.insert(name);
    out_.decls.emplace_back(name, name + " {\n  unsigned long size;\n  const " + et + " *data;\n};\n");
    return name;
  }

  static std::string spanMessage(const std::string& op) {
    if (op == "front" || op == "back") return "precondition of 'std::span::" + op + "': !empty()";
    return "precondition of 'std::span::" + op + "': index < size()";
  }

  // Metoder på en span-parameter (alltid en väg).
  Text spanCall(const CallExpr& c, const CXXMethodDecl& md, const Expr* object, unsigned first) {
    std::string member = memberName(md);
    if (spanParam(object) == nullptr) return fail("std::span::" + member + " on something else than a parameter");
    TRY(o, expr(object));
    unsigned nargs = c.getNumArgs() - first;
    if (member == "size" && nargs == 0) return o + ".size";
    if (member == "empty" && nargs == 0) return "(" + o + ".size == 0)";
    std::string cond, slot;
    if ((member == "front" || member == "back") && nargs == 0) {
      cond = o + ".size > 0";
      slot = member == "front" ? "0" : o + ".size - 1";
    } else if (member == "operator[]" && nargs == 1) {
      TRY(i, expr(c.getArg(first)));
      cond = "(" + i + ") < " + o + ".size";
      slot = "(" + i + ")";
    } else {
      return fail("std::span::" + member + " is not supported");
    }
    return "(*(__CPROVER_assert(" + cond + ", " + quoted(spanMessage(member)) + "), &" + o + ".data[" + slot + "]))";
  }

  // span<const T> skapad vid ett anrop: från en vektor (som är en väg) pekar
  // den in i vektorns data, från en annan span-parameter är den en kopia.
  Text spanFrom(QualType spanType, const Expr* src) {
    if (spanParam(src) != nullptr) return expr(src);
    if (!isPath(src)) return fail("std::span from a temporary std::vector");
    TRY(t, cType(spanType));
    TRY(v, expr(src));
    return "((" + t + "){" + v + ".size, " + v + ".data})";
  }

  std::string vectorGrowMessage(const std::string& seq = "std::vector") const {
    return seq + " may grow beyond " + std::to_string(options_.vectorBound) + " elements (raise --vector-bound)";
  }

  static std::string vectorMessage(const std::string& seq, const std::string& op) {
    if (op == "front" || op == "back") return "precondition of '" + seq + "::" + op + "': !empty()";
    return "precondition of '" + seq + "::" + op + "': index < size()";
  }

  // Elementets uttryck (o, i) -> (kontroll, o.data[i]) för v[i], at, front, back.
  // `object` är objektets C-text, `path` om den får utvärderas två gånger.
  Text vectorElement(const std::string& seq, const std::string& member, const std::string& o, bool path,
                     const std::string& index, const std::string& helper) {
    std::string cond, slot;
    if (member == "front" || member == "back") {
      cond = o + ".size > 0";
      slot = member == "front" ? "0" : o + ".size - 1";
    } else {
      cond = index + " < " + o + ".size";
      slot = index;
    }
    if (!path) return helper + "__" + ident(member) + "(" + o + (index.empty() ? "" : ", " + index) + ")";
    return "(*(__CPROVER_assert(" + cond + ", " + quoted(vectorMessage(seq, member)) + "), &" + o + ".data[" + slot + "]))";
  }

  Text vectorCall(const CallExpr& c, const CXXMethodDecl& md, const Expr* object, unsigned first,
                  const ClassTemplateSpecializationDecl& spec) {
    TRY(name, cType(ctx_.getCanonicalTagType(&spec)));
    std::string h = optionalHelper(name);
    std::string member = memberName(md);
    const std::string seq = sequenceName(spec);
    if (isThis(object)) return fail(seq + " as 'this'");
    TRY(o, expr(object));
    bool path = isPath(object);
    unsigned nargs = c.getNumArgs() - first;
    if (member == "size" && nargs == 0) return o + ".size";
    if (member == "empty" && nargs == 0) return "(" + o + ".size == 0)";
    if ((member == "front" || member == "back") && nargs == 0) return vectorElement(seq, member, o, path, "", h);
    if ((member == "operator[]" || member == "at") && nargs == 1) {
      TRY(i, expr(c.getArg(first)));
      return vectorElement(seq, member, o, path, "(" + i + ")", h);
    }
    if (!path) return fail(seq + "::" + member + " on a temporary");
    if (member == "push_back" && nargs == 1) {
      TRY(x, optionalPayload(c.getArg(first), optionalValue(spec)));
      return "(__CPROVER_assert(" + o + ".size < " + bound() + ", " + quoted(vectorGrowMessage(seq)) + "), " + o +
             ".data[" + o + ".size] = " + x + ", " + o + ".size = " + o + ".size + 1)";
    }
    // assign(antal, värde); inte iterator- eller initieringslistvarianterna.
    if (member == "assign" && nargs == 2 && c.getArg(first)->getType()->isIntegerType()) {
      TRY(n, expr(c.getArg(first)));
      TRY(x, optionalPayload(c.getArg(first + 1), optionalValue(spec)));
      std::string count = "(unsigned long)(" + n + ")";
      return "(__CPROVER_assert(" + count + " <= " + bound() + ", " + quoted(vectorGrowMessage(seq)) + "), " + h +
             "__assign(&" + o + ", " + count + ", " + x + "))";
    }
    // resize(n) och resize(n, x).
    if (member == "resize" && (nargs == 1 || nargs == 2)) {
      TRY(n, expr(c.getArg(first)));
      std::string x;
      if (nargs == 2) {
        TRY(v, optionalPayload(c.getArg(first + 1), optionalValue(spec)));
        x = v;
      } else {
        TRY(v, valueInit(optionalValue(spec)));
        x = v;
      }
      std::string count = "(unsigned long)(" + n + ")";
      return "(__CPROVER_assert(" + count + " <= " + bound() + ", " + quoted(vectorGrowMessage(seq)) + "), " + h +
             "__resize(&" + o + ", " + count + ", " + x + "))";
    }
    if (member == "pop_back" && nargs == 0)
      return "(__CPROVER_assert(" + o + ".size > 0, " + quoted("precondition of '" + seq + "::pop_back': !empty()") + "), " +
             o + ".size = " + o + ".size - 1)";
    if (member == "clear" && nargs == 0) return "(" + o + ".size = 0)";
    if (member == "operator=" && nargs == 1 && sequenceSpec(c.getArg(first)->getType())) {
      TRY(x, expr(c.getArg(first)));
      return "(" + o + " = " + x + ")";
    }
    return fail(seq + "::" + member + " is not supported");
  }

  // PORK_POST-villkoren som ett C-uttryck, med resultatet som `result`.
  // overrides_ avgör vad parametrarna betyder (ingångsvärden i funktionen).
  Text postText(const std::vector<const LambdaExpr*>& posts, const std::string& result) {
    std::string all;
    for (const LambdaExpr* le : posts) {
      const CXXMethodDecl* op = le->getCallOperator();
      if (op->getNumParams() == 1) overrides_[op->getParamDecl(0)] = result;
      const Expr* cond = postConditionExpr(*le);
      if (cond == nullptr) return fail("unsupported PORK_POST");
      inContract_ = true;
      auto translated = expr(cond);
      inContract_ = false;
      TRY(c, std::move(translated));
      all += (all.empty() ? "" : " && ") + std::string("(") + c + ")";
    }
    return all;
  }

  // --- std::variant<A, B, ...> -------------------------------------------------

  Text variantStruct(const ClassTemplateSpecializationDecl& spec) {
    std::string name = "struct x_" + ident(recordName(spec)) + "_" + hash8(usrOf(spec));
    if (types_.contains(name)) return name;
    std::string members;
    std::vector<QualType> alts = variantAlternatives(spec);
    for (std::size_t k = 0; k < alts.size(); ++k) {
      TRY(at, cType(alts[k]));
      members += "    " + at + " a" + std::to_string(k) + ";\n";
    }
    types_.insert(name);
    out_.decls.emplace_back(name, name + " {\n  unsigned long index;\n  union {\n" + members + "  } u;\n};\n");
    return name;
  }

  // Alternativets nummer, eller fel om typen inte finns exakt en gång.
  std::expected<std::size_t, std::string> altIndex(const ClassTemplateSpecializationDecl& spec, QualType t) {
    std::vector<QualType> alts = variantAlternatives(spec);
    std::optional<std::size_t> found;
    for (std::size_t k = 0; k < alts.size(); ++k)
      if (ctx_.hasSameUnqualifiedType(alts[k], t.getNonReferenceType())) {
        if (found) return fail("std::variant with " + t.getAsString() + " more than once");
        found = k;
      }
    if (!found) return fail(t.getAsString() + " is not an alternative of " + recordName(spec));
    return *found;
  }

  Text variantOf(const ClassTemplateSpecializationDecl& spec, std::size_t k, const std::string& value) {
    TRY(name, cType(ctx_.getCanonicalTagType(&spec)));
    return "(" + name + "){" + std::to_string(k) + "ul, {.a" + std::to_string(k) + " = " + value + "}}";
  }

  // Standardvärdet: första alternativet, värdeinitierat.
  Text variantDefault(const ClassTemplateSpecializationDecl& spec) {
    QualType a0 = variantAlternatives(spec).front();
    std::string v;
    if (const CXXRecordDecl* rd = a0->getAsCXXRecordDecl(); rd != nullptr && rd->hasDefinition() &&
                                                             !sm_.isInSystemHeader(rd->getLocation())) {
      TRY(x, defaultValue(*rd->getDefinition()));
      v = x;
    } else {
      TRY(x, zero(a0));
      v = x;
    }
    return variantOf(spec, 0, v);
  }

  // Alternativ k i v (en väg): ett lvalue med kontroll att k är aktivt.
  static std::string variantAlt(const std::string& v, std::size_t k, const std::string& what) {
    return "(*(__CPROVER_assert(" + v + ".index == " + std::to_string(k) + "ul, " +
           quoted("precondition of '" + what + "': holds_alternative") + "), &" + v + ".u.a" + std::to_string(k) + "))";
  }

  // Fria funktioner för std::variant: holds_alternative, get, visit.
  std::optional<Text> variantFunction(const CallExpr& c, const FunctionDecl& callee) {
    std::string name = callee.getQualifiedNameAsString();
    if (name == "std::visit") return visit(c);
    if (name != "std::holds_alternative" && name != "std::get") return std::nullopt;
    if (c.getNumArgs() != 1) return std::nullopt;
    const Expr* object = c.getArg(0);
    const auto* spec = variantSpec(object->getType());
    if (spec == nullptr) return std::nullopt;
    QualType alt;
    if (name == "std::holds_alternative") {
      const TemplateArgumentList* targs = callee.getTemplateSpecializationArgs();
      if (targs == nullptr || targs->size() < 1 || targs->get(0).getKind() != TemplateArgument::Type)
        return fail("std::holds_alternative without a type");
      alt = targs->get(0).getAsType();
    } else {
      alt = callee.getReturnType().getNonReferenceType();
    }
    auto k = altIndex(*spec, alt);
    if (!k) return std::unexpected(k.error());
    TRY(v, expr(object));
    if (name == "std::holds_alternative") return Text("(" + v + ".index == " + std::to_string(*k) + "ul)");
    if (!isPath(object)) return fail("std::get on a temporary");
    return Text(variantAlt(v, *k, "std::get"));
  }

  Text variantCall(const CallExpr& c, const CXXMethodDecl& md, const Expr* object, unsigned first,
                   const ClassTemplateSpecializationDecl& spec) {
    std::string member = memberName(md);
    if (isThis(object)) return fail("std::variant as 'this'");
    TRY(o, expr(object));
    unsigned nargs = c.getNumArgs() - first;
    if (member == "index" && nargs == 0) return o + ".index";
    if (member == "operator=" && nargs == 1 && isPath(object)) {
      const Expr* arg = c.getArg(first);
      TRY(x, expr(arg));
      if (variantSpec(arg->getType())) return "(" + o + " = " + x + ")";
      auto k = altIndex(spec, arg->getType());
      if (!k) return std::unexpected(k.error());
      TRY(v, variantOf(spec, *k, x));
      return "(" + o + " = " + v + ")";
    }
    return fail("std::variant::" + member + " is not supported");
  }

  // o == x, o != x, o == p för std::optional (fria operatorer i <optional>).
  std::optional<Text> optionalCompare(const CallExpr& c, const FunctionDecl& callee) {
    const auto* oc = dyn_cast<CXXOperatorCallExpr>(&c);
    if (oc == nullptr || c.getNumArgs() != 2) return std::nullopt;
    OverloadedOperatorKind op = oc->getOperator();
    if (op != OO_EqualEqual && op != OO_ExclaimEqual) return std::nullopt;
    const Expr* a = c.getArg(0);
    const Expr* b = c.getArg(1);
    bool ao = optionalSpec(a->getType()) != nullptr, bo = optionalSpec(b->getType()) != nullptr;
    if (!ao && !bo) return std::nullopt;
    if (isNullopt(a->getType()) || isNullopt(b->getType())) {
      const Expr* o = ao ? a : b;
      TRY(x, expr(o));
      return Text(op == OO_EqualEqual ? "(!" + x + ".has)" : x + ".has");
    }
    TRY(x, expr(a));
    TRY(y, expr(b));
    std::string eq;
    if (ao && bo) {
      if (optionalValue(*optionalSpec(a->getType()))->getAsCXXRecordDecl() != nullptr)
        return fail("std::optional comparison of structs");
      eq = "(" + x + ".has == " + y + ".has && (!" + x + ".has || " + x + ".v == " + y + ".v))";
    } else if (ao) {
      eq = "(" + x + ".has && " + x + ".v == " + y + ")";
    } else {
      eq = "(" + y + ".has && " + y + ".v == " + x + ")";
    }
    (void)callee;
    return Text(op == OO_EqualEqual ? eq : "(!" + eq + ")");
  }

  // --- Lambdor ---------------------------------------------------------------

  // En instans av operator() blir en C-funktion: fångster med referens som
  // pekare, kopior som värden, sedan parametrarna.
  Text lambdaFunction(const CXXMethodDecl& op, const LambdaExpr& le) {
    if (auto it = lambdaNames_.find(&op); it != lambdaNames_.end()) return it->second;
    std::string name = out_.name + "__l" + std::to_string(lambdaNames_.size());
    lambdaNames_[&op] = name;

    auto savedPointers = pointers_;
    auto savedNullables = nullables_;
    Self savedSelf = self_;
    std::string savedPost = std::exchange(postCheck_, std::string());
    pointers_.clear();
    nullables_.clear();
    self_ = Self::none;
    auto restore = [&] {
      pointers_ = savedPointers;
      nullables_ = savedNullables;
      self_ = savedSelf;
      postCheck_ = savedPost;
    };

    auto build = [&]() -> Text {
      std::string params;
      auto add = [&](const std::string& p) { params += (params.empty() ? "" : ", ") + p; };
      for (const LambdaCapture& cap : le.captures()) {
        const auto* vd = llvm::cast<VarDecl>(cap.getCapturedVar());
        TRY(ct, cType(vd->getType().getNonReferenceType()));
        bool byRef = cap.getCaptureKind() == LCK_ByRef;
        if (byRef) pointers_.insert(vd);
        add(ct + (byRef ? " *" : " ") + varName(*vd));
      }
      for (unsigned i = 0; i < op.getNumParams(); ++i) {
        const ParmVarDecl* p = op.getParamDecl(i);
        QualType t = p->getType();
        bool pointer = false;
        if (const auto* ref = t->getAs<ReferenceType>()) {
          t = ref->getPointeeType();
          pointer = !t.isConstQualified();
        }
        TRY(ct, cType(t));
        if (pointer) pointers_.insert(p);
        add(ct + (pointer ? " *" : " ") + varName(*p, i));
      }
      std::string ret = "void";
      if (!op.getReturnType()->isVoidType()) {
        TRY(r, cType(op.getReturnType()));
        ret = r;
      }
      const auto* body = dyn_cast_or_null<CompoundStmt>(op.getBody());
      if (body == nullptr) return fail("lambda body is not a block");
      for (const Stmt* s : body->body())
        if (preCondition(s) != nullptr) return fail("PORK_PRE in a lambda");
      TRY(b, stmt(body));
      lambdaDefs_ += lineOf(le.getBeginLoc()) + "static " + ret + " " + name + "(" + (params.empty() ? "void" : params) +
                     ")\n" + b;
      return name;
    };
    Text r = build();
    restore();
    return r;
  }

  // Argument för fångsterna vid ett anrop. `snapshot`: kopiorna som togs när
  // lambdavariabeln skapades (namn), annars värdet nu.
  std::string captureArgs(const LambdaExpr& le, const VarDecl* lambdaVar) {
    std::string args;
    for (const LambdaCapture& cap : le.captures()) {
      const auto* vd = llvm::cast<VarDecl>(cap.getCapturedVar());
      std::string n = varName(*vd);
      std::string a;
      if (cap.getCaptureKind() == LCK_ByRef)
        a = pointers_.contains(vd) ? n : "&" + n;
      else if (lambdaVar != nullptr)
        a = varName(*lambdaVar) + "__" + n;
      else
        a = pointers_.contains(vd) ? "(*" + n + ")" : n;
      args += (args.empty() ? "" : ", ") + a;
    }
    return args;
  }

  Text lambdaCall(const CXXMethodDecl& op, const LambdaExpr& le, const VarDecl* lambdaVar,
                  const std::vector<std::string>& args) {
    TRY(name, lambdaFunction(op, le));
    std::string all = captureArgs(le, lambdaVar);
    for (const std::string& a : args) all += (all.empty() ? "" : ", ") + a;
    return name + "(" + all + ")";
  }

  // Ett argument till en lambda/funktion: adress för T&, annars värdet.
  Text argument(const FunctionDecl& callee, unsigned i, const Expr* arg) {
    const auto* ref = i < callee.getNumParams() ? callee.getParamDecl(i)->getType()->getAs<ReferenceType>() : nullptr;
    if (ref != nullptr && !ref->getPointeeType().isConstQualified()) return address(arg);
    return expr(arg);
  }

  // std::visit(f, v) -> (v.index == 0 ? f0(..., v.u.a0) : f1(..., v.u.a1)).
  Text visit(const CallExpr& c) {
    if (c.getNumArgs() != 2) return fail("std::visit with more than one variant");
    const Expr* visitor = c.getArg(0)->IgnoreImplicit();
    if (const auto* m = dyn_cast<MaterializeTemporaryExpr>(visitor)) visitor = m->getSubExpr()->IgnoreImplicit();
    const LambdaExpr* le = dyn_cast<LambdaExpr>(visitor);
    const VarDecl* var = nullptr;
    if (le == nullptr) {
      const auto* dr = dyn_cast<DeclRefExpr>(visitor->IgnoreParenImpCasts());
      var = dr ? dyn_cast<VarDecl>(dr->getDecl()) : nullptr;
      if (var == nullptr || !lambdaVars_.contains(var)) return fail("std::visit needs a lambda");
      le = lambdaVars_.at(var);
    }
    const Expr* object = c.getArg(1);
    const auto* spec = variantSpec(object->getType());
    if (spec == nullptr) return fail("std::visit over a non-variant");
    if (!isPath(object)) return fail("std::visit over a temporary");
    TRY(v, expr(object));
    std::vector<QualType> alts = variantAlternatives(*spec);
    std::string out;
    for (std::size_t k = alts.size(); k-- > 0;) {
      const CXXMethodDecl* op = callOperatorFor(*le, alts[k]);
      if (op == nullptr) return fail("no lambda call operator for " + alts[k].getAsString());
      const auto* ref = op->getParamDecl(0)->getType()->getAs<ReferenceType>();
      std::string slot = v + ".u.a" + std::to_string(k);
      std::string arg = ref != nullptr && !ref->getPointeeType().isConstQualified() ? "&" + slot : slot;
      TRY(call, lambdaCall(*op, *le, var, {arg}));
      out = out.empty() ? call : "(" + v + ".index == " + std::to_string(k) + "ul ? " + call + " : " + out + ")";
    }
    return out;
  }

  Text zero(QualType t) {
    TRY(ct, cType(t));
    return t.getCanonicalType()->getAsCXXRecordDecl() != nullptr ? "(" + ct + "){0}" : "((" + ct + ")0)";
  }

  // Antaganden om godtyckliga indata: flyttal ändliga, enums inom sitt intervall.
  std::string assumeValid(QualType t, const std::string& path) {
    t = t.getCanonicalType();
    if (const auto* bt = t->getAs<BuiltinType>()) {
      const char* sfx = bt->getKind() == BuiltinType::Float    ? "f"
                        : bt->getKind() == BuiltinType::Double ? "d"
                        : bt->getKind() == BuiltinType::LongDouble ? "ld"
                                                                    : nullptr;
      if (sfx == nullptr) return "";
      return std::string("  __CPROVER_assume(!__CPROVER_isnan") + sfx + "(" + path +
             ") && !__CPROVER_isinf" + sfx + "(" + path + "));\n";
    }
    const TagDecl* tag = t->getAsTagDecl();
    // En enum kan ha andra värden än de uppräknade (`static_cast<E>(7)`), också
    // från Silver-funktioner: med fast typ alla värden av den underliggande
    // typen, annars intervallet ([dcl.enum]/8).
    if (const auto* ed = dyn_cast_or_null<EnumDecl>(tag)) {
      const EnumDecl* def = ed->getDefinition();
      if (def == nullptr || def->isFixed()) return "";
      auto range = enumRange(*def);
      if (!range) return "";
      return "  __CPROVER_assume((long long)" + path + " >= " + std::to_string(range->first) + "LL && (long long)" +
             path + " < " + std::to_string(range->second) + "LL);\n";
    }
    if (const auto* spec = optionalSpec(dyn_cast_or_null<CXXRecordDecl>(tag)))
      return assumeValid(optionalValue(*spec), path + ".v");
    if (const auto* spec = variantSpec(dyn_cast_or_null<CXXRecordDecl>(tag))) {
      std::vector<QualType> alts = variantAlternatives(*spec);
      std::string out = "  __CPROVER_assume(" + path + ".index < " + std::to_string(alts.size()) + "ul);\n";
      for (std::size_t k = 0; k < alts.size(); ++k) {
        std::string inner = assumeValid(alts[k], path + ".u.a" + std::to_string(k));
        if (!inner.empty()) out += "  if (" + path + ".index == " + std::to_string(k) + "ul) {\n" + inner + "  }\n";
      }
      return out;
    }
    if (const auto* spec = sequenceSpec(dyn_cast_or_null<CXXRecordDecl>(tag))) {
      std::string k = "pork_k" + std::to_string(++loops_);
      std::string inner = assumeValid(optionalValue(*spec), path + ".data[" + k + "]");
      std::string out = "  __CPROVER_assume(" + path + ".size <= " + bound() + ");\n";
      if (!inner.empty())
        out += "  for (unsigned long " + k + " = 0; " + k + " < " + bound() + "; ++" + k + ") {\n" + inner + "  }\n";
      return out;
    }
    std::string out;
    if (const auto* rd = dyn_cast_or_null<CXXRecordDecl>(tag); rd != nullptr && rd->getDefinition())
      for (const FieldDecl* f : allFields(*rd->getDefinition())) {
        std::string field = path + "." + f->getNameAsString();
        if (const auto* ref = f->getType()->getAs<LValueReferenceType>()) {
          // Kontextreferens: ett eget objekt med godtyckligt innehåll. Stone
          // garanterar att det inte är samma som något annat argument.
          const CXXRecordDecl* target = ref->getPointeeType()->getAsCXXRecordDecl();
          if (target != nullptr && target->hasDefinition() && target->getDefinition()->isPolymorphic()) continue;
          QualType pt = ref->getPointeeType().getUnqualifiedType();
          auto ct = cType(pt);
          if (!ct) continue;  // felet står redan i out_ via fail
          std::string obj = "pork_r" + std::to_string(++loops_);
          out += "  " + *ct + " " + obj + " = " + nondet(*ct) + ";\n" + assumeValid(pt, obj) + "  " + field + " = &" + obj +
                 ";\n";
          continue;
        }
        out += assumeValid(f->getType(), field);
      }
    return out;
  }

  // --- Litteraler -----------------------------------------------------------

  Text intLiteral(const llvm::APSInt& v, QualType t) {
    t = t.getCanonicalType();
    if (const auto* ed = dyn_cast_or_null<EnumDecl>(t->getAsTagDecl())) t = ed->getIntegerType();
    const auto* bt = t->getAs<BuiltinType>();
    if (bt == nullptr) return fail("integer constant of type " + t.getAsString());
    if (bt->getKind() == BuiltinType::Bool) return std::string(v.getBoolValue() ? "1" : "0");
    std::string digits;
    if (v.isSigned() && v.isMinSignedValue() && v.getBitWidth() > 1) {
      llvm::APSInt one(llvm::APInt(v.getBitWidth(), 1), false);
      digits = "(" + llvm::toString(v + one, 10) + " - 1";  // INT_MIN går inte att skriva direkt
    } else {
      digits = llvm::toString(v, 10);
      if (digits.front() == '-') digits = "(" + digits;
    }
    bool paren = digits.front() == '(';
    const char* suffix = nullptr;
    switch (bt->getKind()) {
      case BuiltinType::Int: suffix = ""; break;
      case BuiltinType::UInt: suffix = "u"; break;
      case BuiltinType::Long: suffix = "l"; break;
      case BuiltinType::ULong: suffix = "ul"; break;
      case BuiltinType::LongLong: suffix = "ll"; break;
      case BuiltinType::ULongLong: suffix = "ull"; break;
      default: break;
    }
    if (suffix != nullptr) return digits + suffix + (paren ? ")" : "");
    TRY(ct, cType(t));
    return "((" + ct + ")" + digits + (paren ? ")" : "") + ")";
  }

  Text floatLiteral(const llvm::APFloat& v, QualType t) {
    const auto* bt = t.getCanonicalType()->getAs<BuiltinType>();
    if (bt == nullptr) return fail("floating constant of type " + t.getAsString());
    char buf[64];
    const char* suffix = "";
    if (bt->getKind() == BuiltinType::Float) {
      std::snprintf(buf, sizeof buf, "%.9g", static_cast<double>(v.convertToFloat()));
      suffix = "f";
    } else if (bt->getKind() == BuiltinType::Double) {
      std::snprintf(buf, sizeof buf, "%.17g", v.convertToDouble());
    } else {
      return fail("floating constant of type " + t.getAsString());
    }
    std::string s = buf;
    if (s.find_first_of("ni") != std::string::npos) return fail("non-finite constant");
    if (s.find_first_of(".e") == std::string::npos) s += ".0";
    s += suffix;
    return s.front() == '-' ? "(" + s + ")" : s;
  }

  Text constant(const APValue& v, QualType t) {
    if (v.isInt()) return intLiteral(v.getInt(), t);
    if (v.isFloat()) return floatLiteral(v.getFloat(), t);
    return fail("constant of type " + t.getAsString() + " is not supported");
  }

  // --- Uttryck --------------------------------------------------------------

  std::string varName(const VarDecl& vd, unsigned index = 0) {
    std::string n = vd.getNameAsString();
    if (n.empty()) n = "pork_p" + std::to_string(index);
    return n;
  }

  std::string selfValue() const { return self_ == Self::pointer ? "(*self)" : "self"; }
  std::string selfPointer() const { return self_ == Self::pointer ? "self" : "&self"; }

  static bool isThis(const Expr* e) {
    e = strip(e);
    if (isa<CXXThisExpr>(e)) return true;
    const auto* u = dyn_cast<UnaryOperator>(e);
    return u != nullptr && u->getOpcode() == UO_Deref && isa<CXXThisExpr>(strip(u->getSubExpr()));
  }

  Text expr(const Expr* e) {
    // vector<bool>: proxyn är elementet (se VectorBool.hpp).
    if (const Expr* el = bitRead(e)) return expr(el);
    if (auto [el, x] = bitWrite(e); el != nullptr) {
      TRY(l, expr(el));
      TRY(r, expr(x));
      return "(" + l + " = " + r + ")";
    }
    if (const auto* rw = dyn_cast<CXXRewrittenBinaryOperator>(e)) return expr(rw->getSemanticForm());  // a != b -> !(a == b)
    if (const auto* p = dyn_cast<ParenExpr>(e)) {
      TRY(x, expr(p->getSubExpr()));
      return "(" + x + ")";
    }
    if (const auto* w = dyn_cast<ExprWithCleanups>(e)) return expr(w->getSubExpr());
    if (const auto* m = dyn_cast<MaterializeTemporaryExpr>(e)) return expr(m->getSubExpr());
    if (const auto* b = dyn_cast<CXXBindTemporaryExpr>(e)) return expr(b->getSubExpr());
    if (const auto* s = dyn_cast<SubstNonTypeTemplateParmExpr>(e)) return expr(s->getReplacement());
    if (const auto* d = dyn_cast<CXXDefaultArgExpr>(e)) return expr(d->getExpr());
    if (const auto* d = dyn_cast<CXXDefaultInitExpr>(e)) return expr(d->getExpr());
    if (const auto* c = dyn_cast<ConstantExpr>(e)) {
      if (c->hasAPValueResult()) return constant(c->getAPValueResult(), c->getType());
      return expr(c->getSubExpr());
    }
    if (const auto* c = dyn_cast<CastExpr>(e)) return castExpr(*c);
    if (const auto* u = dyn_cast<UnaryExprOrTypeTraitExpr>(e)) {  // sizeof/alignof: värdet (Stone kräver konstant)
      std::optional<llvm::APSInt> v = u->getIntegerConstantExpr(ctx_);
      if (!v) return fail("sizeof/alignof that is not a constant");
      return intLiteral(*v, u->getType());
    }
    if (const auto* i = dyn_cast<IntegerLiteral>(e)) return intLiteral(llvm::APSInt(i->getValue(), i->getType()->isUnsignedIntegerType()), i->getType());
    if (const auto* f = dyn_cast<FloatingLiteral>(e)) return floatLiteral(f->getValue(), f->getType());
    if (const auto* b = dyn_cast<CXXBoolLiteralExpr>(e)) return std::string(b->getValue() ? "1" : "0");
    if (isa<CXXNullPtrLiteralExpr>(e)) return std::string("0");
    if (const auto* c = dyn_cast<CharacterLiteral>(e)) return std::to_string(c->getValue());
    if (const auto* d = dyn_cast<DeclRefExpr>(e)) return declRef(*d);
    if (const auto* m = dyn_cast<MemberExpr>(e)) return member(*m);
    if (const auto* u = dyn_cast<UnaryOperator>(e)) return unary(*u);
    if (const auto* b = dyn_cast<BinaryOperator>(e)) {
      if (b->getOpcode() == BO_Cmp || b->isPtrMemOp())
        return fail(std::string("operator ") + b->getOpcodeStr().str());
      // Stone godtar att en pekare (lånevärde) flyttas; i C är get_if-pekaren
      // bunden till sin dolda lokal, så det översätts inte (än).
      if (b->isAssignmentOp() && b->getLHS()->getType()->isPointerType())
        return fail("assignment to a pointer");
      TRY(l, expr(b->getLHS()));
      TRY(r, expr(b->getRHS()));
      return "(" + l + " " + b->getOpcodeStr().str() + " " + r + ")";
    }
    if (const auto* c = dyn_cast<ConditionalOperator>(e)) {
      TRY(k, expr(c->getCond()));
      TRY(a, expr(c->getTrueExpr()));
      TRY(b, expr(c->getFalseExpr()));
      return "(" + k + " ? " + a + " : " + b + ")";
    }
    if (isa<CallExpr>(e)) return call(*llvm::cast<CallExpr>(e));
    if (const auto* c = dyn_cast<CXXConstructExpr>(e)) return construct(*c);
    if (const auto* i = dyn_cast<InitListExpr>(e)) return initList(*i);
    if (isa<ImplicitValueInitExpr>(e)) return zero(e->getType());
    if (isa<UnaryExprOrTypeTraitExpr>(e)) {
      Expr::EvalResult r;
      if (e->EvaluateAsInt(r, ctx_)) return intLiteral(r.Val.getInt(), e->getType());
    }
    return fail(std::string("expression ") + e->getStmtClassName() + " is not supported");
  }

  Text castExpr(const CastExpr& c) {
    const Expr* sub = c.getSubExpr();
    switch (c.getCastKind()) {
      case CK_LValueToRValue:
      case CK_NoOp:
      case CK_ConstructorConversion:
      case CK_UserDefinedConversion:  // anropet (konstruktor/konverteringsfunktion) ligger under
        return expr(sub);
      case CK_IntegralCast:
        if (const auto* ed = unfixedEnum(c.getType()); ed != nullptr && !constantInEnum(*sub, *ed)) {
          TRY(x, expr(sub));
          return intToEnum(sub->getType(), c.getType(), *ed, x);
        }
        if (options_.strict && !fits(sub->getType(), c.getType()) && !constantFits(*sub, c.getType())) {
          TRY(x, expr(sub));
          return intToInt(sub->getType(), c.getType(), x);
        }
        [[fallthrough]];
      case CK_FloatingCast:
        if (c.getCastKind() == CK_FloatingCast && ctx_.getFloatingTypeOrder(c.getType(), sub->getType()) < 0) {
          TRY(x, expr(sub));
          return narrowFloat(sub->getType(), c.getType(), x);
        }
        [[fallthrough]];
      case CK_IntegralToFloating:
      case CK_IntegralToBoolean:
      case CK_FloatingToBoolean: {
        TRY(t, cType(c.getType()));
        TRY(x, expr(sub));
        return "((" + t + ")" + x + ")";
      }
      case CK_PointerToBoolean: {
        TRY(x, expr(sub));
        return "(" + x + " != 0)";
      }
      case CK_NullToPointer:
        return std::string("0");
      case CK_ToVoid: {
        TRY(x, expr(sub));
        return "((void)" + x + ")";
      }
      case CK_FloatingToIntegral: {
        TRY(x, expr(sub));
        return floatToInt(sub->getType(), c.getType(), x);
      }
      default:
        return fail(std::string("cast ") + c.getCastKindName() + " is not supported");
    }
  }

  // Heltalskonstant (t.ex. 1 i `x - 1` med unsigned long x) vars värde ryms i
  // `to`: ingen kontroll behövs.
  bool constantFits(const Expr& e, QualType to) const {
    std::optional<llvm::APSInt> v = e.getIntegerConstantExpr(ctx_);
    if (!v) return false;
    unsigned bits = ctx_.getIntWidth(to);
    bool targetSigned = to->isSignedIntegerOrEnumerationType();
    llvm::APSInt lo = llvm::APSInt::getMinValue(bits, !targetSigned), hi = llvm::APSInt::getMaxValue(bits, !targetSigned);
    return llvm::APSInt::compareValues(*v, lo) >= 0 && llvm::APSInt::compareValues(*v, hi) <= 0;
  }

  // En enum utan fast underliggande typ: bara värdena i dess intervall finns
  // (det minsta bitfältet som rymmer alla uppräknade värden). Att konvertera
  // ett annat värde till den är odefinierat ([expr.static.cast]).
  static const EnumDecl* unfixedEnum(QualType t) {
    const auto* et = t.getCanonicalType()->getAs<EnumType>();
    const EnumDecl* ed = et != nullptr ? et->getDecl()->getDefinition() : nullptr;
    return ed != nullptr && !ed->isFixed() ? ed : nullptr;
  }

  // Intervallet [lo, hi) för en enum utan fast typ ([dcl.enum]/8), som clangs
  // EnumDecl::getValueRange men i long long. nullopt om gränserna inte ryms.
  static std::optional<std::pair<long long, long long>> enumRange(const EnumDecl& ed) {
    unsigned neg = ed.getNumNegativeBits(), pos = ed.getNumPositiveBits();
    unsigned exp = neg != 0 ? std::max(neg, pos + 1) - 1 : pos;
    if (exp > 62) return std::nullopt;
    long long hi = 1LL << exp;
    return std::pair{neg != 0 ? -hi : 0LL, hi};
  }

  bool constantInEnum(const Expr& e, const EnumDecl& ed) const {
    std::optional<llvm::APSInt> v = e.getIntegerConstantExpr(ctx_);
    auto range = enumRange(ed);
    if (!v || !range) return false;
    if (v->isSigned() ? v->getSignificantBits() > 64 : v->getActiveBits() > 63) return false;
    return v->getExtValue() >= range->first && v->getExtValue() < range->second;
  }

  Text intToEnum(QualType from, QualType to, const EnumDecl& ed, const std::string& x) {
    TRY(f, cType(from));
    TRY(t, cType(to));
    auto range = enumRange(ed);
    if (!range) return fail("conversion to an enum whose values do not fit in long long");
    std::string cond = "(long long)pork_cv >= " + std::to_string(range->first) + "LL && (long long)pork_cv < " +
                       std::to_string(range->second) + "LL";
    if (!from->isSignedIntegerOrEnumerationType())  // stora unsigned-värden blir negativa i long long
      cond = "(unsigned long long)pork_cv < 9223372036854775808ULL && " + cond;
    return checkedConversion(f, t, x, cond, "conversion to enum out of range");
  }

  // Ryms alla värden av heltalstypen `from` i `to`?
  bool fits(QualType from, QualType to) const {
    unsigned fb = ctx_.getIntWidth(from), tb = ctx_.getIntWidth(to);
    bool fs = from->isSignedIntegerOrEnumerationType(), ts = to->isSignedIntegerOrEnumerationType();
    if (fs == ts) return fb <= tb;
    if (!fs) return fb < tb;  // unsigned -> signed: måste vara strikt bredare
    return false;             // signed -> unsigned: negativa värden ryms aldrig
  }

  // Kontrollerad konvertering av x till `to`: ett GNU statement expression vid
  // användningen, så att assertionen får källraden (#line), inte en hjälpfunktions.
  // `cond` är villkoret över pork_cv, värdet av x i typen `f`.
  Text checkedConversion(const std::string& f, const std::string& t, const std::string& x,
                         const std::string& cond, const char* message) {
    return "({ " + f + " pork_cv = " + x + "; __CPROVER_assert(" + cond + ", \"" + message +
           "\"); (" + t + ")pork_cv; })";
  }

  // --silver-strict: heltal -> heltal som kan tappa värdet (definierat eller
  // implementationsdefinierat i C++, men fel värde).
  Text intToInt(QualType from, QualType to, const std::string& x) {
    TRY(f, cType(from));
    TRY(t, cType(to));
    // fits() är falsk, så källan är signed och målet smalare eller unsigned,
    // eller källan unsigned och målet inte bredare. Målet är som mest 64 bitar.
    unsigned bits = ctx_.getIntWidth(to);
    bool targetSigned = to->isSignedIntegerOrEnumerationType();
    std::string hi = llvm::toString(llvm::APSInt::getMaxValue(bits, !targetSigned), 10);
    std::string cond;
    if (from->isSignedIntegerOrEnumerationType()) {
      if (targetSigned) {  // smalare än källan, så gränserna ryms i long long
        std::string lo = llvm::toString(llvm::APSInt::getMinValue(bits, false), 10);
        cond = "(long long)pork_cv >= " + lo + "LL && (long long)pork_cv <= " + hi + "LL";
      } else {
        cond = "(long long)pork_cv >= 0LL";
        if (ctx_.getIntWidth(from) > bits) cond += " && (unsigned long long)pork_cv <= " + hi + "ULL";
      }
    } else {
      cond = "(unsigned long long)pork_cv <= " + hi + "ULL";
    }
    return checkedConversion(f, t, x, cond, "integer conversion out of range");
  }

  // Ett smalare flyttal (double -> float): ett ändligt värde utanför målets
  // intervall är odefinierat ([conv.double]); i C blir det ±inf (SLV-9).
  Text narrowFloat(QualType from, QualType to, const std::string& x) {
    TRY(f, cType(from));
    TRY(t, cType(to));
    const auto* bt = to.getCanonicalType()->getAs<BuiltinType>();
    const char* sfx = bt == nullptr                                  ? nullptr
                      : bt->getKind() == BuiltinType::Float          ? "f"
                      : bt->getKind() == BuiltinType::Double         ? "d"
                                                                     : nullptr;
    if (sfx == nullptr) return fail("floating-point conversion to " + to.getAsString());
    return checkedConversion(f, t, x, std::string("!__CPROVER_isinf") + sfx + "((" + t + ")pork_cv)",
                             "floating-point conversion overflow");
  }

  // Flyttal -> heltal är odefinierat om värdet inte ryms.
  Text floatToInt(QualType from, QualType to, const std::string& x) {
    TRY(f, cType(from));
    TRY(t, cType(to));
    if (to->isBooleanType()) return fail("float to bool via integral cast");
    unsigned bits = ctx_.getIntWidth(to);
    auto pow2 = [](unsigned n) { return llvm::toString(llvm::APInt::getOneBitSet(n + 1, n), 10, false); };
    std::string cond;
    if (to->isSignedIntegerOrEnumerationType())
      cond = "(long double)pork_cv > -" + pow2(bits - 1) + ".0L - 1.0L && (long double)pork_cv < " + pow2(bits - 1) + ".0L";
    else
      cond = "(long double)pork_cv > -1.0L && (long double)pork_cv < " + pow2(bits) + ".0L";
    return checkedConversion(f, t, x, cond, "float-to-integer conversion out of range");
  }

  Text declRef(const DeclRefExpr& d) {
    const ValueDecl* decl = d.getDecl();
    if (auto it = overrides_.find(decl); it != overrides_.end()) return it->second;
    if (const auto* ec = dyn_cast<EnumConstantDecl>(decl)) return intLiteral(ec->getInitVal(), d.getType());
    const auto* vd = dyn_cast<VarDecl>(decl);
    if (vd == nullptr) return fail("reference to " + decl->getNameAsString());
    if (vd->hasLocalStorage()) {
      std::string n = varName(*vd);
      return pointers_.contains(vd) ? "(*" + n + ")" : n;
    }
    return constantVar(*vd);
  }

  Text constantVar(const VarDecl& vd) {
    const VarDecl* init = nullptr;
    if (vd.getAnyInitializer(init) != nullptr)
      if (const APValue* v = init->evaluateValue()) return constant(*v, vd.getType());
    return fail("variable '" + vd.getNameAsString() + "' is not a constant");
  }

  Text member(const MemberExpr& m) {
    if (const auto* vd = dyn_cast<VarDecl>(m.getMemberDecl())) return constantVar(*vd);
    const FieldDecl* f = asField(m.getMemberDecl());
    if (f == nullptr) return fail("member " + m.getMemberDecl()->getNameAsString());
    std::string name = f->getNameAsString();
    // Basklassens fält via this: C-structen är platt (allFields).
    const Expr* base = m.getBase();
    for (;;) {
      const auto* c = dyn_cast<ImplicitCastExpr>(strip(base));
      if (c == nullptr || (c->getCastKind() != CK_UncheckedDerivedToBase && c->getCastKind() != CK_DerivedToBase)) break;
      base = c->getSubExpr();
    }
    if (f->getType()->isReferenceType()) {  // kontextreferens: pekaren i structen
      if (isThis(base)) {
        if (self_ == Self::none) return fail("'this' outside a method");
        return std::string("(*") + (self_ == Self::pointer ? "self->" : "self.") + name + ")";
      }
      if (m.isArrow()) return fail("reference field '" + name + "' through a pointer");
      TRY(b, expr(base));
      return "(*" + b + "." + name + ")";
    }
    if (isThis(base)) {
      if (self_ == Self::none) return fail("'this' outside a method");
      return self_ == Self::pointer ? "self->" + name : "self." + name;
    }
    if (m.isArrow() && !isThis(m.getBase()) && isa<DeclRefExpr>(m.getBase()->IgnoreParenImpCasts())) {  // x->f, get_if
      TRY(b, expr(m.getBase()));
      return b + "->" + name;
    }
    if (m.isArrow()) {  // o->f med std::optional::operator->: (*o).f
      const auto* oc = dyn_cast<CXXOperatorCallExpr>(strip(m.getBase()));
      if (oc == nullptr || oc->getOperator() != OO_Arrow || !optionalSpec(oc->getArg(0)->getType()))
        return fail("member access through pointer");
      const Expr* object = oc->getArg(0);
      TRY(o, expr(object));
      if (!isPath(object)) return fail("std::optional::operator-> on a temporary");
      return optionalDeref(o, "operator->") + "." + name;
    }
    TRY(b, expr(m.getBase()));
    return b + "." + name;
  }

  Text unary(const UnaryOperator& u) {
    if (u.getOpcode() == UO_Deref && isThis(u.getSubExpr())) return selfValue();
    if (u.getOpcode() == UO_Deref && isa<CXXThisExpr>(strip(u.getSubExpr()))) return selfValue();
    TRY(x, expr(u.getSubExpr()));
    switch (u.getOpcode()) {
      case UO_Plus: return "(+" + x + ")";
      case UO_Minus: return "(-" + x + ")";
      case UO_Not: return "(~" + x + ")";
      case UO_LNot: return "(!" + x + ")";
      case UO_PreInc: return "(++" + x + ")";
      case UO_PreDec: return "(--" + x + ")";
      case UO_PostInc: return "(" + x + "++)";
      case UO_PostDec: return "(" + x + "--)";
      case UO_Deref: {  // get_if-pekare (Stone har kontrollerat)
        const auto* dr = dyn_cast<DeclRefExpr>(strip(u.getSubExpr()));
        const auto* vd = dr != nullptr ? dyn_cast<VarDecl>(dr->getDecl()) : nullptr;
        if (vd == nullptr || !nullables_.contains(vd)) return fail("dereference of this pointer");
        return "(*" + x + ")";
      }
      default: return fail(std::string("operator ") + UnaryOperator::getOpcodeStr(u.getOpcode()).str());
    }
  }

  // &(väg) för ett T&-argument.
  Text address(const Expr* e) {
    if (isThis(e)) {
      if (self_ == Self::none) return fail("'this' outside a method");
      return selfPointer();
    }
    if (isa<MaterializeTemporaryExpr>(strip(e)))
      return fail("temporary passed as non-const reference");
    TRY(x, expr(e));
    return "&" + x;
  }

  // Alltid kontrakt: pork::trusted och systemheaders. (Egna funktioner utan
  // definition här avgörs av Silver, se CFunction::externals.)
  static bool isTrustedCallee(const FunctionDecl& callee, const SourceManager& sm) {
    return hasAnnotation(callee, "pork::trusted") || sm.isInSystemHeader(callee.getLocation());
  }

  // std::hypot(float, float) -> std__hypot__float__float
  Text contractFor(const FunctionDecl& callee) {
    if (const auto* md = dyn_cast<CXXMethodDecl>(&callee); md != nullptr && md->isInstance())
      return fail("call to trusted member function '" + displayName(callee) + "'");
    std::string name = ident(trustName(callee));
    std::string params;
    for (const ParmVarDecl* p : callee.parameters()) {
      QualType t = p->getType();
      bool pointer = false;
      if (const auto* ref = t->getAs<ReferenceType>()) {
        t = ref->getPointeeType();
        pointer = !t.isConstQualified();
      }
      if (isConstCharPointer(t)) {  // bara för strängliteraler (StringLiteralArg.hpp)
        name += "__const_char_ptr";
        params += (params.empty() ? "" : ", ") + std::string("const char *");
        continue;
      }
      TRY(ct, cType(t));
      name += "__" + ident(ct);
      params += (params.empty() ? "" : ", ") + ct + (pointer ? " *" : "");
    }
    QualType rt = callee.getReturnType();
    std::string ret = "void";
    if (!rt->isVoidType()) {
      if (const auto* ref = rt->getAs<ReferenceType>()) {
        if (!ref->getPointeeType().isConstQualified())
          return fail("trusted '" + displayName(callee) + "' returns a non-const reference");
        rt = ref->getPointeeType();
      }
      TRY(r, cType(rt));
      ret = r;
    }
    if (!out_.contracts.contains(name)) {
      out_.contracts.emplace(name, trustName(callee));
      out_.decls.emplace_back(name, ret + " " + name + "(" + (params.empty() ? "void" : params) + ");\n");
    }
    return name;
  }

  Text call(const CallExpr& c) {
    if (preCondition(&c) != nullptr) return fail("PORK_PRE inside the body");
    if (const Expr* a = moveArg(&c)) return expr(a);  // en kopia; x används inte igen (Stone)
    if (const auto* oc = dyn_cast<CXXOperatorCallExpr>(&c); oc != nullptr && isMemberwiseAssign(*oc)) {
      TRY(l, expr(oc->getArg(0)));
      TRY(r, expr(oc->getArg(1)));
      return "(" + l + " = " + r + ")";
    }
    const FunctionDecl* callee = c.getDirectCallee();
    if (callee == nullptr) return fail("indirect call");
    if (const auto* oc = dyn_cast<CXXOperatorCallExpr>(&c); oc != nullptr && oc->getOperator() == OO_Call) {
      const auto* dr = dyn_cast<DeclRefExpr>(oc->getArg(0)->IgnoreParenImpCasts());
      const auto* var = dr ? dyn_cast<VarDecl>(dr->getDecl()) : nullptr;
      if (var != nullptr && lambdaVars_.contains(var)) {
        const auto& op = llvm::cast<CXXMethodDecl>(*callee);
        std::vector<std::string> args;
        for (unsigned i = 1; i < oc->getNumArgs(); ++i) {
          TRY(a, argument(op, i - 1, oc->getArg(i)));
          args.push_back(a);
        }
        return lambdaCall(op, *lambdaVars_.at(var), var, args);
      }
    }
    if (sm_.isInSystemHeader(callee->getLocation())) {
      if (auto r = variantFunction(c, *callee)) return *r;
      if (auto r = optionalCompare(c, *callee)) return *r;
      if (auto r = stringFunction(c)) return *r;
    }

    const Expr* object = nullptr;
    unsigned first = 0;
    if (const auto* mc = dyn_cast<CXXMemberCallExpr>(&c)) {
      object = mc->getImplicitObjectArgument();
    } else if (const auto* md = dyn_cast<CXXMethodDecl>(callee);
               md != nullptr && md->isInstance() && isa<CXXOperatorCallExpr>(&c)) {
      object = c.getArg(0);
      first = 1;
    }

    // Ett dispatchande anrop får aldrig bindas till en av implementationerna
    // (t.ex. basens kropp om den råkar finnas i programmet).
    if (const auto* md = dyn_cast<CXXMethodDecl>(callee); md != nullptr && isDispatch(c, *md, object))
      return dispatchCall(c, *md, first);

    if (const auto* md = dyn_cast<CXXMethodDecl>(callee); md != nullptr && md->isInstance()) {
      if (const auto* spec = optionalSpec(md->getParent()))
        return optionalCall(c, *md, object, first, *spec);
      if (const auto* spec = stringSpec(md->getParent()))
        return stringCall(c, *md, object, first, *spec);
      if (const auto* spec = vectorSpec(md->getParent()))
        return vectorCall(c, *md, object, first, *spec);
      if (constSpanSpec(ctx_.getCanonicalTagType(md->getParent())) != nullptr)
        return spanCall(c, *md, object, first);
      if (const auto* spec = variantSpec(md->getParent()))
        return variantCall(c, *md, object, first, *spec);
    }

    std::string name;
    if (isTrustedCallee(*callee, sm_)) {
      TRY(n, contractFor(*callee));
      name = n;
    } else {
      name = cFunctionName(*callee);
      if (inContract_) out_.contractCallees.insert(usrOf(*callee));
      if (!callee->isDefined()) {  // definierad i en annan TU, eller betrodd via trust-filen
        auto contract = contractFor(*callee);
        out_.externals[name] = contract ? *contract : std::string();
        out_.contracts.emplace(name, trustName(*callee));
      }
    }

    std::string args;
    auto add = [&](const std::string& a) { args += (args.empty() ? "" : ", ") + a; };
    if (object != nullptr) {
      const auto* md = llvm::cast<CXXMethodDecl>(callee);
      if (md->isConst()) {
        if (isThis(object)) {
          if (self_ == Self::none) return fail("'this' outside a method");
          add(selfValue());
        } else {
          TRY(o, expr(object));
          add(o);
        }
      } else {
        TRY(o, address(object));
        add(o);
      }
    }
    for (unsigned i = first; i < c.getNumArgs(); ++i) {
      unsigned pi = i - first;
      if (pi >= callee->getNumParams()) return fail("variadic call");
      const auto* ref = callee->getParamDecl(pi)->getType()->getAs<ReferenceType>();
      if (const StringLiteral* lit = stringLiteralArg(c.getArg(i), callee->getParamDecl(pi)->getType())) {
        add(cStringLiteral(*lit));
      } else if (ref != nullptr && !ref->getPointeeType().isConstQualified()) {
        TRY(a, address(c.getArg(i)));
        add(a);
      } else {
        TRY(a, expr(c.getArg(i)));
        add(a);
      }
    }
    return name + "(" + args + ")";
  }

  // Anrop till en egen konstruktor med objektet `self` (en pekare) först.
  Text constructorCall(const CXXConstructorDecl& ctor, const CXXConstructExpr& c, const std::string& self) {
    std::string name = cFunctionName(ctor);
    if (inContract_) out_.contractCallees.insert(usrOf(ctor));
    if (!ctor.isDefined()) {  // i en annan TU: Silver kopplar ihop via USR
      out_.externals[name] = std::string();
      out_.contracts.emplace(name, trustName(ctor));
    }
    std::string args = self;
    for (unsigned i = 0; i < c.getNumArgs(); ++i) {
      if (i >= ctor.getNumParams()) return fail("variadic constructor call");
      const auto* ref = ctor.getParamDecl(i)->getType()->getAs<ReferenceType>();
      if (const StringLiteral* lit = stringLiteralArg(c.getArg(i), ctor.getParamDecl(i)->getType())) {
        args += ", " + cStringLiteral(*lit);
      } else if (ref != nullptr && !ref->getPointeeType().isConstQualified()) {
        TRY(a, address(c.getArg(i)));
        args += ", " + a;
      } else {
        TRY(a, expr(c.getArg(i)));
        args += ", " + a;
      }
    }
    return name + "(" + args + ")";
  }

  // SLV-39: initieringslistan som tilldelningar till self->f, i ordning.
  Text ctorInits(const CXXConstructorDecl& ctor) {
    std::string out;
    for (const CXXCtorInitializer* init : ctor.inits()) {
      std::string at = lineOf(init->getSourceLocation());
      const Expr* e = init->getInit();
      if (init->isDelegatingInitializer()) {
        const auto* ce = dyn_cast<CXXConstructExpr>(e->IgnoreImplicit());
        if (ce == nullptr) return fail("delegating initializer");
        TRY(call, constructorCall(*ce->getConstructor(), *ce, "self"));
        out += at + call + ";\n";
        continue;
      }
      if (init->isBaseInitializer()) {
        const CXXRecordDecl* base = init->getBaseClass()->getAsCXXRecordDecl();
        if (base != nullptr && base->hasDefinition() && allFields(*base->getDefinition()).empty()) continue;
        return fail("base initializer (not translated yet)");
      }
      const FieldDecl* f = init->getMember();
      if (f == nullptr) return fail("member initializer");
      std::string field = "self->" + f->getNameAsString();
      if (f->getType()->isReferenceType()) {
        const auto* ref = dyn_cast<DeclRefExpr>(e->IgnoreParenImpCasts());
        const auto* p = ref == nullptr ? nullptr : dyn_cast<ParmVarDecl>(ref->getDecl());
        if (p == nullptr) return fail("reference field '" + f->getNameAsString() + "'");
        if (!pointers_.contains(p))
          return fail("reference field '" + f->getNameAsString() + "' bound to a const reference parameter");
        out += at + field + " = " + varName(*p, p->getFunctionScopeIndex()) + ";\n";
        continue;
      }
      TRY(x, expr(e));
      out += at + field + " = " + x + ";\n";
    }
    return out;
  }

  Text construct(const CXXConstructExpr& c) {
    const CXXConstructorDecl* ctor = c.getConstructor();
    if (const auto* spec = optionalSpec(ctor->getParent()); spec != nullptr && !ctor->isCopyOrMoveConstructor())
      return optionalConstruct(c, *spec);
    if (const auto* spec = variantSpec(ctor->getParent())) {
      if (ctor->isCopyOrMoveConstructor() && c.getNumArgs() == 1) return expr(c.getArg(0));
      if (c.getNumArgs() == 0) return variantDefault(*spec);
      if (c.getNumArgs() == 1) {
        auto k = altIndex(*spec, c.getArg(0)->getType());
        if (!k) return std::unexpected(k.error());
        TRY(x, expr(c.getArg(0)));
        return variantOf(*spec, *k, x);
      }
      return fail("constructor of " + recordName(*spec));
    }
    if (constSpanSpec(c.getType()) != nullptr) {
      if (const Expr* src = spanSource(&c, ctx_)) return spanFrom(c.getType(), src);
      return fail("constructor of " + recordName(*ctor->getParent()));
    }
    if (const auto* spec = sequenceSpec(ctor->getParent())) {
      if (ctor->isCopyOrMoveConstructor() && c.getNumArgs() == 1) return expr(c.getArg(0));
      if (c.getNumArgs() == 0) return zero(c.getType());
      // std::string s = "abc" (och en temporär till const std::string&).
      if (stringSpec(spec) != nullptr && c.getNumArgs() >= 1 && ctor->getNumParams() >= 1)
        if (const StringLiteral* lit = stringLiteralArg(c.getArg(0), ctor->getParamDecl(0)->getType()))
          if (c.getNumArgs() == 1 || isa<CXXDefaultArgExpr>(c.getArg(1))) return stringLiteral(*spec, *lit);
      return fail("constructor of " + recordName(*ctor->getParent()));
    }
    if (ctor->isCopyOrMoveConstructor() && (ctor->isTrivial() || !ctor->isUserProvided()) &&
        c.getNumArgs() == 1)
      return expr(c.getArg(0));
    // SLV-39: egen konstruktor -> en temporär som konstruktorn fyller i.
    if (ctor->isUserProvided() && !ctor->isCopyOrMoveConstructor() && !sm_.isInSystemHeader(ctor->getLocation())) {
      TRY(ct, cType(c.getType()));
      std::string tmp = "pork_c" + std::to_string(++loops_);
      TRY(call, constructorCall(*ctor, c, "&" + tmp));
      return "({ " + ct + " " + tmp + "; " + call + "; " + tmp + "; })";
    }
    if (c.getNumArgs() == 0 && c.requiresZeroInitialization()) return zero(c.getType());
    if (ctor->isDefaultConstructor() && !ctor->isUserProvided() && !sm_.isInSystemHeader(ctor->getLocation()))
      return defaultValue(*ctor->getParent());
    return fail("constructor of " + recordName(*ctor->getParent()));
  }

  // T x; med implicit standardkonstruktor: fältinitierarna (Stone har krävt
  // att varje fält har en, är en sådan klass, eller en betrodd typ som
  // optional/vector, vars standardvärde är tomt).
  Text defaultValue(const CXXRecordDecl& rd) {
    TRY(ct, cType(ctx_.getCanonicalTagType(&rd)));
    std::string parts;
    for (const FieldDecl* f : rd.fields()) {
      std::string v;
      if (f->hasInClassInitializer()) {
        TRY(x, expr(f->getInClassInitializer()));
        v = x;
      } else if (optionalSpec(f->getType()) || sequenceSpec(f->getType())) {
        TRY(z, zero(f->getType()));
        v = z;
      } else if (const auto* spec = variantSpec(f->getType())) {
        TRY(x, variantDefault(*spec));
        v = x;
      } else if (const CXXRecordDecl* sub = f->getType()->getAsCXXRecordDecl(); sub != nullptr && sub->hasDefinition()) {
        TRY(x, defaultValue(*sub->getDefinition()));
        v = x;
      } else {
        return fail("field '" + f->getNameAsString() + "' of " + recordName(rd) + " has no initializer");
      }
      parts += (parts.empty() ? "" : ", ") + v;
    }
    if (parts.empty()) return "(" + ct + "){0}";
    return "(" + ct + "){" + parts + "}";
  }

  Text initList(const InitListExpr& il) {
    if (il.isTransparent()) return expr(il.getInit(0));
    QualType t = il.getType();
    if (t->isArrayType()) return fail("array initializer");
    if (t.getCanonicalType()->getAsCXXRecordDecl() == nullptr) {
      if (il.getNumInits() == 0) return zero(t);
      return expr(il.getInit(0));
    }
    TRY(ct, cType(t));
    if (il.getNumInits() == 0) return "(" + ct + "){0}";
    // Den platta C-structen (allFields) har basens fält först, men listan har
    // basen som ett eget element: positionerna skulle inte stämma (SLV-2).
    if (t.getCanonicalType()->getAsCXXRecordDecl()->getNumBases() != 0)
      return fail("initializer list of a class with base classes (not translated yet)");
    std::string parts;
    for (const Expr* i : il.inits()) {
      TRY(x, expr(i));
      parts += (parts.empty() ? "" : ", ") + x;
    }
    return "(" + ct + "){" + parts + "}";
  }

  // --- Satser ---------------------------------------------------------------

  std::string lineOf(SourceLocation loc) {
    PresumedLoc p = sm_.getPresumedLoc(sm_.getExpansionLoc(loc));
    if (p.isInvalid()) return "";
    return "#line " + std::to_string(p.getLine()) + " " + quoted(p.getFilename()) + "\n";
  }

  Text stmt(const Stmt* s) {
    if (s == nullptr) return std::string();
    std::string at = lineOf(s->getBeginLoc());
    if (auto a = assertMacro(s, sm_, ctx_.getLangOpts())) {  // som SPARK:s pragma Assert
      TRY(c, expr(a->condition));
      return at + "__CPROVER_assert(" + c + ", " + quoted("assertion '" + a->text + "'") + ");\n";
    }
    if (const auto* e = dyn_cast<Expr>(s)) {
      // SLV-43: ett kast (STN-83) är ett körtidsfel i Silver: beviset ska visa
      // att satsen inte nås under förvillkoren (som raise i SPARK). Vägen
      // slutar där.
      const Expr* inner = e;
      if (const auto* fe = dyn_cast<FullExpr>(inner)) inner = fe->getSubExpr();
      if (const auto* te = dyn_cast<CXXThrowExpr>(inner->IgnoreParens())) {
        std::string type = "an exception";
        if (const Expr* op = te->getSubExpr())
          if (const CXXRecordDecl* rd = op->getType()->getAsCXXRecordDecl()) type = rd->getQualifiedNameAsString();
        return at + "__CPROVER_assert(0, " + quoted("throw " + type + " is reachable") + ");\n__CPROVER_assume(0);\n";
      }
      TRY(x, expr(e));
      return at + x + ";\n";
    }
    if (const auto* cs = dyn_cast<CompoundStmt>(s)) {
      std::string out = "{\n";
      for (const Stmt* child : cs->body()) {
        TRY(c, stmt(child));
        out += c;
      }
      return out + "}\n";
    }
    if (isa<NullStmt>(s)) return at + ";\n";
    if (isa<BreakStmt>(s)) return at + "break;\n";
    if (isa<ContinueStmt>(s)) return at + "continue;\n";
    if (const auto* r = dyn_cast<ReturnStmt>(s)) {
      if (postCheck_.empty()) {
        if (r->getRetValue() == nullptr) return at + "return;\n";
        TRY(x, returnValue(r->getRetValue()));
        return at + "return " + x + ";\n";
      }
      if (r->getRetValue() == nullptr) return "{\n" + at + postCheck_ + "\nreturn;\n}\n";
      TRY(rt, returnCType());
      TRY(x, returnValue(r->getRetValue()));
      return "{\n" + at + rt + " pork_result = " + x + ";\n" + at + postCheck_ + "\nreturn pork_result;\n}\n";
    }
    if (const auto* d = dyn_cast<DeclStmt>(s)) {
      std::string out;
      for (const Decl* decl : d->decls()) {
        if (isa<StaticAssertDecl>(decl)) continue;  // STN-64
        if (isa<UsingDecl, UsingDirectiveDecl>(decl)) continue;  // STN-81: bara namnuppslagning
        const auto* vd = dyn_cast<VarDecl>(decl);
        if (vd == nullptr) return fail("local declaration");
        TRY(v, local(*vd));
        out += at + v;
      }
      return out;
    }
    if (const auto* i = dyn_cast<IfStmt>(s)) {
      std::string init;
      if (i->getInit() != nullptr) {
        TRY(x, stmt(i->getInit()));
        init = x;
      }
      if (i->getConditionVariableDeclStmt() != nullptr) {
        TRY(x, stmt(i->getConditionVariableDeclStmt()));
        init += x;
      }
      TRY(c, expr(i->getCond()));
      TRY(t, stmt(i->getThen()));
      TRY(e, stmt(i->getElse()));
      std::string out = at + "if (" + c + ")\n" + t + (i->getElse() ? "else\n" + e : "");
      return init.empty() ? out : "{\n" + init + out + "}\n";
    }
    if (const auto* w = dyn_cast<WhileStmt>(s)) {
      if (w->getConditionVariable() != nullptr) return fail("while with a condition variable");
      TRY(c, loopCondition(w->getCond()));
      TRY(b, stmt(w->getBody()));
      return at + "while (" + c + ")\n" + b;
    }
    if (const auto* d = dyn_cast<DoStmt>(s)) {
      TRY(b, stmt(d->getBody()));
      TRY(c, expr(d->getCond()));
      return at + "do\n" + b + "while (" + c + ");\n";
    }
    if (const auto* f = dyn_cast<ForStmt>(s)) {
      if (f->getConditionVariable() != nullptr) return fail("for with a condition variable");
      TRY(init, stmt(f->getInit()));
      std::string c, inc;
      if (f->getCond()) {
        TRY(x, loopCondition(f->getCond()));
        c = x;
      }
      if (f->getInc()) {
        TRY(x, expr(f->getInc()));
        inc = x;
      }
      TRY(b, stmt(f->getBody()));
      return "{\n" + init + at + "for (; " + c + "; " + inc + ")\n" + b + "}\n";
    }
    if (const auto* sw = dyn_cast<SwitchStmt>(s)) {
      if (sw->getConditionVariable() != nullptr) return fail("switch with a condition variable");
      TRY(init, stmt(sw->getInit()));
      TRY(c, expr(sw->getCond()));
      TRY(b, stmt(sw->getBody()));
      std::string out = at + "switch (" + c + ")\n" + b;
      return init.empty() ? out : "{\n" + init + out + "}\n";
    }
    if (const auto* c = dyn_cast<CaseStmt>(s)) {
      if (c->caseStmtIsGNURange()) return fail("case range");
      Expr::EvalResult r;
      if (!c->getLHS()->EvaluateAsInt(r, ctx_)) return fail("non-constant case label");
      TRY(k, intLiteral(r.Val.getInt(), c->getLHS()->getType()));
      TRY(b, stmt(c->getSubStmt()));
      return at + "case " + k + ":\n" + b;
    }
    if (const auto* d = dyn_cast<DefaultStmt>(s)) {
      TRY(b, stmt(d->getSubStmt()));
      return at + "default:\n" + b;
    }
    if (const auto* rf = dyn_cast<CXXForRangeStmt>(s)) return rangeFor(*rf, at);
    return fail(std::string("statement ") + s->getStmtClassName() + " is not supported");
  }

  // for (T& x : v) -> indexloop till v.size. Stone garanterar att v är en väg
  // som inte ändras i loopen; x är en pekare till elementet eller en kopia.
  Text rangeFor(const CXXForRangeStmt& rf, const std::string& at) {
    const Expr* range = rf.getRangeInit();
    if ((!sequenceSpec(range->getType()) || !isPath(range)) && spanParam(range) == nullptr)
      return fail("range-for over this expression");
    TRY(v, expr(range));
    const VarDecl& var = *rf.getLoopVariable();
    std::string x = varName(var);
    std::string i = x + "__i";
    std::string element = v + ".data[" + i + "]";
    std::string decl;
    if (const auto* ref = var.getType()->getAs<ReferenceType>()) {
      TRY(ct, cType(ref->getPointeeType()));
      decl = std::string(ref->getPointeeType().isConstQualified() ? "const " : "") + ct + " *" + x + " = &" + element + ";\n";
      pointers_.insert(&var);
    } else {
      TRY(ct, cType(var.getType()));
      decl = ct + " " + x + " = " + element + ";\n";
    }
    TRY(b, stmt(rf.getBody()));
    return "{\n" + at + "unsigned long " + i + ";\n" + at + "for (" + i + " = 0; " + i + " < " + v + ".size && " + i +
           " < " + bound() + "; " + i + "++)\n{\n" + decl + b + "}\n}\n";
  }

  // Loopvillkor. `i < v.size()` (i size_t) får tillägget `&& i < N`
  // (N = --vector-bound). Det följer av v.size <= N och ändrar inget, men
  // när i är känt i varje varv ser cbmc att loopen slutar efter N varv.
  // Annars rullas den ut till --unwind, eftersom storleken är okänd.
  Text loopCondition(const Expr* cond) {
    TRY(c, expr(cond));
    const auto* b = dyn_cast<BinaryOperator>(cond->IgnoreParens());
    if (b == nullptr || (b->getOpcode() != BO_LT && b->getOpcode() != BO_GT)) return c;
    const Expr* counter = b->getOpcode() == BO_LT ? b->getLHS() : b->getRHS();
    const Expr* limit = b->getOpcode() == BO_LT ? b->getRHS() : b->getLHS();
    if (!isa<DeclRefExpr>(counter->IgnoreParenImpCasts()) || !isVectorSize(limit->IgnoreParenImpCasts())) return c;
    auto unsignedLong = [&](const Expr* e) {
      return e->getType().getCanonicalType().getUnqualifiedType() == ctx_.UnsignedLongTy;
    };
    if (!unsignedLong(counter) || !unsignedLong(limit)) return c;
    TRY(i, expr(counter));
    return "(" + c + ") && (" + i + ") < " + bound();
  }

  bool isVectorSize(const Expr* e) const {
    const auto* mc = dyn_cast<CXXMemberCallExpr>(e);
    if (mc == nullptr || mc->getNumArgs() != 0) return false;
    const CXXMethodDecl* md = mc->getMethodDecl();
    QualType t = mc->getImplicitObjectArgument()->getType();
    return md != nullptr && md->getName() == "size" && (vectorSpec(t) || constSpanSpec(t));
  }

  // Referens till ett element: (kontrollsats, adress). v[i], v.at(i), front,
  // back i en std::vector, *o / o.value() i en std::optional. Objektet är en
  // väg (Stones lån), så adressen är stabil så länge referensen lever.
  std::expected<std::pair<std::string, std::string>, std::string> elementAddress(const Expr* e) {
    const auto* c = dyn_cast<CallExpr>(e);
    const auto* md = c ? dyn_cast_or_null<CXXMethodDecl>(c->getDirectCallee()) : nullptr;
    if (md == nullptr || !md->isInstance()) return fail("local reference to this expression is not supported");
    const Expr* object = nullptr;
    unsigned first = 0;
    if (const auto* mc = dyn_cast<CXXMemberCallExpr>(c)) {
      object = mc->getImplicitObjectArgument();
    } else {
      object = c->getArg(0);
      first = 1;
    }
    if (!isPath(object)) return fail("local reference into a temporary");
    TRY(o, expr(object));
    std::string member = memberName(*md);
    unsigned nargs = c->getNumArgs() - first;
    if (optionalSpec(md->getParent()) && (member == "operator*" || member == "value") && nargs == 0)
      return std::pair{"__CPROVER_assert(" + o + ".has, " +
                           quoted("precondition of 'std::optional::" + member + "': has_value()") + "); ",
                       "&" + o + ".v"};
    if (const auto* seqSpec = sequenceSpec(md->getParent())) {
      const std::string seq = sequenceName(*seqSpec);
      if ((member == "front" || member == "back") && nargs == 0)
        return std::pair{"__CPROVER_assert(" + o + ".size > 0, " + quoted(vectorMessage(seq, member)) + "); ",
                         "&" + o + ".data[" + (member == "front" ? "0" : o + ".size - 1") + "]"};
      if ((member == "operator[]" || member == "at") && nargs == 1) {
        TRY(i, expr(c->getArg(first)));
        return std::pair{"__CPROVER_assert((" + i + ") < " + o + ".size, " + quoted(vectorMessage(seq, member)) + "); ",
                         "&" + o + ".data[" + i + "]"};
      }
    }
    return fail("local reference to " + displayName(*md) + " is not supported");
  }

  // SLV-42: kan det `ref` (bunden till `call`) refererar ändras mellan
  // bindningen och en senare användning av `ref`? Konservativt, i källordning:
  // en skrivning (tilldelning, ++/--, icke-const metod, icke-const
  // referens- eller pekarargument, lambda som fångar med referens) av en rot
  // i anropets objekt eller referensargument; ett anrop av en lambda; och,
  // när en rot ligger utanför funktionen (parameter, *this, referens), varje
  // anrop av användarkod (en port eller kontextreferens kan nå den). En
  // skrivning i en loop som inte innehåller bindningen räknas om ref används
  // någonstans i loopen. Svar: orsaken, eller inget om kopian är samma sak.
  std::optional<std::string> copyChangesMeaning(const VarDecl& ref, const CallExpr& call) {
    struct Root {
      const Decl* decl;  // nullptr: *this
      bool outside;
    };
    auto rootOf = [](const Expr* e) -> std::optional<Root> {
      for (;;) {
        e = e->IgnoreParenImpCasts();
        if (const auto* m = dyn_cast<MemberExpr>(e)) {
          e = m->getBase();
          continue;
        }
        if (const auto* oc = dyn_cast<CXXOperatorCallExpr>(e); oc != nullptr && oc->getNumArgs() > 0 &&
                                                                (oc->getOperator() == OO_Subscript || oc->getOperator() == OO_Star ||
                                                                 oc->getOperator() == OO_Arrow)) {
          e = oc->getArg(0);
          continue;
        }
        if (const auto* u = dyn_cast<UnaryOperator>(e); u != nullptr && u->getOpcode() == UO_Deref) {
          e = u->getSubExpr();
          continue;
        }
        if (isa<CXXThisExpr>(e)) return Root{nullptr, true};
        if (const auto* dr = dyn_cast<DeclRefExpr>(e))
          if (const auto* v = dyn_cast<VarDecl>(dr->getDecl()); v != nullptr && v->hasLocalStorage())
            return Root{v, v->getType()->isReferenceType() || v->getType()->isPointerType()};
        return std::nullopt;
      }
    };
    std::vector<Root> roots;
    auto addRoot = [&](const Expr* e) -> bool {
      auto r = rootOf(e);
      if (!r) return false;
      roots.push_back(*r);
      return true;
    };
    const FunctionDecl* callee = call.getDirectCallee();
    if (const auto* mc = dyn_cast<CXXMemberCallExpr>(&call)) {
      if (!addRoot(mc->getImplicitObjectArgument())) return "and its object is not a variable or *this";
    }
    for (unsigned i = 0; i < call.getNumArgs() && callee != nullptr; ++i) {
      unsigned pi = isa<CXXOperatorCallExpr>(call) && isa<CXXMethodDecl>(callee) ? i - 1 : i;
      if (isa<CXXOperatorCallExpr>(call) && isa<CXXMethodDecl>(callee) && i == 0) {
        if (!addRoot(call.getArg(0))) return "and its object is not a variable or *this";
        continue;
      }
      if (pi < callee->getNumParams() && callee->getParamDecl(pi)->getType()->isReferenceType() &&
          !isa<MaterializeTemporaryExpr>(call.getArg(i)->IgnoreParenImpCasts()) && !addRoot(call.getArg(i)))
        return "and an argument is not a variable";
    }
    bool anyOutside = std::ranges::any_of(roots, [](const Root& r) { return r.outside; });
    auto hits = [&](const Expr* e) {
      auto r = rootOf(e);
      if (!r) return true;  // okänt mål: försiktigt
      return std::ranges::any_of(roots, [&](const Root& x) { return x.decl == r->decl || (x.outside && r->outside); });
    };
    struct Event {
      const Stmt* at;
      std::vector<const Stmt*> loops;
      std::string what;
    };
    std::vector<Event> writes;
    std::vector<std::pair<const Stmt*, std::vector<const Stmt*>>> uses;
    std::vector<const Stmt*> loops;
    std::vector<const Stmt*> declLoops;
    bool afterDecl = false;
    auto userCode = [&](const FunctionDecl* f) { return f == nullptr || !isTrustedCallee(*f, sm_); };
    std::function<void(const Stmt*)> walk = [&](const Stmt* st) {
      if (st == nullptr) return;
      if (const auto* ds = dyn_cast<DeclStmt>(st))
        for (const Decl* d : ds->decls())
          if (d == &ref) {
            afterDecl = true;
            declLoops = loops;
          }
      bool loop = isa<ForStmt, WhileStmt, DoStmt, CXXForRangeStmt>(st);
      if (loop) loops.push_back(st);
      for (const Stmt* c : st->children()) walk(c);
      if (loop) loops.pop_back();
      auto write = [&](std::string what) { writes.push_back({st, loops, std::move(what)}); };
      if (const auto* dr = dyn_cast<DeclRefExpr>(st); dr != nullptr && dr->getDecl() == &ref) uses.push_back({st, loops});
      if (const auto* b = dyn_cast<BinaryOperator>(st); b != nullptr && b->isAssignmentOp() && hits(b->getLHS()))
        write("and an assignment may change what it refers to");
      if (const auto* u = dyn_cast<UnaryOperator>(st); u != nullptr && u->isIncrementDecrementOp() && hits(u->getSubExpr()))
        write("and an increment may change what it refers to");
      if (const auto* le = dyn_cast<LambdaExpr>(st))
        for (const LambdaCapture& c : le->captures())
          if (c.capturesThis() || (c.capturesVariable() && c.getCaptureKind() == LCK_ByRef))
            write("and a lambda captures what it refers to");
      const FunctionDecl* f = nullptr;
      if (const auto* c = dyn_cast<CallExpr>(st)) {
        f = c->getDirectCallee();
        if (const auto* md = dyn_cast_or_null<CXXMethodDecl>(f); md != nullptr && md->getParent()->isLambda())
          write("and a lambda call may change what it refers to");
        if (const auto* mc = dyn_cast<CXXMemberCallExpr>(c))
          if (const auto* md = mc->getMethodDecl(); md != nullptr && !md->isConst() && hits(mc->getImplicitObjectArgument()))
            write("and '" + md->getNameAsString() + "' may change what it refers to");
        if (const auto* oc = dyn_cast<CXXOperatorCallExpr>(c))
          if (const auto* md = dyn_cast_or_null<CXXMethodDecl>(f);
              md != nullptr && md->isInstance() && !md->isConst() && oc->getNumArgs() > 0 && hits(oc->getArg(0)))
            write("and '" + md->getNameAsString() + "' may change what it refers to");
        for (unsigned i = 0; i < c->getNumArgs() && f != nullptr; ++i) {
          unsigned pi = isa<CXXOperatorCallExpr>(c) && isa<CXXMethodDecl>(f) ? i - 1 : i;
          if (isa<CXXOperatorCallExpr>(c) && isa<CXXMethodDecl>(f) && i == 0) continue;
          if (pi >= f->getNumParams()) continue;
          QualType pt = f->getParamDecl(pi)->getType();
          bool mut = (pt->isReferenceType() || pt->isPointerType()) && !pt->getPointeeType().isConstQualified();
          if (mut && hits(c->getArg(i))) write("and a call may change what it refers to");
        }
        if (anyOutside && userCode(f) && c != &call) write("and a call to user code may change what it refers to");
      }
      if (const auto* ce = dyn_cast<CXXConstructExpr>(st); ce != nullptr && anyOutside && userCode(ce->getConstructor()))
        write("and a constructor may change what it refers to");
    };
    walk(fd_.getBody());
    SourceLocation declAt = ref.getBeginLoc();
    auto before = [&](const Stmt* a, const Stmt* b) {
      return sm_.isBeforeInTranslationUnit(a->getBeginLoc(), b->getBeginLoc());
    };
    auto atOrAfterDecl = [&](const Stmt* a) { return !sm_.isBeforeInTranslationUnit(a->getBeginLoc(), declAt); };
    for (const Event& w : writes) {
      bool afterBinding = atOrAfterDecl(w.at);
      for (const auto& [u, uloops] : uses) {
        if (afterBinding && before(w.at, u)) return w.what + " while it is used later";
        // En loop som innehåller skrivningen och en användning men inte
        // bindningen: nästa varv använder referensen efter skrivningen.
        for (const Stmt* l : w.loops)
          if (std::ranges::find(declLoops, l) == declLoops.end() && std::ranges::find(uloops, l) != uloops.end())
            return w.what + " in a loop that uses it";
      }
    }
    return std::nullopt;
  }

  Text local(const VarDecl& vd) {
    std::string name = varName(vd);
    const Expr* init = vd.getInit();
    if (init == nullptr) {
      // Skalär utan initierare: Stone har visat att den tilldelas före varje
      // läsning (clangs analys), så värdet i C (godtyckligt för cbmc) läses aldrig.
      QualType t = vd.getType();
      if (t->isReferenceType() || t->isPointerType() || !(t->isIntegralOrEnumerationType() || t->isRealFloatingType()))
        return fail("uninitialized local '" + name + "'");
      TRY(ct, cType(t));
      return ct + " " + name + ";\n";
    }
    if (const CXXRecordDecl* rd = vd.getType()->getAsCXXRecordDecl(); rd != nullptr && rd->isLambda()) {
      const auto* le = dyn_cast<LambdaExpr>(init->IgnoreImplicit());
      if (le == nullptr) return fail("lambda variable '" + name + "'");
      lambdaVars_[&vd] = le;
      std::string out;  // kopiorna tas nu
      for (const LambdaCapture& cap : le->captures()) {
        if (cap.getCaptureKind() == LCK_ByRef) continue;
        const auto* cv = llvm::cast<VarDecl>(cap.getCapturedVar());
        TRY(ct, cType(cv->getType().getNonReferenceType()));
        std::string n = varName(*cv);
        out += ct + " " + name + "__" + n + " = " + (pointers_.contains(cv) ? "(*" + n + ")" : n) + ";\n";
      }
      return out;
    }
    if (vd.getType()->isPointerType()) return getIf(vd);
    if (const auto* ref = vd.getType()->getAs<ReferenceType>()) {
      QualType t = ref->getPointeeType();
      TRY(ct, cType(t));
      const Expr* target = strip(init);
      if (isa<MaterializeTemporaryExpr>(target)) {  // livslängden förlängs: ett värde
        TRY(x, expr(target));
        return ct + " " + name + " = " + x + ";\n";
      }
      std::string decl = std::string(t.isConstQualified() ? "const " : "") + ct + " *" + name + " = ";
      // Egen funktion som returnerar const T&: C-funktionen ger värdet, så
      // referensen blir en kopia. Det är samma program bara om det den
      // refererar inte kan ändras mellan bindningen och en senare användning
      // (SLV-42; Stone kräver det inte, lånemodellen ser bara döda lån).
      if (const auto* call = dyn_cast<CallExpr>(target)) {
        const FunctionDecl* callee = call->getDirectCallee();
        if (callee != nullptr && t.isConstQualified() && callee->getReturnType()->isLValueReferenceType() &&
            !isTrustedCallee(*callee, sm_)) {
          if (auto why = copyChangesMeaning(vd, *call))
            return fail("const reference '" + vd.getNameAsString() + "' to the result of '" +
                        callee->getNameAsString() + "' would be a copy in C, " + *why);
          TRY(x, expr(target));
          return "const " + ct + " " + name + " = " + x + ";\n";
        }
      }
      if (!isa<DeclRefExpr, MemberExpr, UnaryOperator>(target)) {
        auto e = elementAddress(target);
        if (!e) return std::unexpected(e.error());
        pointers_.insert(&vd);
        return e->first + decl + e->second + ";\n";
      }
      TRY(a, address(target));
      pointers_.insert(&vd);
      return decl + a + ";\n";
    }
    TRY(ct, cType(vd.getType()));
    TRY(x, expr(init));
    return ct + " " + name + " = " + x + ";\n";
  }

  // T* x = std::get_if<A>(&v) -> T *x = v.index == k ? &v.u.ak : 0;
  // Returtypen i C: `const T&` som värde, `const T*` som nullableStruct.
  // Nya godtyckliga (giltiga) värden för det ett anrop kan ändra i `target`
  // (ett C-lvalue av typen t). `writable`: värdena själva; kontextreferensernas
  // mål alltid (utom const T&), pekarna i structen aldrig.
  // Ett godtyckligt värde av C-typen ct: ett anrop av en funktion utan kropp
  // med prefixet __VERIFIER_nondet_. cbmc och ESBMC ger ett eget godtyckligt
  // värde per anrop, fast mellan läsningar av variabeln, utan varningen
  // "no body". Indata är aldrig oinitierade variabler: att läsa en sådan är
  // odefinierat beteende i C.
  std::string nondet(const std::string& ct) {
    std::string type = ct;
    for (std::string::size_type at; (at = type.find("const ")) != std::string::npos;) type.erase(at, 6);
    std::string fn = "__VERIFIER_nondet_pork_" + mangled(type);
    out_.decls.emplace_back(fn, type + " " + fn + "(void);\n");
    return fn + "()";
  }

  static std::string mangled(const std::string& ct) {
    std::string out;
    for (char c : ct) out += std::isalnum(static_cast<unsigned char>(c)) ? c : '_';
    return out;
  }

  Text havocInto(QualType t, const std::string& target, bool writable) {
    t = t.getCanonicalType().getUnqualifiedType();
    const CXXRecordDecl* rd = t->getAsCXXRecordDecl();
    bool hasRefs = false;
    if (rd != nullptr && rd->hasDefinition() && !sequenceSpec(rd) && !optionalSpec(rd) && !variantSpec(rd))
      for (const FieldDecl* f : allFields(*rd->getDefinition())) hasRefs = hasRefs || f->getType()->isReferenceType();
    if (!hasRefs) {
      if (!writable) return std::string();
      TRY(ct, cType(t));
      std::string h = "pork_h" + std::to_string(++loops_);
      return "{\n  " + ct + " " + h + " = " + nondet(ct) + ";\n" + assumeValid(t, h) + "  " + target + " = " + h + ";\n}\n";
    }
    std::string out;
    for (const FieldDecl* f : allFields(*rd->getDefinition())) {
      std::string field = target + "." + f->getNameAsString();
      if (const auto* ref = f->getType()->getAs<LValueReferenceType>()) {
        QualType pt = ref->getPointeeType();
        const CXXRecordDecl* prd = pt->getAsCXXRecordDecl();
        if (prd != nullptr && prd->hasDefinition() && prd->getDefinition()->isPolymorphic()) continue;  // port
        TRY(h, havocInto(pt, "(*" + field + ")", !pt.isConstQualified()));
        out += h;
      } else if (writable) {
        TRY(h, havocInto(f->getType(), field, true));
        out += h;
      }
    }
    return out;
  }

  // Ett dispatchande anrop (SLV-34): gränssnittskontraktets kontraktsversion
  // (förvillkoret kontrolleras, eftervillkoret antas, T&-argument får nya
  // värden), sedan nya värden för allt utanför funktionen som implementationen
  // kan nå (STN-54): *this, kontexten och referensparametrarna. Saknas
  // kontraktet: "no contract for 'I::f (interface contract)'".
  Text dispatchCall(const CallExpr& c, const CXXMethodDecl& md, unsigned first) {
    std::string name = interfaceContractName(md);
    out_.contracts.emplace(name, displayName(md) + " (interface contract)");
    out_.externals.emplace(name, std::string());
    TRY(it, cType(ctx_.getCanonicalTagType(md.getParent())));
    std::string ret = "void";
    if (!md.getReturnType()->isVoidType()) {
      if (md.getReturnType()->isPointerType()) return fail("dispatching call returning a pointer");
      TRY(r, cType(md.getReturnType().getNonReferenceType()));
      ret = r;
    }
    std::string plist = md.isConst() ? it : it + "*";
    for (const ParmVarDecl* p : md.parameters()) {
      QualType t = p->getType();
      bool pointer = false;
      if (const auto* ref = t->getAs<LValueReferenceType>()) {
        t = ref->getPointeeType();
        pointer = !t.isConstQualified();
      }
      TRY(ct, cType(t));
      plist += ", " + ct + (pointer ? "*" : "");
    }
    out_.decls.emplace_back("proto:" + name, ret + " " + name + "(" + plist + ");\n");

    std::string k = std::to_string(++loops_);
    std::string selfArg = md.isConst() ? "((" + it + "){0})" : "&pork_if" + k;
    std::string args = selfArg;
    for (unsigned i = first; i < c.getNumArgs(); ++i) {
      unsigned pi = i - first;
      if (pi >= md.getNumParams()) return fail("variadic call");
      const auto* ref = md.getParamDecl(pi)->getType()->getAs<ReferenceType>();
      if (const StringLiteral* lit = stringLiteralArg(c.getArg(i), md.getParamDecl(pi)->getType())) {
        args += ", " + cStringLiteral(*lit);
      } else if (ref != nullptr && !ref->getPointeeType().isConstQualified()) {
        TRY(a, address(c.getArg(i)));
        args += ", " + a;
      } else {
        TRY(a, expr(c.getArg(i)));
        args += ", " + a;
      }
    }
    std::string havoc;
    if (self_ != Self::none) {
      TRY(h, havocInto(ctx_.getCanonicalTagType(llvm::cast<CXXMethodDecl>(fd_).getParent()),
                       self_ == Self::pointer ? "(*self)" : "self", true));
      havoc += h;
    }
    for (unsigned i = 0; i < fd_.getNumParams(); ++i) {
      const ParmVarDecl* p = fd_.getParamDecl(i);
      if (!p->getType()->isReferenceType()) continue;
      bool pointer = pointers_.contains(p);
      TRY(h, havocInto(p->getType().getNonReferenceType(), pointer ? "(*" + varName(*p, i) + ")" : varName(*p, i), true));
      havoc += h;
    }
    std::string pre = md.isConst() ? "" : it + " pork_if" + k + " = {0}; ";
    std::string call = name + "(" + args + ")";
    if (ret == "void") return "({ " + pre + call + "; " + havoc + " })";
    return "({ " + pre + ret + " pork_dr" + k + " = " + call + ";\n" + havoc + " pork_dr" + k + "; })";
  }

  static bool returnsOwnClass(const FunctionDecl& fd) {
    const auto* md = dyn_cast<CXXMethodDecl>(&fd);
    if (md == nullptr) return false;
    const ASTContext& ctx = md->getASTContext();
    return ctx.hasSameType(md->getReturnType().getNonReferenceType().getUnqualifiedType().getCanonicalType(),
                           ctx.getCanonicalTagType(md->getParent()));
  }

  Text returnCType() {
    QualType rt = fd_.getReturnType();
    if (rt->isPointerType()) return nullableStruct(rt->getPointeeType());
    // SLV-41: T& in i *this (STN-76) är en väg som anroparen skriver genom;
    // ett returnerat värde vore inte samma program. (`return *this` som C&,
    // STN-73, översätts redan.)
    if (const auto* ref = rt->getAs<LValueReferenceType>(); ref != nullptr && !ref->getPointeeType().isConstQualified() &&
        !returnsOwnClass(fd_))
      return fail("return type " + rt.getAsString() + " (a non-const reference, not translated yet)");
    return cType(rt.getNonReferenceType());
  }

  // `const T*` som returvärde (Stone: null eller ett lån in i *this/en
  // const&-parameter): `struct { _Bool has; T v; }`. En const&-parameter är
  // en kopia i C, så en C-pekare in i den skulle hänga; värdet är detsamma
  // eftersom lånet hindrar ändringar medan anroparen använder pekaren.
  Text nullableStruct(QualType pointee) {
    TRY(vt, cType(pointee));
    std::string name = "struct np_" + ident(vt) + "_" + hash8(pointee.getCanonicalType().getUnqualifiedType().getAsString());
    if (types_.insert(name).second) out_.decls.emplace_back(name, name + " {\n  _Bool has;\n  " + vt + " v;\n};\n");
    return name;
  }

  Text returnValue(const Expr* e) {
    if (!fd_.getReturnType()->isPointerType()) return expr(e);
    TRY(st, nullableStruct(fd_.getReturnType()->getPointeeType()));
    std::string none = "((" + st + "){0})";
    if (e->isNullPointerConstant(ctx_, Expr::NPC_ValueDependentIsNotNull) != Expr::NPCK_NotNull) return none;
    const Expr* x = e->IgnoreParenImpCasts();
    if (const auto* w = dyn_cast<ExprWithCleanups>(x)) x = w->getSubExpr()->IgnoreParenImpCasts();
    if (const auto* dr = dyn_cast<DeclRefExpr>(x)) {
      const auto* vd = dyn_cast<VarDecl>(dr->getDecl());
      if (vd == nullptr || !nullables_.contains(vd)) return fail("returned pointer is not a get_if pointer");
      std::string n = varName(*vd);
      return "(" + n + " ? ((" + st + "){1, *" + n + "}) : " + none + ")";
    }
    if (const auto* c = dyn_cast<CallExpr>(x); c != nullptr && c->getDirectCallee() != nullptr) {
      if (c->getDirectCallee()->getQualifiedNameAsString() == "std::get_if") {
        auto slot = getIfSlot(*c, fd_.getReturnType()->getPointeeType());
        if (!slot) return std::unexpected(slot.error());
        return "(" + slot->first + " ? ((" + st + "){1, " + slot->second + "}) : " + none + ")";
      }
      return expr(c);  // en annan funktion som returnerar samma struct
    }
    return fail("returned pointer");
  }

  // std::get_if<A>(&v): (villkor, alternativets lvalue).
  std::expected<std::pair<std::string, std::string>, std::string> getIfSlot(const CallExpr& c, QualType pointee) {
    const auto* u = c.getNumArgs() == 1 ? dyn_cast<UnaryOperator>(c.getArg(0)->IgnoreParenImpCasts()) : nullptr;
    if (u == nullptr || u->getOpcode() != UO_AddrOf) return fail("std::get_if without &");
    const Expr* object = u->getSubExpr();
    const auto* spec = variantSpec(object->getType());
    if (spec == nullptr || !isPath(object)) return fail("std::get_if on this expression");
    auto k = altIndex(*spec, pointee);
    if (!k) return std::unexpected(k.error());
    TRY(v, expr(object));
    std::string slot = std::to_string(*k);
    return std::pair{v + ".index == " + slot + "ul", v + ".u.a" + slot};
  }

  Text getIf(const VarDecl& vd) {
    // `const T* x = f(...)`, f returnerar const T* (nullableStruct): värdet i
    // en dold lokal, x pekar in i den (eller är null), som en get_if-pekare.
    if (const auto* call = dyn_cast<CallExpr>(vd.getInit()->IgnoreParenImpCasts());
        call != nullptr && call->getDirectCallee() != nullptr &&
        call->getDirectCallee()->getQualifiedNameAsString() != "std::get_if") {
      QualType pointee = vd.getType()->getPointeeType();
      TRY(st, nullableStruct(pointee));
      TRY(ct, cType(pointee));
      TRY(x, expr(call));
      std::string n = varName(vd), hidden = "pork_np_" + n;
      nullables_.insert(&vd);
      return st + " " + hidden + " = " + x + ";\nconst " + ct + " *" + n + " = " + hidden + ".has ? &" + hidden +
             ".v : 0;\n";
    }
    const auto* c = dyn_cast<CallExpr>(vd.getInit()->IgnoreImplicit());
    const auto* u = c && c->getNumArgs() == 1 ? dyn_cast<UnaryOperator>(c->getArg(0)->IgnoreParenImpCasts()) : nullptr;
    if (u == nullptr || u->getOpcode() != UO_AddrOf) return fail("pointer variable '" + varName(vd) + "'");
    const Expr* object = u->getSubExpr();
    const auto* spec = variantSpec(object->getType());
    if (spec == nullptr || !isPath(object)) return fail("std::get_if on this expression");
    QualType pointee = vd.getType()->getPointeeType();
    auto k = altIndex(*spec, pointee);
    if (!k) return std::unexpected(k.error());
    TRY(ct, cType(pointee));
    TRY(v, expr(object));
    nullables_.insert(&vd);
    std::string slot = std::to_string(*k);
    return std::string(pointee.isConstQualified() ? "const " : "") + ct + " *" + varName(vd) + " = " + v +
           ".index == " + slot + "ul ? &" + v + ".u.a" + slot + " : 0;\n";
  }

  const FunctionDecl& fd_;
  const CXXMethodDecl* contractOf_ = nullptr;  // fd_ är gränssnittskontraktet för den här metoden
  ASTContext& ctx_;
  const SourceManager& sm_;
  TranslateOptions options_;
  CFunction out_;
  Self self_ = Self::none;
  std::set<const VarDecl*> pointers_;  // T&-parametrar och lokala referenser: pekare i C
  std::set<const VarDecl*> nullables_;  // get_if-pekare: pekare i C, används som pekare
  std::map<const VarDecl*, const LambdaExpr*> lambdaVars_;
  std::map<const CXXMethodDecl*, std::string> lambdaNames_;
  std::string lambdaDefs_;  // lambdornas C-funktioner, före definitionen
  std::map<const ValueDecl*, std::string> overrides_;  // namn som ersätts (PORK_POST)
  std::string postCheck_;   // assert av eftervillkoret vid return (tomt = inget)
  bool inContract_ = false;  // översätter PRE/POST: anrop noteras i contractCallees
  std::set<std::string> types_;        // deklarerade structar och hjälpfunktioner
  std::set<std::string> recordsInProgress_;  // record() på väg ner (rekursiva typer, STN-63)
  unsigned loops_ = 0;                 // unika loopvariabler i harnessen
};

#undef TRY

}  // namespace

std::expected<CFunction, std::string> translateToC(const FunctionDecl& fd,
                                                   const TranslateOptions& options) {
  return Translator(fd, options).run();
}

}  // namespace pork
