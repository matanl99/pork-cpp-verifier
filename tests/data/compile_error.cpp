// Ett kompileringsfel (okänd typ) ger en ofullständig AST: pork ska avsluta
// med "compilation failed" och status 2, inte analysera den (och krascha).
#include <vector>
bool parse(const std::vector<unsigned char>& in, void* sax);
bool f() {
  std::vector<unsigned char> input(4, 0x9F);
  Unknown accept_all(100);
  return parse(input, &accept_all);
}
