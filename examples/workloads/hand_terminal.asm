CALL hand_terminal
MOV.W r0,[$2F00]
SWI #1
HALT
hand_terminal:
PUSH r4
PUSH r5
PUSH r6
PUSH r7
MOV.W #32,r0
MOV.W #3,r1
MOV.W #$2000,r4
SWI #2
MOV r0,r2
MOV.W #$2000,r6
MOV.W #0,r3
MOV.W #0,r0
MOV.W r0,[$2020]
MOV.W r0,[$2022]
loop:
TST.W r2
BEQ done
MOV.B [r6],r0
CMPI.B #13,r0
BEQ next
CMPI.B #10,r0
BNE column
ADDI.W #1,r3
MOV.W r3,[$2020]
MOV.W #0,r1
MOV.W r1,[$2022]
BRA letter
column:
MOV.W [$2022],r1
ADDI.W #1,r1
MOV.W r1,[$2022]
letter:
CMPI.B #97,r0
BCC emit
CMPI.B #123,r0
BCS emit
SUBI.B #32,r0
emit:
SWI #4
next:
SUBI.W #1,r2
BEQ done
ADDP #1,r6
BRA loop
done:
MOV r3,r0
POP r7
POP r6
POP r5
POP r4
RET
