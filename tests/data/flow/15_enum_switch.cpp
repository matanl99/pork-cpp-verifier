// En switch över en enum med ett fall för varje uppräknat värde tar ingen
// gren för andra värden (`static_cast<E>(7)`), så utdatan kan behålla sitt
// ingångsvärde.

enum class Scoped : int { A, B };

// flow: out <- out, s (maybe written)
void covered(Scoped s, int& out) {
  switch (s) {
    case Scoped::A: out = 1; break;
    case Scoped::B: out = 2; break;
  }
}

// flow: out <- s (always written)
void with_default(Scoped s, int& out) {
  switch (s) {
    case Scoped::A: out = 1; break;
    default: out = 2; break;
  }
}

// flow: return <- s
int returns_after(Scoped s) {
  switch (s) {
    case Scoped::A: return 1;
    case Scoped::B: return 2;
  }
  return 0;
}
