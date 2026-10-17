; K 0:$0500. Admit only the deliberate Invalid State from the registered LDX.
GETX xcause,r0
CMPI.W #4,r0
BNE $1400
GETX xpc,r0
CMPI.W #$0700,r0
BNE $1400
GETX xcs,r0
TST.W r0
BNE $1400
GETX xorigin_mode,r0
CMPI.W #1,r0
BNE $1400
GETX xframe_depth,r0
CMPI.W #2,r0
BNE $1400
MOV.W [$2202],r0
TST.W r0
BNE $1400
; The lower frame was never changed. Repair the candidate and retry LDX.
MOV.W #0,r0
MOV.W r0,[r5+20]
MOV.W #1,r0
MOV.W r0,[$2202]
IRET
