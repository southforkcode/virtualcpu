u16 canary = 12345;
u16 main() { u8 *p = (u8 *)0; p = p - 1; *p = 42; return 0; }
