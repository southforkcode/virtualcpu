struct Pair { u8 tag; u16 value; };
u16 inspect(ss struct Pair *p) { return p->value + p->tag; }
u16 main() { struct Pair local = {7,1234}; return inspect(&local); }
