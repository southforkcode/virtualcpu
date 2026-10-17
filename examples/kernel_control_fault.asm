; Control-library recovery at K 0:$1800. Exact PC/cause registry at $A000.
; Unowned Memory faults continue to the registered copy handler at $1300.
MOV.W #0,r0
MOV r0,ds
MOV.W [$5000],r0
TST.W r0
BEQ unowned_fault
GETX xorigin_mode,r0
CMPI.W #1,r0
BNE $1400
GETX xcs,r0
TST.W r0
BNE $1400
GETX xprior_depth,r0
MOV.W [$5002],r1
CMP r1,r0
BNE $1400
GETX xpc,r0
MOV.W #$A000,r4
MOV.W [r4],r2
ADDP #2,r4
find_site:
TST.W r2
BEQ $1400
MOV.W [r4],r1
CMP r1,r0
BEQ found_site
ADDP #4,r4
SUBI.W #1,r2
BRA find_site
found_site:
MOV.W [r4+2],r3
GETX xcause,r0
CMPI.W #2,r0
BEQ memory_fault
CMPI.W #4,r0
BEQ invalid_fault
CMPI.W #3,r0
BNE $1400
TSTI.W #$80,r3
BEQ $1400
BRA recover
invalid_fault:
TSTI.W #$40,r3
BEQ $1400
BRA recover
memory_fault:
GETX xmem_origin,r0
CMPI.W #1,r0
BEQ user_return_fault
CMPI.W #2,r0
BNE $1400
GETX xmapid,r0
TST.W r0
BNE $1400
GETX xsegment,r0
TST.W r0
BNE $1400
GETX xreason,r0
CMPI.W #1,r0
BCC $1400
CMPI.W #8,r0
BHI $1400
GETX xmem_phase,r0
CMPI.W #1,r0
BEQ fetch_fault
CMPI.W #4,r0
BEQ control_target_fault
CMPI.W #2,r0
BNE $1400
TSTI.W #$20,r3
BEQ $1400
TSTI.W #$800,r3
BNE kernel_probe
GETX xwidth,r0
CMPI.W #2,r0
BNE $1400
GETX xaccess,r0
CMPI.W #2,r0
BEQ read_operand
CMPI.W #3,r0
BNE $1400
TSTI.W #$200,r3
BEQ $1400
BRA recover
read_operand:
TSTI.W #$100,r3
BEQ $1400
BRA recover
kernel_probe:
GETX xwidth,r0
TST.W r0
BEQ selector_probe
CMPI.W #1,r0
BNE $1400
GETX xaccess,r0
CMPI.W #2,r0
BEQ recover
CMPI.W #3,r0
BNE $1400
BRA recover
selector_probe:
GETX xaccess,r0
TST.W r0
BNE $1400
BRA recover
user_return_fault:
TSTI.W #$400,r3
BEQ $1400
GETX xmem_phase,r0
CMPI.W #5,r0
BEQ user_selector
CMPI.W #6,r0
BNE $1400
GETX xwidth,r0
CMPI.W #1,r0
BNE $1400
GETX xaccess,r0
CMPI.W #1,r0
BNE $1400
BRA recover
user_selector:
GETX xwidth,r0
TST.W r0
BNE $1400
GETX xaccess,r0
TST.W r0
BNE $1400
BRA recover
control_target_fault:
TSTI.W #$1000,r3
BEQ $1400
GETX xwidth,r0
CMPI.W #1,r0
BNE $1400
GETX xaccess,r0
CMPI.W #1,r0
BNE $1400
GETX xoffsetlo,r0
MOV.W #$A000,r5
MOV.W [r5],r2
ADDP #2,r5
find_target:
TST.W r2
BEQ $1400
MOV.W [r5],r1
CMP r1,r0
BEQ target_registered
ADDP #4,r5
SUBI.W #1,r2
BRA find_target
target_registered:
MOV.W [r5+2],r1
TSTI.W #$10,r1
BEQ $1400
BRA recover
fetch_fault:
TSTI.W #$10,r3
BEQ $1400
GETX xwidth,r0
CMPI.W #1,r0
BNE $1400
GETX xaccess,r0
CMPI.W #1,r0
BNE $1400
recover:
GETX xcause,r0
MOV.W r0,[$500A]
ANDI.W #1,r3
MOV.W r3,[$5008]
MOV.W #$705B,r0
SETX r0,xpc
IRET
unowned_fault:
GETX xcause,r0
CMPI.W #2,r0
BNE $1400
JS $1300
