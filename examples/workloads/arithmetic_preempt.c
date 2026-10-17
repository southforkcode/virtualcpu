u16 main(){u16 total=0;u16 b=7;for(u16 i=0;i<128;i=i+1){u16 a=i*31+17;total=total+a/b+a%b+a*b;if(i==64){yield();}}return total;}
