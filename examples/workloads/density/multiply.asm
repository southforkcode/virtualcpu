; r0 * r1 -> low word r0 (same signed/unsigned bits). Clobber r2/r3/NZCV.
; Fixed 16-round helper; B1 preserved, no checked-index overflow guarantee.
multiply:
MOV.W #0,r3
MOV.W #16,r2
multiply_loop:
SHRI.W #1,r0
BCC multiply_skip
ADD r1,r3
multiply_skip:
SHLI.W #1,r1
SUBI.W #1,r2
BNE multiply_loop
MOV r3,r0
RET
