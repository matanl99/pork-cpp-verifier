// pork-args: --extra-arg=-Werror --extra-arg=-Wunused-variable --extra-arg=-fmodules-ts --extra-arg=-fdeps-format=p1689r5
//
// Kompileringsflaggor från projektet (compile_commands.json): -Werror ska inte
// göra varningar till analysfel, och GCC:s flaggor för modulsökning (som CMake
// lägger till för C++20 och senare i ett GCC-bygge) ska inte stoppa clang.

// rules: STN-47
// expect: stone
// expect-warning: the initialisation of 'unused' has no effect
int unused_local(int a) { int unused = 0; return a; }
