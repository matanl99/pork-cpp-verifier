#include "provider.hpp"

// flow: audit <- price (always written)
void fill_audit(int, int price, int& audit) {
  audit = price;
}
