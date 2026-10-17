; Validated top-frame load at $1600. r5 points to CTX/1; r0=0/1 status.
; Recovery admits only this LDX in an outermost U-origin replacement window.
LDX [r5]
MOV.W #0,r0
load_return:
RET
