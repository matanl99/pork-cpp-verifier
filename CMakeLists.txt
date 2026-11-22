cmake_minimum_required(VERSION 3.28)
project(pork VERSION 0.1.0 LANGUAGES C CXX)  # C: LLVMConfig kör check_include_file

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)

# ASan/UBSan för hela bygget (preset debug-asan, ./cmasan.sh).
option(ENABLE_SANITIZERS "Enable Address/UB sanitizers" OFF)
if(ENABLE_SANITIZERS)
  add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=all)
  add_link_options(-fsanitize=address,undefined)
endif()

# --- Kärna: ren logik, inga clang-beroenden ---
# summaries/std.txt byggs in i binären (lånemodellens std-sammanfattningar).
configure_file(summaries/std.txt ${CMAKE_CURRENT_BINARY_DIR}/generated/std.txt COPYONLY)
file(READ ${CMAKE_CURRENT_SOURCE_DIR}/summaries/std.txt PORK_STD_SUMMARIES)
configure_file(core/StdSummaries.cpp.in ${CMAKE_CURRENT_BINARY_DIR}/generated/StdSummaries.cpp @ONLY)

add_library(pork_core STATIC
  core/Classify.cpp
  core/Analyse.cpp
  core/Silver.cpp
  core/LoanSummaries.cpp
  core/Flow.cpp
  core/UnionCheckProver.cpp
  ${CMAKE_CURRENT_BINARY_DIR}/generated/StdSummaries.cpp)
target_include_directories(pork_core PUBLIC core)
target_compile_options(pork_core PRIVATE -Wall -Wextra -Wpedantic)

# --- Utgående adapter: textrapport ---
add_library(pork_text STATIC adapters/text/TextReport.cpp adapters/text/TrustFile.cpp)
target_include_directories(pork_text PUBLIC adapters/text)
target_link_libraries(pork_text PUBLIC pork_core)
target_compile_options(pork_text PRIVATE -Wall -Wextra -Wpedantic)

# --- Inkommande adapter: clang/LibTooling ---
find_package(Clang REQUIRED CONFIG)

# Verktyget måste hitta clangs inbyggda headers (stddef.h m.fl.) när det
# analyserar kod som inkluderar standardbiblioteket.
execute_process(
  COMMAND clang -print-resource-dir
  OUTPUT_VARIABLE PORK_CLANG_RESOURCE_DIR
  OUTPUT_STRIP_TRAILING_WHITESPACE
  COMMAND_ERROR_IS_FATAL ANY)

add_library(pork_clang STATIC
  adapters/clang/LocalCheck.cpp
  adapters/clang/LoanCheck.cpp
  adapters/clang/CTranslate.cpp
  adapters/clang/FlowBuild.cpp
  adapters/clang/ClangFactsSource.cpp)
target_include_directories(pork_clang PUBLIC adapters/clang)
target_include_directories(pork_clang SYSTEM PUBLIC ${CLANG_INCLUDE_DIRS} ${LLVM_INCLUDE_DIRS})
target_link_libraries(pork_clang PUBLIC pork_core clang-cpp LLVM)
target_compile_options(pork_clang PRIVATE -Wall -Wextra -Wpedantic)
if(NOT LLVM_ENABLE_RTTI)
  target_compile_options(pork_clang PRIVATE -fno-rtti)
endif()

# --- Gemensamt för bevisare som körs som egen process (cbmc, esbmc) ---
add_library(pork_process STATIC adapters/process/ProverProcess.cpp)
target_include_directories(pork_process PUBLIC adapters/process)
target_include_directories(pork_process SYSTEM PUBLIC ${LLVM_INCLUDE_DIRS})
target_link_libraries(pork_process PUBLIC pork_core PRIVATE LLVM)
target_compile_options(pork_process PRIVATE -Wall -Wextra -Wpedantic)
if(NOT LLVM_ENABLE_RTTI)
  target_compile_options(pork_process PRIVATE -fno-rtti)
endif()

# --- Utgående adapter: CBMC (Silver), extern process ---
add_library(pork_cbmc STATIC adapters/cbmc/CbmcProver.cpp)
target_include_directories(pork_cbmc PUBLIC adapters/cbmc)
target_include_directories(pork_cbmc SYSTEM PRIVATE ${LLVM_INCLUDE_DIRS})
target_link_libraries(pork_cbmc PUBLIC pork_core PRIVATE pork_process LLVM)
target_compile_options(pork_cbmc PRIVATE -Wall -Wextra -Wpedantic)
if(NOT LLVM_ENABLE_RTTI)
  target_compile_options(pork_cbmc PRIVATE -fno-rtti)
