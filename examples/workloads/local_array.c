u16 sum(ss u8 *p,u16 count){u16 total=0;for(u16 i=0;i<count;i=i+1){total=total+p[i];}return total;}
u16 main(){u8 local[3]={7,8,9};return sum(local,3);}
