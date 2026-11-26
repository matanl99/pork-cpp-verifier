// Bronze steg 1 (--flow), från idéunderlagets exempel (ej publicerat)
// (förväntat flöde i FORVANTAT.md). tests/check_flow.py.
struct Totals {
  int total;
  int stamp;

  // Den lokala referensen är ett andra namn på this->total.
  // flow: this.total <- amount, this.total (always written)
  void add(int amount) {
    int& target = total;
    target += amount;
  }
};

// De två observerbara fälten ska ha separata beroendemängder.
// flow: result.stamp <- stamp (always written)
// flow: result.total <- total (always written)
void update_fields(Totals& result, int total, int stamp) {
  result.total = total;
  result.stamp = stamp;
}

// stamp ändras inte och totalens gamla värde skrivs över.
// flow: result.total <- total (always written)
void replace_total(Totals& result, int total) {
  result.total = total;
}

// En initierare läser ett fält som redan är initierat (STN-66 c): y får
// beroendet från x, och standardinitieraren för z det från y.
struct Chained {
  int x;
  int y;
  int z = y * 2;
  int k;
  // flow: this.k <- b (always written)
  // flow: this.x <- a (always written)
  // flow: this.y <- a (always written)
  // flow: this.z <- a (always written)
  Chained(int a, int b) : x(a), y(x + 1), k(b) {}
};
