; Current working ISA, DS=SS=CS=0, ordinary RAM; byte $80 at $4200.
MOV.W #$12FF,r0
INC.W r0
DEC.B r0
SEXT r0
MOV.W r0,[$4000]
MOV.W #$4200,r4
MOV.W #$4300,r5
MOV.B [r4+],r1
SEXT r1
MOV.W r1,[r5+]
CALLR increment
HALT
increment:
INC.W r1
RET
