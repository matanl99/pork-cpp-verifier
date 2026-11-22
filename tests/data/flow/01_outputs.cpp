// Bronze steg 1 (--flow), från idéunderlagets exempel (ej publicerat)
// (förväntat flöde i FORVANTAT.md). tests/check_flow.py.
// Utdata skrivs på samma väg, men har olika beroenden.
// flow: return <- discounted, price
// flow: audit <- price (always written)
int quote(int price, bool discounted, int& audit) {
  audit = price;
  if (discounted) return price / 2;
  return price;
}

// Samma returvärde. Bara audit får ett nytt beroende av discounted.
// flow: return <- discounted, price
// flow: audit <- discounted, price (always written)
int quote_audit_flag(int price, bool discounted, int& audit) {
  audit = discounted ? 1 : price;
  if (discounted) return price / 2;
  return price;
}

// Det gamla värdet kan överleva trots att kroppen aldrig läser det.
// flow: audit <- audit, enabled, price (maybe written)
void audit_if(int price, bool enabled, int& audit) {
  if (enabled) audit = price;
}

// Utdata är beroende av sitt ingångsvärde som ett faktiskt läsberoende.
// flow: audit <- audit, delta (always written)
void audit_add(int delta, int& audit) {
  audit += delta;
}
