#include "ClangFactsSource.hpp"

#include "Assert.hpp"
#include "CTranslate.hpp"
#include "FlowBuild.hpp"
#include "LocalCheck.hpp"

#include <clang/AST/ASTConsumer.h>
#include <clang/AST/ParentMapContext.h>
#include <clang/AST/RecursiveASTVisitor.h>
#include <clang/Frontend/CompilerInstance.h>
#include <clang/Frontend/FrontendAction.h>
#include <clang/Tooling/ArgumentsAdjusters.h>
#include <clang/Tooling/Tooling.h>
#include <llvm/Support/FileSystem.h>
#include <llvm/Support/VirtualFileSystem.h>

#include <algorithm>
#include <atomic>
#include <set>
#include <thread>

using namespace clang;
using namespace clang::tooling;

namespace pork {
namespace {

class Visitor : public RecursiveASTVisitor<Visitor> {
 public:
  Visitor(ASTContext& ctx, const TrustList& trust, std::optional<TranslateOptions> translate, const LoanOptions& loans, FactsMap& facts, std::vector<StrayRequirement>& stray, bool flow)
      : ctx_(ctx), trust_(trust), translate_(translate), loans_(loans), facts_(facts), stray_(stray), flow_(flow) {}

  // Templates klassas per instans (som generics i SPARK): identity<int> och
  // identity<Vec2> är olika funktioner med egna besked.
  bool shouldVisitTemplateInstantiations() const { return true; }

  // BRZ-6: varje PORK_DEPENDS i TU:n, och de som en funktionskropp äger
  // (allRequirements, för varje definition: också mallmönster och
  // kontraktsfunktioner). Resten står utanför varje kropp (finish()).
  bool VisitCallExpr(CallExpr* c) {
    if (isRequirementCall(*c)) everyRequirement_.push_back(c);
    return true;
  }

  bool VisitFunctionDecl(FunctionDecl* fd) {
    if (fd->doesThisDeclarationHaveABody())
      for (const CallExpr* c : allRequirements(*fd)) owned_.insert(c);
    // BRZ-6: placeringen går att pröva på ett mallmönster, också om mallen
    // aldrig instansieras (flödet väntar på instanserna).
    if (fd->doesThisDeclarationHaveABody() && fd->isTemplated()) misplacedInPattern(*fd);
    if (!fd->doesThisDeclarationHaveABody() || fd->isImplicit()) return true;
    if (fd->isTemplated()) return true;  // mönstret: bara instanserna rapporteras
    if (isContractHelper(*fd)) return true;  // kontraktens hjälpfunktioner

    const SourceManager& sm = ctx_.getSourceManager();
    SourceLocation loc = sm.getExpansionLoc(fd->getLocation());
    if (sm.isInSystemHeader(loc)) return true;

    std::string usr = usrOf(*fd);
    if (usr.empty() || facts_.contains(usr)) return true;  // inline i header: redan sedd

    PresumedLoc p = sm.getPresumedLoc(loc);
    FunctionFacts f;
    f.usr = usr;
    f.name = displayName(*fd);
    f.file = p.getFilename();
    f.line = p.getLine();
    f.column = p.getColumn();
    f.required = hasAnnotation(*fd, "pork::stone");
    f.trusted = hasAnnotation(*fd, "pork::trusted");
    if (const auto* md = dyn_cast<CXXMethodDecl>(fd)) {
      std::vector<const CXXMethodDecl*> todo(md->begin_overridden_methods(), md->end_overridden_methods());
      while (!todo.empty()) {
        const CXXMethodDecl* base = todo.back();
        todo.pop_back();
        if (std::string u = usrOf(*base); !u.empty() && f.overrides.insert(u).second)
          todo.insert(todo.end(), base->begin_overridden_methods(), base->end_overridden_methods());
      }
    }
    if (isInterfaceContract(*fd))
      if (const CXXMethodDecl* m = contractedMethod(*fd)) f.contractFor = usrOf(*m);
    // Kontraktsinledningen som översättaren läser den: PORK_PRE, PORK_POST
    // och PORK_DEPENDS i valfri ordning (STN-56 ser hela inledningen).
    if (const auto* body = dyn_cast_or_null<CompoundStmt>(fd->getBody()); body != nullptr && isa<CXXMethodDecl>(fd))
      for (const Stmt* st : body->body()) {
        if (flowRequirement(st) != nullptr ||
            assertMacro(st, ctx_.getSourceManager(), ctx_.getLangOpts()) ||
            disabledAssert(st, ctx_.getSourceManager(), ctx_.getLangOpts()))
          continue;
        if (preCondition(st) == nullptr && postCondition(st) == nullptr) break;
        f.declContract = true;
        break;
      }
    // BRZ-6: varje krav i funktionen, och om något står utanför inledningen.
    if (fd->hasBody()) {
      f.declaresDepends = !allRequirements(*fd).empty();
      const FunctionDecl* pattern = fd->getTemplateInstantiationPattern();
      for (const CallExpr* c : misplacedRequirements(*fd))
        f.misplacedAt.insert(occurrenceOf(c, pattern != nullptr ? usrOf(*pattern) : std::string()));
      f.misplacedDepends = !f.misplacedAt.empty();
    }
    if (!f.trusted)  // betrodd: kroppen ignoreras
      f.local = checkFunction(*fd, trust_, f.callees, f.assumedTypes, &f.dispatchCalls, &loans_.summaries);
    if (const auto* md = dyn_cast<CXXMethodDecl>(fd); md != nullptr && md->isInstance()) classShape(*md, trust_, f);
    if (!f.trusted) {
      // Stone kräver att både reglerna och modellen godkänner (docs/stone_model.md).
      Reason model = loanContext_.check(*fd);
      if (loans_.mode == LoanMode::report) {
        f.loans = model;
        f.rulesRejected = f.local.has_value();
      }
      if (!f.local && model) f.local = "loans: " + *model;
    }
    if (translate_ && !f.trusted && !f.local) f.c = translateToC(*fd, *translate_);
    // Flödet behövs för Bronze-kraven (BRZ-5) också utan --flow.
    if (!f.trusted) f.flow = buildFlowGraph(*fd);
    facts_.emplace(usr, std::move(f));
    return true;
  }

