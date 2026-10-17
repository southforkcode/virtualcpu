struct Item { u8 tag; u16 value; };
struct Item items[3] = {{1,100}, {2,200}, {3,300}};
u16 witness = 0;
u16 main() {
    u16 total = 0; u16 i = 0;
    while (i < 3) { total = total + items[i].value + items[i].tag; i = i + 1; }
    items[1].tag = 9;
    witness = items[1].value;
    return total;
}
