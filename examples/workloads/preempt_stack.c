u16 fetch(ss u8 *p,u16 index){return p[index];}
u16 main(){
    u8 local[2]={7,9};
    u16 total=0;
    for(u16 i=0;i<256;i=i+1){total=total+fetch(local,i&1);}
    return total;
}