 private:
  ASTContext& ctx_;
  const TrustList& trust_;
  std::optional<TranslateOptions> translate_;
  const LoanOptions& loans_;
 public:
  // Efter genomgången: kraven ingen kropp äger, där de står.
  void finish() {
    const SourceManager& sm = ctx_.getSourceManager();
    for (const CallExpr* c : everyRequirement_) {
      if (owned_.contains(c)) continue;
      SourceLocation loc = sm.getExpansionLoc(c->getBeginLoc());
      if (sm.isInSystemHeader(loc)) continue;
      PresumedLoc p = sm.getPresumedLoc(loc);
      stray_.push_back({p.getFilename(), p.getLine(), p.getColumn(), placeOf(*c), "is outside a function body", false,
                        occurrenceOf(c, "")});
    }
  }

 private:
  // Kraven i kroppen som inte står i kontraktsinledningen.
  static std::vector<const CallExpr*> misplacedRequirements(const FunctionDecl& fd) {
    std::vector<const CallExpr*> leading = leadingRequirements(fd), out;
    for (const CallExpr* c : allRequirements(fd))
      if (std::ranges::find(leading, c) == leading.end()) out.push_back(c);
    return out;
  }

  // Den verkliga filen (inte #line) och byteoffseten, med mallens usr först.
  std::string occurrenceOf(const CallExpr* c, const std::string& templateUsr) const {
    const SourceManager& sm = ctx_.getSourceManager();
    SourceLocation loc = sm.getExpansionLoc(c->getBeginLoc());
    std::string file;
    if (OptionalFileEntryRef fe = sm.getFileEntryRefForID(sm.getFileID(loc))) {
      file = fe->getFileEntry().tryGetRealPathName().str();
      if (file.empty()) file = fe->getName().str();
    }
    return templateUsr + '@' + file + ':' + std::to_string(sm.getFileOffset(loc));
  }

  void misplacedInPattern(const FunctionDecl& fd) {
    const SourceManager& sm = ctx_.getSourceManager();
    for (const CallExpr* c : misplacedRequirements(fd)) {
      SourceLocation loc = sm.getExpansionLoc(c->getBeginLoc());
      if (sm.isInSystemHeader(loc)) continue;
      PresumedLoc p = sm.getPresumedLoc(loc);
      stray_.push_back({p.getFilename(), p.getLine(), p.getColumn(), "the body of '" + displayName(fd) + "'",
                        "must come before all other statements", true, occurrenceOf(c, usrOf(fd))});
    }
  }

