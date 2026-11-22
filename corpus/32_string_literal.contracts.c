/* Kontrakt för 32_string_literal.cpp (--silver). */
void print_line__const_char_ptr(const char *s) { (void)s; }
void print_twice__const_char_ptr__int(const char *s, int n)
{
  (void)s;
  __CPROVER_assert(n >= 0, "precondition of 'print_twice': n >= 0");
}
