struct Triple{u16 a;u16 b;u16 c;};struct Triple values[3]={{10,11,12},{20,21,22},{30,31,32}};u16 main(){u16 total=0;u16 stride=1;for(u16 i=0;i<3;i=i+1){total=total+values[i*stride].b;}return total;}
