// Virtuella funktioner och portar.
//  - Objekttyp: polymorf klass, eller klass med baser, vars baser är
//    objekttyper och fält värdetyper eller portreferenser. Får vara *this och
//    stå bakom en referens, men inte kopieras (slicing).
//  - Gränssnitt: polymorf klass utan datafält. En referens till det (I&) är
//    en port: analysen ser inget i den.
//  - Ett dispatchande anrop (virtuellt, inte avgjort statiskt) håller om varje
//    känd implementation håller; okända är antagandet "dispatch I::f".

struct Vec2 { int x; int y; };

// --- Gränssnitt och implementationer ---

struct IPort {
  // rules: STN-8, STN-50
  // expect: stone
  virtual ~IPort() = default;
  virtual void send(int v) = 0;
  virtual int peek() const = 0;
};

struct Counter : IPort {
  // rules: STN-50, STN-52
  // expect: stone
  void send(int v) override { total += v < 0 ? 0 : 1; }
  // rules: STN-50, STN-52
  // expect: stone
  int peek() const override { return total; }
  int total = 0;
};

// Anropare via porten: dispatch.
// rules: STN-51, STN-52
// expect: stone: assuming dispatch IPort::send
void forward(IPort& port, int v) { port.send(v); }

// rules: STN-51, STN-52, STN-54
// expect: stone: assuming dispatch IPort::peek
int read_port(const IPort& port) { int v = port.peek(); return v + 1; }

// Inuti ett uttryck: implementationen kan ändra det som resten läser.
// rules: STN-54, STN-20
// expect: not-stone: side effect inside expression: call to 'IPort::peek' reaches a port
int read_port_inside(const IPort& port) { return port.peek() + 1; }

// Ett dispatchande anrop som ändrar porten: samma sidoeffektsregel som andra anrop.
// rules: STN-52, STN-21
// expect: not-stone: side effect inside expression: call to 'IPort::send'
int send_inside(IPort& port, int v) { return (port.send(v), v); }

// --- Klass med en portreferens (beroendeinjektion) ---

struct Relay {
  // rules: STN-51, STN-52
  // expect: stone: assuming dispatch IPort::send
  void relay(int v) { if (v > limit) port.send(v); }
  // rules: STN-51, STN-52
  // expect: stone: assuming dispatch IPort::peek
  int status() const { int v = port.peek(); return v + limit; }
  IPort& port;
  int limit;
};

// --- Kända implementationer räknas ---

int noise = 0;

struct IClock {
  // rules: STN-8, STN-50
  // expect: stone
  virtual ~IClock() = default;
  // rules: STN-52
  // expect: stone
  virtual int now() const { return 0; }
};

struct NoisyClock : IClock {
  // rules: STN-52, STN-22
  // expect: not-stone: uses variable 'noise'
  int now() const override { return noise; }
};

// En känd override läser en global: dispatch underkänns.
// rules: STN-52
// expect: not-stone: dispatches to non-stone 'NoisyClock::now'
int elapsed(const IClock& c, int start) { int n = c.now(); return n - start; }

// Kvalificerat anrop: inte dispatch, basens egen kropp.
// rules: STN-52
// expect: stone
int base_now(const IClock& c) { return c.IClock::now(); }

struct FixedClock final : IClock {
  // rules: STN-52
  // expect: stone
  int now() const override { return 7; }
};

// final: anropet avgörs statiskt.
// rules: STN-52
// expect: stone
int fixed_now(const FixedClock& c) { return c.now(); }

// --- Det som fortfarande underkänns ---

// rules: STN-50, STN-12
// expect: not-stone: parameter 'p' has type Counter (Counter is polymorphic)
int by_value(Counter p) { return p.total; }

struct ILog {
  // rules: STN-8, STN-50
  // expect: stone
  virtual ~ILog() = default;
  virtual void log(int v) = 0;
};

int log_level = 0;
struct Logged : ILog {
  // rules: STN-68
  // expect: stone
  ~Logged() override {}
  // rules: STN-68, STN-50, STN-22
  // expect: not-stone: uses variable 'log_level' with static storage
  void log(int v) override { last = v + log_level; }
  int last = 0;
};

// Den enda kända implementationen är inte Stone.
// rules: STN-52
// expect: not-stone: dispatches to non-stone 'Logged::log'
void log_twice(ILog& l, int v) { l.log(v); l.log(v); }

// Ett pekarfält: varken värde, port eller kontextreferens (STN-53).
struct Holder {
  // rules: STN-50, STN-51
  // expect: not-stone: member of non-object type Holder (field 'v' of Holder has type Vec2 *
  int get() const { return v->x; }
  Vec2* v;
};

