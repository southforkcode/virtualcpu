struct Pair { u16 first; u16 second; };
u16 kernel_helper(u16 seed, u16 increment) {
    struct Pair local = {0,0};
    local.first = seed;
    local.second = increment;
    ss struct Pair *p = &local;
    return p->first + p->second;
}
