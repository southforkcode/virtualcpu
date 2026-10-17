; K 0:$0100. Owned, pinned RAM; I=0, map 0 and reset stack.
MOV.W #0,r0
SETC r0,entry_ds
SETC r0,entry_ss
SETC r0,entry_sb
MOV.W #$FFFE,r0
SETC r0,entry_st
SETC r0,entry_sp
MOV.W #5,r0
SETC r0,command

; DESC/1: map, segment, frame, flags, lower, upper_lo, upper_hi, sb, st.
MOV.W #$2000,r4
MOV.W #7,r0
MOV.W r0,[r4]
MOV.W #3,r0
MOV.W r0,[r4+2]
MOV.W r0,[r4+4]
MOV.W #$0019,r0
MOV.W r0,[r4+6]
MOV.W #0,r0
MOV.W r0,[r4+8]
MOV.W r0,[r4+10]
MOV.W r0,[r4+14]
MOV.W r0,[r4+16]
MOV.W #1,r0
MOV.W r0,[r4+12]
install:
LDC [r4],DESC

; STC reads these two header words before exporting committed state.
MOV.W #$2021,r5
MOV.W #7,r0
MOV.W r0,[r5]
MOV.W #3,r0
MOV.W r0,[r5+2]
readback:
STC DESC,[r5]

; Odd pointers are legal. Neither buffer contains CPU frame authority.
MOV.W #$4001,r4
MOV.W #$4101,r5
take_trap:
SWI #9
resume:
MOV.W r0,[$2200]
HALT
