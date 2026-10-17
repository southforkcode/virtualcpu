u16 canary = 12345;
u16 main() { u8 *p = (u8 *)0; u16 index = 40000; u16 stride = 2; p[index * stride] = 42; return 0; }
