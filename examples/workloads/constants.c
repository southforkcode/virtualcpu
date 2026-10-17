u16 narrow=(u8)256+1;
i16 negative=(i16)65535>>1;
u16 Mixed(){return 3;}
u16 mixed(){return 4;}
u16 main(){return narrow+(u16)(negative+2)+Mixed()+mixed();}
