// En värdeinitierad array av pekare i ett aggregat (`return {};` för en
// struct med `int* v[2]`): arrayfyllaren är en implicit nod utan plats i
// källan. Den har bara nollpekare, så lånemodellen ser inga lån, och domen
// kommer från reglerna (STN-9: returtypen är ingen värdetyp).

struct PointerArray {
  int* value[2];
};

struct PointerRecord {
  int* value;
};

// rules: STN-9, STN-62
// expect: not-stone: return type PointerArray
PointerArray pointer_array() { return {}; }

// rules: STN-9, STN-62
// expect: not-stone: return type PointerRecord
PointerRecord pointer_record() { return {}; }
