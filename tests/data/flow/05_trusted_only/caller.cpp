// Bara en betrodd deklaration av fill_audit: konservativt, allt kan nå audit
// och skrivningen är inte säker.
#include "provider.hpp"

// flow: return <- price
// flow: audit <- audit, price, unrelated (maybe written)
int quote_external(int price, int unrelated, int& audit) {
  fill_audit(unrelated, price, audit);
  return price;
}