endif()

# --- Utgående adapter: ESBMC (Silver), extern process ---
add_library(pork_esbmc STATIC adapters/esbmc/EsbmcProver.cpp)
target_include_directories(pork_esbmc PUBLIC adapters/esbmc)
target_include_directories(pork_esbmc SYSTEM PRIVATE ${LLVM_INCLUDE_DIRS})
target_link_libraries(pork_esbmc PUBLIC pork_core PRIVATE pork_process LLVM)
target_compile_options(pork_esbmc PRIVATE -Wall -Wextra -Wpedantic)
if(NOT LLVM_ENABLE_RTTI)
  target_compile_options(pork_esbmc PRIVATE -fno-rtti)
endif()

# --- Adapter runt en bevisare: svaren sparas på disk ---
add_library(pork_cache STATIC adapters/cache/CachingProver.cpp)
target_include_directories(pork_cache PUBLIC adapters/cache)
target_include_directories(pork_cache SYSTEM PRIVATE ${LLVM_INCLUDE_DIRS})
target_link_libraries(pork_cache PUBLIC pork_core PRIVATE LLVM)
target_compile_options(pork_cache PRIVATE -Wall -Wextra -Wpedantic)
if(NOT LLVM_ENABLE_RTTI)
  target_compile_options(pork_cache PRIVATE -fno-rtti)
endif()

