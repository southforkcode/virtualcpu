u16 canary = 12345;
u16 main() { u16 *p = (u16 *)0; p = p + 32768; *p = 42; return 0; }
