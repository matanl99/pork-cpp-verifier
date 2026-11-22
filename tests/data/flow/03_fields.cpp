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
