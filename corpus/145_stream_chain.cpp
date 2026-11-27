// En lång kedja `out << a << b << ...`: objektet i varje led är föregående
// anrop. Lånemodellen frågar efter samma ställen genom flera operander, och
// utan att ta bort dubbletterna växte svaret tiofalt per led (kedjan nedan tog
// då längre tid än universums ålder).
// Båda avvisas (std::ostream har virtuella baser), men lånemodellen körs ändå.
#include <ostream>

// rules: STN-50
// expect: not-stone: has virtual base classes
void show(std::ostream& out, int a, int b) {
  out << a << ':' << b << ':' << a << ':' << b << ':' << a << ':' << b << ':' << a << ':' << b << ':' << a << ':'
      << b << ':' << a << ':' << b << ':' << a << ':' << b << ':' << a << ':' << b << ':' << a << ':' << b << '\n';
}

// rules: STN-50
// expect: not-stone: has virtual base classes
void labels(std::ostream& out, const char (&name)[4]) {
  out << name << '=' << name << ',' << name << '=' << name << ',' << name << '=' << name << ',' << name << '='
      << name << ',' << name << '=' << name << ',' << name << '=' << name << ',' << name << '=' << name << '\n';
}
