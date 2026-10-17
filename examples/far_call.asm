; Caller: CS=0, origin $0100; assemble separately with --revision working.
; Load far_callee.asm at physical $010200 and set reset vector to $000100.
MOV.W #5,r0
MOV.W #1,r1
CALLF $010200
HALT
