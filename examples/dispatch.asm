; Working ISA. Assemble as a flat image at $0100 in CS=0.
MOV.W #$0300,r4
MOV.W [cs:r4],r5
JS r5
MOV.W #$DEAD,r0
HALT
target:
MOV.W #$BEEF,r0
MOV.W r0,[$2000]
HALT
