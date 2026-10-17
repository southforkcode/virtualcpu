u16 trace=0;i16 left(){trace=trace*10+1;return -100;}i16 right(){trace=trace*10+2;return 7;}u16 main(){i16 q=left()/right();i16 r=left()%right();return q==-14 && r==-2 && trace==1212;}
