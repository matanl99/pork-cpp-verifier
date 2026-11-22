// Bronze steg 1 (--flow), från idéunderlagets exempel (ej publicerat)
// (förväntat flöde i FORVANTAT.md). tests/check_flow.py.
// Båda normala returvägarna skriver output, trots den tidiga returen.
// flow: output <- a, b, flag (always written)
void choose(bool flag, int a, int b, int& output) {
  if (flag) {
    output = a;
    return;
  }
  output = b;
}

// Ingen iteration om count <= 0. I det fallet lever ingångsvärdet kvar.
// flow: output <- count, output, value (maybe written)
void write_in_loop(int count, int value, int& output) {
  for (int i = 0; i < count; ++i) output = value;
}

// Den första tilldelningen slår ut beroendet av output vid ingången.
// flow: output <- count, value (always written)
void write_before_loop(int count, int value, int& output) {
  output = 0;
  for (int i = 0; i < count; ++i) output = value;
}
