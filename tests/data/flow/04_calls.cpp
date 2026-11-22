// Bronze steg 1 (--flow), från idéunderlagets exempel (ej publicerat)
// (förväntat flöde i FORVANTAT.md). tests/check_flow.py.
// unused är ett argument till anropet men påverkar inget utdata.
// flow: return <- selected
int select_first(int selected, int unused) {
  (void)unused;
  return selected;
}

// Ett positionsbaserat beroende behöver översättas till rätt argument.
// flow: return <- right
int reordered(int left, int right) {
  return select_first(right, left);
}

// flow: out <- value (always written)
void set_value(int& out, int value) { out = value; }

// flow: out <- value (always written)
void forward_write(int value, int& out) {
  set_value(out, value);
}