  // Den närmaste deklarationen kravet står i, för meddelandet.
  std::string placeOf(const CallExpr& c) {
    DynTypedNodeList parents = ctx_.getParents(c);
    for (int depth = 0; !parents.empty() && depth < 1000; ++depth) {
      const DynTypedNode& n = parents[0];
      if (const auto* d = n.get<Decl>()) {
        if (const auto* pd = dyn_cast<ParmVarDecl>(d)) {
          std::string of;
          if (const auto* f = dyn_cast_or_null<FunctionDecl>(pd->getDeclContext())) of = " of '" + displayName(*f) + "'";
          return "the default argument of parameter '" + pd->getNameAsString() + "'" + of;
        }
        if (const auto* fd = dyn_cast<FieldDecl>(d)) return "the initializer of field '" + fd->getQualifiedNameAsString() + "'";
        if (const auto* vd = dyn_cast<VarDecl>(d)) return "the initializer of variable '" + vd->getQualifiedNameAsString() + "'";
        if (const auto* ec = dyn_cast<EnumConstantDecl>(d)) return "the initializer of enumerator '" + ec->getQualifiedNameAsString() + "'";
        if (isa<StaticAssertDecl>(d)) return "a static_assert";
        if (const auto* nd = dyn_cast<NamedDecl>(d)) return "the declaration of '" + nd->getQualifiedNameAsString() + "'";
        return "a declaration";
      }
      parents = ctx_.getParents(n);
    }
    return "a declaration";
  }

  FactsMap& facts_;
  std::vector<StrayRequirement>& stray_;
  bool flow_;
  std::vector<const CallExpr*> everyRequirement_;
  std::set<const CallExpr*> owned_;
  LoanContext loanContext_{loans_.summaries};  // sammanfattningar inom TU:n
};

class Consumer : public ASTConsumer {
 public:
  Consumer(const TrustList& trust, std::optional<TranslateOptions> translate, const LoanOptions& loans, FactsMap& facts,
           std::vector<StrayRequirement>& stray, bool flow)
      : trust_(trust), translate_(translate), loans_(loans), facts_(facts), stray_(stray), flow_(flow) {}
  void HandleTranslationUnit(ASTContext& ctx) override {
    Visitor v(ctx, trust_, translate_, loans_, facts_, stray_, flow_);
    v.TraverseDecl(ctx.getTranslationUnitDecl());
    v.finish();
  }

 private:
  const TrustList& trust_;
  std::optional<TranslateOptions> translate_;
  const LoanOptions& loans_;
  FactsMap& facts_;
  std::vector<StrayRequirement>& stray_;
  bool flow_;
};

class Action : public ASTFrontendAction {
 public:
  Action(const TrustList& trust, std::optional<TranslateOptions> translate, const LoanOptions& loans, FactsMap& facts,
         std::vector<StrayRequirement>& stray, bool flow)
      : trust_(trust), translate_(translate), loans_(loans), facts_(facts), stray_(stray), flow_(flow) {}
  std::unique_ptr<ASTConsumer> CreateASTConsumer(CompilerInstance&, StringRef) override {
    return std::make_unique<Consumer>(trust_, translate_, loans_, facts_, stray_, flow_);
  }

 private:
  const TrustList& trust_;
  std::optional<TranslateOptions> translate_;
  const LoanOptions& loans_;
  FactsMap& facts_;
  std::vector<StrayRequirement>& stray_;
  bool flow_;
};

class Factory : public FrontendActionFactory {
 public:
  Factory(const TrustList& trust, std::optional<TranslateOptions> translate, const LoanOptions& loans, FactsMap& facts,
          std::vector<StrayRequirement>& stray, bool flow)
      : trust_(trust), translate_(translate), loans_(loans), facts_(facts), stray_(stray), flow_(flow) {}
  std::unique_ptr<FrontendAction> create() override {
    return std::make_unique<Action>(trust_, translate_, loans_, facts_, stray_, flow_);
  }

