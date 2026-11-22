#include "matrix.hpp"

// --- Läsning av v[i] -----------------------------------------------------------

// flow: return <- i, v[*]
// flow oracle: ok
int r_direct(const std::vector<int>& v, int i) {
  PORK_PRE(i >= 0 && i < (int)v.size());
  return v[i];
}
// flow: return <- i, v[*]
// flow oracle: ok
int r_local_ref(const std::vector<int>& v, int i) {
  PORK_PRE(i >= 0 && i < (int)v.size());
  const int& e = v[i];
  return e;
}
// Indexet gäller vid bindningen.
// flow: return <- i, v[*]
// flow oracle: ok
int r_local_ref_later(const std::vector<int>& v, int i) {
  PORK_PRE(i >= 0 && i < (int)v.size());
  const int& e = v[i];
  i = 0;
  return e + i;
}
// flow: return <- r
// flow oracle: ok
int read_const(const int& r) { return r; }
// flow: return <- i, v[*]
// flow oracle: ok
int r_helper(const std::vector<int>& v, int i) {
  PORK_PRE(i >= 0 && i < (int)v.size());
  return read_const(v[i]);
}
// flow: return <- r
// flow oracle: ok
int read_mut(int& r) { return r; }
// flow: return <- i, v[*]
// flow oracle: ok
int r_helper_mut(std::vector<int>& v, int i) {
  PORK_PRE(i >= 0 && i < (int)v.size());
  return read_mut(v[i]);
}
// flow: return <- r
// flow oracle: ok
int read_twice(const int& r) { return read_const(r); }
// flow: return <- i, v[*]
// flow oracle: ok
int r_helper_two_levels(const std::vector<int>& v, int i) {
  PORK_PRE(i >= 0 && i < (int)v.size());
  return read_twice(v[i]);
}
// flow: return <- i, v[*]
// flow oracle: ok
int r_other_tu(const std::vector<int>& v, int i) {
  PORK_PRE(i >= 0 && i < (int)v.size());
  return read_other(v[i]);
}
// flow: return <- i, v[*].v
// flow oracle: ok
int r_field(const std::vector<Item>& v, int i) {
  PORK_PRE(i >= 0 && i < (int)v.size());
  return v[i].v;
}
// flow: return <- i, v[*]
// flow oracle: ok
int r_method(const std::vector<Item>& v, int i) {
  PORK_PRE(i >= 0 && i < (int)v.size());
  return v[i].get();
}
// flow: return <- i, v[*].v
// flow oracle: ok
int r_method_mut(std::vector<Item>& v, int i) {
  PORK_PRE(i >= 0 && i < (int)v.size());
  return v[i].get_mut();
}
// flow: return <- s.a.b
// flow oracle: ok
int r_nested(const Outer& s) { return s.a.b; }
// flow: return <- in.b
// flow oracle: ok
int read_inner(const Inner& in) { return in.b; }
// flow: return <- s.a
// flow oracle: ok
int r_nested_helper(const Outer& s) { return read_inner(s.a); }
// Range-for: varje element och antalet.
// flow: return <- v, v[*]
// flow oracle: ok
int r_range_for(const std::vector<int>& v) {
  int n = 0;
  for (const int& e : v)
    if (e > 0) n = n + 1;
  return n;
}

// --- Skrivning till v[i] -------------------------------------------------------

// flow: v[*] <- i, v[*], x (maybe written)
// flow oracle: ok
void w_direct(std::vector<int>& v, int i, int x) {
  PORK_PRE(i >= 0 && i < (int)v.size());
  v[i] = x;
}
// flow: v[*] <- i, v[*], x (maybe written)
// flow oracle: ok
void w_local_ref(std::vector<int>& v, int i, int x) {
  PORK_PRE(i >= 0 && i < (int)v.size());
  int& e = v[i];
  e = x;
}
// flow: r <- x (always written)
// flow oracle: ok
void set_value(int& r, int x) { r = x; }
// flow: v[*] <- i, v[*], x (maybe written)
// flow oracle: ok
void w_helper(std::vector<int>& v, int i, int x) {
  PORK_PRE(i >= 0 && i < (int)v.size());
  set_value(v[i], x);
}
// flow: v[*] <- i, v[*], x (maybe written)
// flow oracle: ok
void w_other_tu(std::vector<int>& v, int i, int x) {
  PORK_PRE(i >= 0 && i < (int)v.size());
  write_other(v[i], x);
}
// flow: v[*].v <- i, v[*].v, x (maybe written)
// flow oracle: ok
void w_method(std::vector<Item>& v, int i, int x) {
  PORK_PRE(i >= 0 && i < (int)v.size());
  v[i].set(x);
}
// flow: v[*].v <- i, v[*].v, x (maybe written)
// flow oracle: ok
void w_field(std::vector<Item>& v, int i, int x) {
  PORK_PRE(i >= 0 && i < (int)v.size());
  v[i].v = x;
}
// flow: a <- nothing (always written)
// flow: b <- x (always written)
// flow oracle: ok
void update(int& a, int& b, int x) {
  a = 0;
  b = x;
}
// Anropet skriver indexvariabeln; elementet valdes med det gamla i.
// flow: i <- nothing (always written)
// flow: v[*] <- i, v[*], x (maybe written)
// flow oracle: ok
void w_index_reset(std::vector<int>& v, int& i, int x) {
  PORK_PRE(i >= 0 && i < (int)v.size());
  update(i, v[i], x);
}
// flow: s.a.b <- x (always written)
// flow oracle: ok
void w_nested(Outer& s, int x) { s.a.b = x; }
// flow: in.b <- x (always written)
// flow oracle: ok
void write_inner(Inner& in, int x) { in.b = x; }
// flow: s.a.b <- x (always written)
// flow oracle: ok
void w_nested_helper(Outer& s, int x) { write_inner(s.a, x); }

// --- Styrning och garanterade skrivningar --------------------------------------

// flow: out <- c, out, x (maybe written)
// flow oracle: ok
void w_maybe(bool c, int x, int& out) {
  if (c) out = x;
}
// flow: out <- c, x, y (always written)
// flow oracle: ok
void w_both_paths(bool c, int x, int y, int& out) {
  if (c) out = x;
  else out = y;
}
// flow: return <- n
// flow oracle: ok
int loop_count(int n) {
  PORK_PRE(n >= 0 && n < 5);
  int k = 0;
  for (int j = 0; j < n; ++j) k = k + 1;
  return k;
}

// --- Satser med temporärer (ExprWithCleanups) -----------------------------------
// Hittat av oraklet i rugby: satsen föll bort ur flödet.

// flow: out <- x (always written)
// flow oracle: ok
void w_assign_temp(int x, int& out) {
  PORK_PRE(x >= 0 && x < 100);
  out = read_const(x + 1);
}
// flow: v <- v, x (maybe written)
// flow oracle: ok
void w_push_temp(std::vector<int>& v, int x) {
  PORK_PRE(x >= 0 && x < 100 && v.size() < 4);
  v.push_back(x + 1);
}
// flow: o <- o, x (maybe written)
// flow oracle: ok
void w_optional_assign(std::optional<Inner>& o, int x) { o = Inner{x}; }
