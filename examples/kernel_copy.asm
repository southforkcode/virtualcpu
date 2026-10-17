; User copy fixture at K CS=0:$1500. Registered fault PC=$150F, error PC=$1521.
GETX xdp,r4
GETX xr1,r0
SETC r0,uaccess_segment
GETC process_map,r0
SETC r0,uaccess_map
copy_fault:
MOVU.W [r4],r0
SETX r0,xr0
MOV.W #0,r0
SETX r0,xr1
SETC r0,uaccess_map
IRET
copy_error:
MOV.W #0,r0
SETX r0,xr0
MOV.W #1,r0
SETX r0,xr1
MOV.W #0,r0
SETC r0,uaccess_map
IRET
