u16 mix(u16 a,u16 b,u16 c) {
    u16 d = a + b; u16 e = b + c; u16 f = a ^ c;
    u16 g = d + e; u16 h = f + c; u16 j = g ^ h;
    return j + a + b + c;
}
u16 main() { return mix(10,mix(2,3,4),mix(5,6,7)) + mix(8,9,10); }
