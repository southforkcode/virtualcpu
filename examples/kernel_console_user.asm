; A: up-to-32 terminal read; uppercase ASCII, omit CR and count newline.
read:
MOV.W #$5678,r3
MOV.W #$ABCD,r5
MOV.W #$DCBA,r6
MOV.W #$2468,r7
MOV.W #$00FF,r2
ADDI.B #1,r2
MOV.W [$2100],r0
MOV.W [$2104],r1
MOV.W [$2102],r4
SWI #2
MOV.W r0,[$2020]
MOV.W r1,[$2022]
; Read preserves NZCV and all registers except the two result registers.
BCC bad
BNE bad
CMPI.W #0,r2
BNE bad
CMPI.W #$5678,r3
BNE bad
MOV r5,r0
CMPI.W #$ABCD,r0
BNE bad
MOV r6,r0
CMPI.W #$DCBA,r0
BNE bad
MOV r7,r0
CMPI.W #$2468,r0
BNE bad
MOV r4,r0
MOV.W [$2102],r1
CMP r1,r0
BNE bad
MOV.W #1,r0
MOV.W r0,[$2028]
MOV.W [$2020],r2
MOV.W #1,r0
MOV.W r0,[$2024]
MOV.W [$2102],r6
TST.W r2
BEQ done
loop:
MOV.B [r6],r0
CMPI.B #$0D,r0
BEQ next
CMPI.B #$0A,r0
BNE letter
MOV.W [$2026],r1
ADDI.W #1,r1
MOV.W r1,[$2026]
letter:
CMPI.B #$61,r0
BCC emit
CMPI.B #$7B,r0
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
SWI #1
HALT
bad:
SWI #255
HALT
