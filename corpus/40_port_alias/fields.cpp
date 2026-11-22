// Ett eget fält i implementationen: bara när någon annan kan ha en referens
// dit (en kontextreferens i programmet till en typ som fältet innehåller)
// eller implementationen kan anropa tillbaka (har portfält).
#include "sink.hpp"

struct Stats { int count; };
struct ILog {
  virtual ~ILog() = default;
  virtual void note(const Stats& s) = 0;
  virtual void note_doc(const Doc& d) = 0;
};

// Egna fält Stats och Doc. Ingen referens till Stats någonstans; DocSink har Doc&.
struct Store final : ILog {
  // rules: STN-52
  // expect: stone
  void note(const Stats& s) override { stats = s; }
  // rules: STN-52
  // expect: stone
  void note_doc(const Doc& d) override { last = d; }
  Stats stats;
  Doc last;
};

// rules: STN-55
// expect: stone: assuming dispatch ILog::note
void log_stats(ILog& l, const Stats& s) { l.note(s); }

// DocSink::doc_ kan vara Store::last, och d kan vara samma Doc.
// rules: STN-55
// expect: not-stone: argument 'd' may refer to a field of the implementation 'Store::note_doc' (a reference field elsewhere may refer to it)
void log_doc(ILog& l, const Doc& d) { l.note_doc(d); }

// Eget fält Text, och implementationen har en port: den kan anropa tillbaka.
struct IEcho {
  // rules: STN-50
  // expect: stone
  virtual ~IEcho() = default;
  virtual void echo(const Text& t) = 0;
};
struct Echo final : IEcho {
  // rules: STN-66, STN-51
  // expect: stone
  explicit Echo(ISink& s) : sink_(s), last{0} {}
  // rules: STN-52, STN-54
  // expect: stone: assuming dispatch ISink::put
  void echo(const Text& t) override { sink_.put(last); last = t; }
  ISink& sink_;
  Text last;
};

// Implementationerna av ISink (DocSink) når Text via doc_, och Echo har en port.
// rules: STN-55
// expect: not-stone: argument 't' may refer to a field of the implementation 'Echo::echo' (it has ports and may call back)
void echo_it(IEcho& e, const Text& t) { e.echo(t); }