 private:
  const TrustList& trust_;
  std::optional<TranslateOptions> translate_;
  const LoanOptions& loans_;
  FactsMap& facts_;
  std::vector<StrayRequirement>& stray_;
  bool flow_;
};

}  // namespace

std::expected<FactsMap, std::string> ClangFactsSource::collect() {
  // ClangTool byter till kompileringskommandots katalog och avbryter hela
  // processen (report_fatal_error) om den saknas, t.ex. ett build-katalog som
  // inte finns i en kopia av projektet. Kontrollera först och ge ett vanligt fel.
  for (const std::string& file : files_)
    for (const CompileCommand& cc : db_.getCompileCommands(file))
      if (!llvm::sys::fs::is_directory(cc.Directory))
        return std::unexpected("compile command for " + file + " uses directory " + cc.Directory +
                               ", which does not exist");
  // En ClangTool per översättningsenhet, upp till jobs_ samtidigt. Varje TU
  // analyseras för sig (fakta per TU); fixpunkten körs först över alla.
  // Eget fysiskt filsystem per verktyg: ClangTool byter till kommandots
  // katalog, och med det delade getRealFileSystem() vore det ett chdir för
  // hela processen (som clangs AllTUsToolExecutor).
  std::vector<FactsMap> perFile(files_.size());
  std::vector<std::vector<StrayRequirement>> strayPerFile(files_.size());
  std::vector<char> failed(files_.size(), 0);
  std::atomic<std::size_t> next{0};
  auto work = [&] {
    for (std::size_t i; (i = next.fetch_add(1)) < files_.size();)
      failed[i] = runOne(files_[i], perFile[i], strayPerFile[i]) ? 0 : 1;
  };
  unsigned n = static_cast<unsigned>(std::min<std::size_t>(std::max(jobs_, 1u), files_.size()));
  if (n <= 1) {
    work();
  } else {
    std::vector<std::thread> threads;
    for (unsigned t = 0; t < n; ++t) threads.emplace_back(work);
    for (std::thread& t : threads) t.join();
  }
  if (std::ranges::any_of(failed, [](char f) { return f != 0; }))
    return std::unexpected("compilation failed, no classification produced");
  // I filordning, första TU:n vinner (en inline-funktion i en header ses i
  // flera TU:er): samma resultat som en körning i en tråd.
  FactsMap facts;
  for (FactsMap& m : perFile)
    for (auto& [usr, f] : m) facts.try_emplace(usr, std::move(f));
  // En header ses i flera TU:er (och en mall per instans): en gång per plats.
  // Sorterat: mönstret ("Tmpl::v") före instanserna ("Tmpl<int>::v").
  std::set<StrayRequirement> stray;
  for (auto& v : strayPerFile) stray.insert(v.begin(), v.end());
  // Bara identiska rader slås ihop här. Samma förekomst kan visas på flera
  // sätt (#line i olika TU:er, mönstret och instanserna): vilken som skrivs
  // ut avgör Analyse efter --only.
  stray_.assign(stray.begin(), stray.end());
  return facts;
}

bool ClangFactsSource::runOne(const std::string& file, FactsMap& facts, std::vector<StrayRequirement>& stray) const {
  ClangTool tool(db_, {file}, std::make_shared<PCHContainerOperations>(), llvm::vfs::createPhysicalFileSystem());
  // Utan detta letar LibTooling efter clangs inbyggda headers relativt
  // pork-binären och hittar inte t.ex. stddef.h.
  tool.appendArgumentsAdjuster(
      getInsertArgumentAdjuster(("-resource-dir=" + resourceDir_).c_str(),
                                ArgumentInsertPosition::BEGIN));
  // compile_commands.json från ett GCC-bygge: flaggorna för C++-modulsökning
  // (CMake lägger till dem för C++20 och senare) känner clang inte till, och
  // kompileringen avbryts. De påverkar inte analysen.
  tool.appendArgumentsAdjuster([](const CommandLineArguments& args, llvm::StringRef) {
    static constexpr llvm::StringRef kPrefixes[] = {"-fmodule-mapper=", "-fdeps-format=",
                                                    "-fdeps-file=", "-fdeps-target="};
    CommandLineArguments out;
    for (const std::string& a : args) {
      llvm::StringRef arg(a);
      if (arg == "-fmodules-ts" || arg == "-fmodule-only" ||
          llvm::any_of(kPrefixes, [&](llvm::StringRef p) { return arg.starts_with(p); }))
        continue;
      out.push_back(a);
    }
    return out;
  });
  // Varningar är inte analysfel: projektets -Werror (och varningsflaggor som
  // bara GCC har) ska inte stoppa analysen.
  tool.appendArgumentsAdjuster(getInsertArgumentAdjuster(
      CommandLineArguments{"-Wno-error", "-Wno-unknown-warning-option"}, ArgumentInsertPosition::END));
  // PORK_PRE blir ett anrop som pork ser (se include/pork/annotations.hpp).
  tool.appendArgumentsAdjuster(
      getInsertArgumentAdjuster("-DPORK_ANALYSIS", ArgumentInsertPosition::END));

  Factory factory(trust_, translate_, loans_, facts, stray, flow_);
  return tool.run(&factory) == 0;
}

}  // namespace pork
