#pragma once

#include "Facts.hpp"
#include "LoanSummaries.hpp"

#include <clang/AST/Decl.h>

#include <memory>

namespace pork {

// --loans: lånemodellen (docs/stone_model.md) körs alltid; Stone kräver att
// både LocalCheck och lånemodellen godkänner (LocalCheck har inte längre
// regler för alla lån, så Stone utan modellen vore osunt).
//  enforce = standard
//  report  = samma Stone-dom, plus en rad med modellens och reglernas domar
//            var för sig (tests/check_loans.py)
enum class LoanMode { enforce, report };

struct LoanOptions {
  LoanMode mode = LoanMode::enforce;
  LoanSummaries summaries;  // std-sammanfattningar (summaries/std.txt + --summaries)
};

// Lånemodellen för en funktion: platser, lån och sammanfattningar i stället
// för en regel per idiom. Ett lån får inte överleva en ändring av det lånade
// (G2), argument till samma anrop överlappar inte när ett av dem skrivs (G3).
// Okända uttryck som bär lån ger nej, aldrig ja.
Reason checkLoans(const clang::FunctionDecl& fd);

// En niebloid i std::ranges (`std::ranges::sort` är ett objekt vars
// operator() är algoritmen): ett anrop av den är ett anrop av en fri
// funktion, för både LoanCheck och LocalCheck.
bool isNiebloid(const clang::FunctionDecl& fd);

// Regeln i std-sammanfattningarna för ett anrop av fd (ingen regel: standarden).
// Samma uppslagning används av LocalCheck (vad en referens från std lånar).
LoanRule libraryRule(const LoanSummaries& lib, const clang::FunctionDecl* fd);

// En per översättningsenhet: sammanfattningar av egna funktioner (vad de
// ändrar, vad returen lånar), härledda ur kropparna och återanvända vid
// anrop. Anropade funktioner sammanfattas vid behov, oavsett ordning.
class LoanContext {
 public:
  explicit LoanContext(const LoanSummaries& lib);  // lib måste överleva kontexten
  ~LoanContext();
  LoanContext(const LoanContext&) = delete;
  LoanContext& operator=(const LoanContext&) = delete;

  Reason check(const clang::FunctionDecl& fd);

  struct Impl;

 private:
  std::unique_ptr<Impl> impl_;
};

}  // namespace pork
