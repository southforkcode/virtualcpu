u16 values[4] = {10,20,30,40};
i16 delta = 0;
u16 main() {
    u16 *p = values;
    p = p + 3;
    u16 last = *p;
    p = p - 2;
    delta = values - p;
    return last + *p + (p - values);
}
