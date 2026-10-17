i8 low = -128;
u8 high = 255;
u16 calls = 0;
u16 bump() { calls = calls + 1; return 1; }
u16 main() {
    i16 n = -3;
    if (0 && bump()) { return 0; }
    if (1 || bump()) {
        if (low < 0 && high > 127 && n < 0) { return low + high + n; }
    }
    return 65535;
}
