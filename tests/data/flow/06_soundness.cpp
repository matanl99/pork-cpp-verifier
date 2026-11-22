// Konstruktioner där ett fel i flödet vore ett falskt "beror inte på".
#include <algorithm>
#include <vector>

struct P { int x; int y; };

// Ett element: svag skrivning, och indexet avgör vilket element.
// flow: v[*] <- i, v[*], x (maybe written)
void set_elem(std::vector<int>& v, int i, int x) { v[i] = x; }

// flow: return <- v
int sum(const std::vector<int>& v) { int s = 0; for (int e : v) s += e; return s; }

// flow: v <- v, x (maybe written)
void push(std::vector<int>& v, int x) { v.push_back(x); }

// Två lokaler som heter x är olika platser.
// flow: return <- nothing
// flow: out <- a (always written)
int shadow(int a, int b, int& out) { int x = a; { int x = b; (void)x; } out = x; return 0; }

// Hela p skrivs efter fältet: bara en rad.
// flow: p <- q (always written)
void field_then_whole(P& p, int a, const P& q) { p.x = a; p = q; }

// Nästlade villkor: båda styr.
// flow: out <- a, c, d, out (maybe written)
void nested(bool c, bool d, int a, int& out) { if (c) { if (d) out = a; } }

// flow: return <- a, b, c
int ternary(bool c, int a, int b) { return c ? a : b; }

// flow: return <- a, b, c, d
int andand(bool c, bool d, int a, int b) { if (c && d) return a; return b; }

// En lokal referens är ett andra namn på fältet.
// flow: p.y <- p.y, x (always written)
void via_local_ref(P& p, int x) { int& r = p.y; r += x; }

// Skrivning genom iteratorer syns inte som en plats: inte uträknat.
// flow: not computed: call of 'std::sort' can write through an iterator, pointer or view argument
void sorted(std::vector<int>& v) { std::sort(v.begin(), v.end()); }

// En lokal lambda: fångsten per värde (tests/data/flow/10_lambda.cpp).
// flow: return <- a
int with_lambda(int a) { auto f = [a] { return a; }; return f(); }
