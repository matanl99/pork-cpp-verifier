#pragma once

#include "Flow.hpp"

#include <clang/AST/Decl.h>

namespace pork {

// Bronze steg 1 (docs/bronze.md): funktionens kropp som flödesgraf för
// kärnan (core/Flow.hpp). Det som inte går att översätta sunt (lambdor,
// skrivning genom pekare eller iteratorer, destruktorer med egen kod) ger en
// graf med `unsupported` satt: flödet räknas då inte ut.
FlowGraph buildFlowGraph(const clang::FunctionDecl& fd);

}  // namespace pork
