u8 bytes[4] = {1, 2, 127, 255};
u16 sum(u8 *p, u16 count) {
    u16 i = 0; u16 total = 0;
    while (i < count) { total = total + p[i]; i = i + 1; }
    return total;
}
u16 main() { return sum(bytes, 4); }
