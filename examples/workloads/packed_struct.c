struct Record {u8 a;u8 b;u8 c;u16 value;};
struct Record records[2]={{1,2,3,1000},{4,5,6,2000}};
u16 main(){u16 total=0;for(u16 i=0;i<2;i=i+1){total=total+records[i].value+records[i].c;}return total;}
