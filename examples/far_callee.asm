; Callee: CS=1, origin $0200; assemble separately at origin $0200.
; A near helper uses RET; this far entry returns with RETF.
CALL helper
RETF
helper:
ADD r1,r0
RET
