; Working ISA. Assemble as a flat image at $0100 in CS=0.
MOV.W #$1111,r4
MOV.W #$2222,r5
MOV.B #7,r0
CALL framed
MOV.W r0,[$2000]
MOV.W r4,[$2002]
MOV.W r5,[$2004]
HALT
framed:
PUSH r4
PUSH r5
ADJSP #-12
MOV.W r0,[sp+0]
MOV.W #$3000,r4
MOV.W #$3002,r5
MOV.W r0,[r4]
MOV.W [sp+0],r1
MOV.W r1,[r5]
ADD r1,r0
ADJSP #12
POP r5
POP r4
RET
