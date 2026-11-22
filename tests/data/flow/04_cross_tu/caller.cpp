// Anroparen i en annan TU än fill_audit: sammanfattningen följer med.
#include "provider.hpp"

// flow: return <- price
// flow: audit <- price (always written)
int quote_external(int price, int unrelated, int& audit) {
  fill_audit(unrelated, price, audit);
  return price;
}
