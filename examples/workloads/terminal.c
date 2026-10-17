u8 buffer[32];
u16 lines = 0;
u16 columns = 0;
u16 main() {
    u16 count = read(buffer,32);
    if (io_status() != 0) { return 65535; }
    u16 i = 0;
    while (i < count) {
        u8 ch = buffer[i];
        if (ch != 13) {
            if (ch == 10) { lines = lines + 1; columns = 0; }
            else { columns = columns + 1; }
            if (ch >= 97 && ch <= 122) { ch = ch - 32; }
            putc(ch);
        }
        i = i + 1;
    }
    return lines;
}
