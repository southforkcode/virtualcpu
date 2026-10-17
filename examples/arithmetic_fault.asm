; Example retry policy at K 0:$0300: repair the registered DIVU r1,r0 at U/K PC $0100.
; A real kernel chooses its arithmetic policy; unrelated faults are fatal here.
GETX xcause,r2
CMPI.W #8,r2
BNE $1400
GETX xpc,r2
CMPI.W #$0100,r2
BNE $1400
MOV.W #7,r2
SETX r2,xr1
IRET
