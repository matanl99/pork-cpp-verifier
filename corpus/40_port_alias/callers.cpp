// Referensargument till ett dispatchande anrop: implementationen DocSink (i
// impl.cpp) håller Doc&, så en Text-parameter kan vara doc_.title.
#include "sink.hpp"

// rules: STN-55
// expect: not-stone: argument 't' may refer to the same object as reference field 'DocSink::doc_' of the implementation 'DocSink::put'
void send(ISink& s, const Text& t) { s.put(t); }

// rules: STN-55
// expect: not-stone: argument 't' may refer to the same object as reference field 'DocSink::doc_' of the implementation 'DocSink::fix'
void fix_it(ISink& s, Text& t) { s.fix(t); }

// En temporär eller en lokal variabel kan implementationen inte nå.
// rules: STN-55
// expect: stone: assuming dispatch ISink::put
void send_temp(ISink& s) { s.put(Text{1}); }

// rules: STN-55
// expect: stone: assuming dispatch ISink::fix, dispatch ISink::put
void send_local(ISink& s) { Text t{2}; s.put(t); s.fix(t); }

// Ett värde: en kopia.
// rules: STN-55
// expect: stone: assuming dispatch ISink::put
void send_copy(ISink& s, Text t) { s.put(t); }
