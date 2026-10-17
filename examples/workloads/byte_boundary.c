u16 canary = 12345;
u16 main() {
    u8 *p = (u8 *)65535;
    *p = 90;
    p = p - 65535;
    *p = 33;
    return *p;
}
