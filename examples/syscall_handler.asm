; Working ISA. Assemble as a flat image at $0200 in CS=1.
; Service 1 adds saved r0+r1, returns status r1=0; other services return r0=0/r1=1.
; Requires configured protected entry and a valid saved return state.
GETX xtrap,r2
CMPI.W #1,r2
BNE unsupported
GETX xr0,r0
GETX xr1,r1
ADD r1,r0
SETX r0,xr0
XOR r1,r1
SETX r1,xr1
IRET
unsupported:
XOR r0,r0
MOV.B #1,r1
SETX r0,xr0
SETX r1,xr1
IRET
