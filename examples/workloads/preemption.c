u16 values[8] = {1,2,3,4,5,6,7,8};
u16 at(u16 *p,u16 index){return p[index];}
u16 main() {
    u16 total = 0;
    for (u16 i = 0; i < 96; i = i + 1) { total = total + at(values,i & 7); }
    return total;
}
