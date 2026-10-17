; Preserve the measured helper's explicit error ABI; compiler /% propagates instead.
signed_divide:
TST.W r1
BEQ divide_zero
CMPI.W #$8000,r0
BNE signed_native
CMPI.W #$FFFF,r1
BEQ signed_overflow
signed_native:
MOV r0,r2
DIVS r1,r0
MODS r1,r2
MOV r2,r1
RET
signed_overflow:
MOV.W #2,r0
MOV.W r0,[$3F00]
BRA divide_error
