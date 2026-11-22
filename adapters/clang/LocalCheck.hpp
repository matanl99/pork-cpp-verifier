#pragma once

#include "Facts.hpp"
#include "LoanSummaries.hpp"

#include <clang/AST/ASTContext.h>
#include <clang/AST/Decl.h>

#include <clang/AST/DeclTemplate.h>
#include <clang/AST/ExprCXX.h>

#include <string_view>
#include <vector>

namespace pork {

// Kontrollerar en funktion mot Stone-delmängden, utan att titta på
// anropade funktioner (de samlas bara in i `callees`).
//
// Princip: tillåtelselista. Varje sats-/uttrycksslag som inte uttryckligen
// hanteras här ger "unsupported ...". Hellre falskt nej än falskt ja.
//
// `trust.types` avgör vilka klasstemplates som räknas som värdetyper; de som
// används läggs i `assumedTypes` ("type std::optional").
Reason checkFunction(const clang::FunctionDecl& fd, const TrustList& trust,
                     std::map<std::string, Callee>& callees,
                     std::set<std::string>& assumedTypes,
                     std::vector<DispatchCall>* dispatchCalls = nullptr,
                     const LoanSummaries* summaries = nullptr);

// Klassens form för en metod (FunctionFacts::fieldReach m.fl., STN-55).
void classShape(const clang::CXXMethodDecl& md, const TrustList& trust, FunctionFacts& f);

// Sant om någon deklaration av funktionen (eller av templatemönstret den är
// en instans av) har [[clang::annotate("<annotation>")]].
bool hasAnnotation(const clang::FunctionDecl& fd, std::string_view annotation);

// Unified Symbol Resolution för en deklaration ("" om den inte går att ta fram).
std::string usrOf(const clang::Decl& d);

// Kvalificerat namn med templateargument, t.ex. "Box<int>::get".
std::string displayName(const clang::FunctionDecl& fd);

// Kvalificerat namn utan templateargument, som matchas mot trust-listan:
// "std::clamp", "std::optional::reset".
std::string trustName(const clang::FunctionDecl& fd);

// Kvalificerat namn med templateargument, t.ex. "Box<Holder>".
std::string recordName(const clang::CXXRecordDecl& rd);

// Gränssnittskontrakt (PORK_INTERFACE_CONTRACT): den virtuella metoden det
// gäller, eller nullptr med orsaken i `why`.
bool isInterfaceContract(const clang::FunctionDecl& fd);
const clang::CXXMethodDecl* contractedMethod(const clang::FunctionDecl& fd, std::string* why = nullptr);

// Kontraktens hjälpfunktioner (pork::pre, post, depends, depends_result):
// inga anrop i programmet, bara krav (också en mallinstans av dem).
bool isContractHelper(const clang::FunctionDecl& fd);

// PORK_DEPENDS / PORK_DEPENDS_RESULT (Bronze, spec/3_bronze.txt): anropet,
// annars nullptr. result: kravet gäller returvärdet (alla argument tillåtna).
const clang::CallExpr* flowRequirement(const clang::Stmt* s, bool* result = nullptr);

// Ett anrop till PORK_DEPENDS/PORK_DEPENDS_RESULT, också före instansiering
// (i en mall: en olöst uppslagning vars kandidater är kravfunktionerna).
bool isRequirementCall(const clang::CallExpr& c);

// BRZ-1: kraven på tillåten plats, i ordning: i kroppens kontraktsinledning
// (PORK_PRE, PORK_POST, inledande assert och PORK_DEPENDS i valfri ordning),
// som Stone, översättaren och flödet läser den.
std::vector<const clang::CallExpr*> leadingRequirements(const clang::FunctionDecl& fd);

// BRZ-6: alla PORK_DEPENDS i funktionen (kropp, konstruktorinitierare,
// lambdor, ej utvärderade uttryck), också de som inte står på tillåten plats.
// Ett defaultargument räknas inte: det står utanför kroppen (requirementsIn).
std::vector<const clang::CallExpr*> allRequirements(const clang::FunctionDecl& fd);

// Villkoret i en PORK_PRE-sats (anrop till pork::pre), annars nullptr.
const clang::Expr* preCondition(const clang::Stmt* s);

// PORK_POST(r, c) i analysläge: lambdan, annars nullptr. Villkoret c är
// lambdans enda return-uttryck (nullptr om kroppen ser annorlunda ut).
const clang::LambdaExpr* postCondition(const clang::Stmt* s);
const clang::Expr* postConditionExpr(const clang::LambdaExpr& le);

// Alternativen i en std::variant<A, B, ...>.
std::vector<clang::QualType> variantAlternatives(const clang::ClassTemplateSpecializationDecl& spec);

// operator() i en lambda för ett argument av typen `alt`: instansen av en
// generisk lambda vars första parameter är alt, annars den enda operatorn.
const clang::CXXMethodDecl* callOperatorFor(const clang::LambdaExpr& le, clang::QualType alt);

}  // namespace pork
