u16 main(){u16 total=0;u16 z=0;for(u16 i=0;i<512;i=i+1){total=total+i*31;if(i==256){yield();}}return total/z;}
