// pork-args: --silver
// Lokala using-deklarationer (STN-81) ändrar bara namnuppslagningen; anropet
// granskas med den funktion clang valde.
#include <algorithm>
#include <utility>

struct Value {
  int number;
  // rules: STN-81
  // expect: silver
  friend void swap(Value& a, Value& b) {
    int old = a.number;
    a.number = b.number;
    b.number = old;
  }
};

// ADL hittar Value:s egen swap.
// rules: STN-81
// expect: silver
int own_swap() {
  Value first{2};
  Value second{7};
  using std::swap;
  swap(first, second);
  return 10 * first.number + second.number;
}

// För int väljs std::swap (betrodd, utan definition i analysen).
// rules: STN-81, STN-24
// expect: stone: not silver: no contract for 'std::swap'
int std_swap(int a, int b) {
  using std::swap;
  swap(a, b);
  return a > b ? 1 : 0;
}

// rules: STN-81, STN-24
// expect: silver: assuming std::max
int directive(int a, int b) {
  using namespace std;
  return max(a, b) > 0 ? 1 : 0;
}

int counter = 0;

struct Logged {
  int number;
  // rules: STN-81
  // expect: not-stone: static storage
  friend void swap(Logged& a, Logged& b) {
    counter = counter + 1;
    int old = a.number;
    a.number = b.number;
    b.number = old;
  }
};

// rules: STN-81
// expect: not-stone: calls non-stone 'swap'
int logged_swap() {
  Logged first{2};
  Logged second{7};
  using std::swap;
  swap(first, second);
  return first.number;
}
