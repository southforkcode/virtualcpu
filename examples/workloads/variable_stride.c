u8 bytes[16] = {0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15};
u16 gather(u8 *p, u16 first, u16 stride) {
    u16 i = 0; u16 total = 0;
    while (i < 4) { total = total + p[first + i * stride]; i = i + 1; }
    return total;
}
u16 main() { return gather(bytes,1,3); }
