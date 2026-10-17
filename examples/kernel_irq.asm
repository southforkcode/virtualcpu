; Timer source 0 handler at K CS=0:$1000; pinned I=0 replacement window.
GETX xirq_source,r0
CMPI.W #0,r0
BNE $1400
GETX xorigin_mode,r0
CMPI.W #0,r0
BNE $1400
MOV.W [$3006],r0
ADDI.W #1,r0
MOV.W r0,[$3006]
GETC process_map,r0
CMPI.W #7,r0
BEQ from_seven
MOV.W #$4100,r4
MOV.W #$4000,r5
BRA spill
from_seven:
MOV.W #$4000,r4
MOV.W #$4100,r5
spill:
STX [r4]
CALL $1600
MOV.W #$00FF,r0
MOV r0,ds
MOV.W #1,r0
MOV.W r0,[$0006]
MOV.W #0,r0
MOV r0,ds
IRET
