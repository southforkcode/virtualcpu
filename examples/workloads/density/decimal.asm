; Parse exactly five ASCII decimal digits at $4000; checked uint16 result.
; status 3 = invalid digit/overflow. Preserves B1; clobbers B0/NZCV.
parse_decimal:
PUSH r4
PUSH r5
MOV.W #$4000,r4
MOV.W #5,r5
MOV.W #0,r0
parse_loop:
MOV.B [r4],r1
SUBI.W #48,r1
CMPI.W #9,r1
BHI parse_bad
CMPI.W #6553,r0
BHI parse_bad
BNE parse_scale
CMPI.W #5,r1
BHI parse_bad
parse_scale:
MOV r0,r2
SHLI.W #3,r0
SHLI.W #1,r2
ADD r2,r0
ADD r1,r0
ADDP #1,r4
MOV r5,r2
SUBI.W #1,r2
MOV r2,r5
BNE parse_loop
POP r5
POP r4
RET
parse_bad:
MOV.W #3,r0
MOV.W r0,[$3F00]
MOV.W #0,r0
POP r5
POP r4
RET
; Format uint16 r0 into exactly five zero-padded digits at $4100.
format_decimal:
PUSH r4
PUSH r5
MOV.W #$4104,r4
MOV.W #5,r5
format_loop:
MOV.W #10,r1
CALL divide
ADDI.W #48,r1
MOV.B r1,[r4]
ADDP #-1,r4
MOV r5,r2
SUBI.W #1,r2
MOV r2,r5
BNE format_loop
POP r5
POP r4
RET