# --- Kompositionsrot ---
add_executable(pork app/main.cpp)
target_link_libraries(pork PRIVATE pork_clang pork_cbmc pork_esbmc pork_cache pork_text)
target_compile_definitions(pork PRIVATE PORK_CLANG_RESOURCE_DIR="${PORK_CLANG_RESOURCE_DIR}"
  PORK_VERSION="${PROJECT_VERSION}"
  PORK_CONTRACTS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/contracts")
target_compile_options(pork PRIVATE -Wall -Wextra -Wpedantic)

# --- Tester ---
enable_testing()

find_package(GTest REQUIRED)
add_executable(pork_unit_tests
  tests/unit/ClassifyTest.cpp
  tests/unit/LoanSummariesTest.cpp
  tests/unit/FlowTest.cpp
  tests/unit/AnalyseTest.cpp
  tests/unit/CachingProverTest.cpp
  tests/unit/CbmcProverTest.cpp
  tests/unit/UnionCheckProverTest.cpp
  tests/unit/EsbmcProverTest.cpp
  tests/unit/SilverTest.cpp
  tests/unit/TextReportTest.cpp
  tests/unit/TrustFileTest.cpp)
target_link_libraries(pork_unit_tests PRIVATE pork_core pork_text pork_cache pork_cbmc pork_esbmc LLVM GTest::gtest_main)
target_include_directories(pork_unit_tests SYSTEM PRIVATE ${LLVM_INCLUDE_DIRS})
include(GoogleTest)
gtest_discover_tests(pork_unit_tests)

find_package(Python3 REQUIRED COMPONENTS Interpreter)
add_test(NAME corpus
  COMMAND Python3::Interpreter -I ${CMAKE_CURRENT_SOURCE_DIR}/tests/check_corpus.py
          $<TARGET_FILE:pork> ${CMAKE_CURRENT_SOURCE_DIR}/corpus)
# Regelkatalogen (spec/): korpusens `// rules:` får bara nämna regler som finns.
add_test(NAME corpus_rules
  COMMAND Python3::Interpreter -I ${CMAKE_CURRENT_SOURCE_DIR}/tests/check_corpus.py
          --rules ${CMAKE_CURRENT_SOURCE_DIR}/corpus ${CMAKE_CURRENT_SOURCE_DIR}/spec)
# Samma korpus med ESBMC som bevisare (Silver-fallen hoppas över utan esbmc).
add_test(NAME corpus_loans
  COMMAND Python3::Interpreter -I ${CMAKE_CURRENT_SOURCE_DIR}/tests/check_loans.py
          $<TARGET_FILE:pork> ${CMAKE_CURRENT_SOURCE_DIR}/corpus)

# --jobs: TU:er analyseras parallellt, domarna ska vara desamma som i en tråd.
add_test(NAME jobs_same_verdicts
  COMMAND Python3::Interpreter -I ${CMAKE_CURRENT_SOURCE_DIR}/tests/check_jobs.py
          $<TARGET_FILE:pork> ${CMAKE_CURRENT_SOURCE_DIR}/corpus)

add_test(NAME corpus_esbmc
  COMMAND Python3::Interpreter -I ${CMAKE_CURRENT_SOURCE_DIR}/tests/check_corpus.py
          $<TARGET_FILE:pork> ${CMAKE_CURRENT_SOURCE_DIR}/corpus --silver-prover=esbmc)

# Körningsorakel (P-e): korpusfall med körningar (#ifdef PORK_RUN) körs med
# ASan/UBSan/_GLIBCXX_DEBUG och MSan. Stone + larm = hål.
add_test(NAME corpus_runtime
  COMMAND Python3::Interpreter -I ${CMAKE_CURRENT_SOURCE_DIR}/tests/check_runtime.py
          $<TARGET_FILE:pork> ${CMAKE_CURRENT_SOURCE_DIR}/corpus --jobs 4)
# Självtest: med en felaktig rad (push_back ogiltigförklarar inte) ska ett hål hittas.
add_test(NAME corpus_runtime_selftest
  COMMAND Python3::Interpreter -I ${CMAKE_CURRENT_SOURCE_DIR}/tests/check_runtime.py
          $<TARGET_FILE:pork> ${CMAKE_CURRENT_SOURCE_DIR}/tests/data/runtime
          --summaries ${CMAKE_CURRENT_SOURCE_DIR}/tests/data/override.summaries)
set_tests_properties(corpus_runtime_selftest PROPERTIES PASS_REGULAR_EXPRESSION "HOLE 1,")
# Självtest: ett körningsnamn som passar två överlagringar, och ett program som
# kraschar vid listningen, fäller testet (granskning 1).
add_test(NAME corpus_runtime_ambiguous
  COMMAND Python3::Interpreter -I ${CMAKE_CURRENT_SOURCE_DIR}/tests/check_runtime.py
          $<TARGET_FILE:pork> ${CMAKE_CURRENT_SOURCE_DIR}/tests/data/runtime_ambiguous)
set_tests_properties(corpus_runtime_ambiguous PROPERTIES PASS_REGULAR_EXPRESSION "names functions on lines 4, 5")
# --flow (Bronze steg 1, docs/bronze.md): härlett flöde mot `// flow:`-rader.
add_test(NAME flow
  COMMAND Python3::Interpreter -I ${CMAKE_CURRENT_SOURCE_DIR}/tests/check_flow.py
          $<TARGET_FILE:pork> ${CMAKE_CURRENT_SOURCE_DIR}/tests/data/flow)
# Beroendeoraklet (docs/bronze.md steg 3): försöker motbevisa --flow med två
# körningar per utdata i Silvers C (hoppas över utan bevisaren). Självtestet
# tappar ett beroende per utdata: då ska hålen synas.
add_test(NAME flow_oracle
  COMMAND Python3::Interpreter -I ${CMAKE_CURRENT_SOURCE_DIR}/tests/check_flow.py
          $<TARGET_FILE:pork> ${CMAKE_CURRENT_SOURCE_DIR}/tests/data/flow --oracle)
add_test(NAME flow_oracle_esbmc
  COMMAND Python3::Interpreter -I ${CMAKE_CURRENT_SOURCE_DIR}/tests/check_flow.py
          $<TARGET_FILE:pork> ${CMAKE_CURRENT_SOURCE_DIR}/tests/data/flow --oracle --silver-prover=esbmc)
add_test(NAME flow_oracle_selftest
  COMMAND Python3::Interpreter -I ${CMAKE_CURRENT_SOURCE_DIR}/tests/check_flow.py
          $<TARGET_FILE:pork> ${CMAKE_CURRENT_SOURCE_DIR}/tests/data/flow --oracle --selftest)
set_tests_properties(flow_oracle_selftest PROPERTIES FAIL_REGULAR_EXPRESSION "HOLE 0$")
# Constexpr-orakel: korpusens körningar (och PORK_CONSTEXPR_CASE) utvärderas av
# kompilatorn i en kopia där funktionerna är constexpr. Stone + UB = hål.
add_test(NAME corpus_constexpr
  COMMAND Python3::Interpreter -I ${CMAKE_CURRENT_SOURCE_DIR}/tests/check_constexpr.py
          $<TARGET_FILE:pork> ${CMAKE_CURRENT_SOURCE_DIR}/corpus --jobs 4)
add_test(NAME corpus_constexpr_selftest
  COMMAND Python3::Interpreter -I ${CMAKE_CURRENT_SOURCE_DIR}/tests/check_constexpr.py
          $<TARGET_FILE:pork> ${CMAKE_CURRENT_SOURCE_DIR}/tests/data/constexpr
          --summaries ${CMAKE_CURRENT_SOURCE_DIR}/tests/data/override.summaries)
set_tests_properties(corpus_constexpr_selftest PROPERTIES PASS_REGULAR_EXPRESSION "HOLE 1, caught 0, runtime 1,")
# Självtest: en kompilator som dör utan diagnos är ett verktygsfel, inte ok (granskning).
add_test(NAME corpus_constexpr_killed
  COMMAND ${CMAKE_COMMAND} -E env PORK_CONSTEXPR_CLANG=${CMAKE_CURRENT_SOURCE_DIR}/tests/data/fake_clang_killed.sh
          ${Python3_EXECUTABLE} -I ${CMAKE_CURRENT_SOURCE_DIR}/tests/check_constexpr.py
          $<TARGET_FILE:pork> ${CMAKE_CURRENT_SOURCE_DIR}/tests/data/constexpr_cannot)
set_tests_properties(corpus_constexpr_killed PROPERTIES PASS_REGULAR_EXPRESSION "without a diagnostic")
add_test(NAME corpus_constexpr_cannot
  COMMAND Python3::Interpreter -I ${CMAKE_CURRENT_SOURCE_DIR}/tests/check_constexpr.py
          $<TARGET_FILE:pork> ${CMAKE_CURRENT_SOURCE_DIR}/tests/data/constexpr_cannot)
set_tests_properties(corpus_constexpr_cannot PROPERTIES PASS_REGULAR_EXPRESSION "cannot evaluate")
add_test(NAME corpus_runtime_listing
  COMMAND Python3::Interpreter -I ${CMAKE_CURRENT_SOURCE_DIR}/tests/check_runtime.py
          $<TARGET_FILE:pork> ${CMAKE_CURRENT_SOURCE_DIR}/tests/data/runtime_listing)
set_tests_properties(corpus_runtime_listing PROPERTIES PASS_REGULAR_EXPRESSION "listing the runs failed")

# --summaries: en felaktig fil stoppar körningen med rad och orsak, en
# giltig fil ersätter inbyggda regler.
add_test(NAME summaries_bad_file
  COMMAND pork --loans=report --summaries=${CMAKE_CURRENT_SOURCE_DIR}/tests/data/bad.summaries
          -p ${CMAKE_CURRENT_SOURCE_DIR}/corpus ${CMAKE_CURRENT_SOURCE_DIR}/corpus/51_loan_model.cpp)
set_tests_properties(summaries_bad_file PROPERTIES
  PASS_REGULAR_EXPRESSION "bad.summaries:2: unknown effect 'read'")
add_test(NAME summaries_override
  COMMAND pork --loans=report --summaries=${CMAKE_CURRENT_SOURCE_DIR}/tests/data/override.summaries
          -p ${CMAKE_CURRENT_SOURCE_DIR}/corpus ${CMAKE_CURRENT_SOURCE_DIR}/corpus/51_loan_model.cpp)
set_tests_properties(summaries_override PROPERTIES
  PASS_REGULAR_EXPRESSION "vector_push_breaks[^\n]*\n  loans: ok")

# std-sammanfattningarna mot körning: genererade program "låna, operation,
# använd" granskas av pork och körs med -D_GLIBCXX_DEBUG + ASan/UBSan. pork
# ok men körningen larmar = osund rad i summaries/std.txt (eller checkern).
# Tar några minuter: `ctest -LE runtime` hoppar över.
add_test(NAME summaries_runtime
  COMMAND Python3::Interpreter -I ${CMAKE_CURRENT_SOURCE_DIR}/tests/check_summaries.py
          $<TARGET_FILE:pork> ${CMAKE_CURRENT_BINARY_DIR}/summaries_runtime --jobs 16)
# Självtest: med en medvetet felaktig regel måste osunda fall hittas.
add_test(NAME summaries_runtime_selftest
  COMMAND Python3::Interpreter -I ${CMAKE_CURRENT_SOURCE_DIR}/tests/check_summaries.py
          $<TARGET_FILE:pork> ${CMAKE_CURRENT_BINARY_DIR}/summaries_runtime_selftest --jobs 4
          --only vector.push_back --summaries ${CMAKE_CURRENT_SOURCE_DIR}/tests/data/override.summaries)
set_tests_properties(summaries_runtime summaries_runtime_selftest PROPERTIES LABELS runtime)
set_tests_properties(summaries_runtime_selftest PROPERTIES PASS_REGULAR_EXPRESSION "unsound [1-9]")
