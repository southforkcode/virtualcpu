u16 words[5] = {1, 256, 32768, 65535, 7};
u16 main() {
    u16 total = 0;
    for (u16 i = 0; i < 5; i = i + 1) { total = total + words[i]; }
    return total;
}
