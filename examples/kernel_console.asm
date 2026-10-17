; console-v1 kernel. Pinned at $8000; PCB and request ABI in ISA_EXAMPLES.
irq:
GETX xirq_source,r0
CMPI.W #2,r0
BEQ rx_irq
CMPI.W #0,r0
BNE $1400
MOV.W #$00FF,r0
MOV r0,ds
MOV.W #1,r0
MOV.W r0,[$0006]
MOV.W #0,r0
MOV r0,ds
GETX xorigin_mode,r0
TST.W r0
BNE nested_timer
JS schedule
nested_timer:
MOV.W #1,r0
MOV.W r0,[$3030]
IRET
rx_irq:
MOV.W #$4000,r7
CALL service_if_blocked
MOV.W #$4100,r7
CALL service_if_blocked
MOV.W #$00FF,r0
MOV r0,ds
MOV.W #4,r0
MOV.W r0,[$0006]
MOV.W #0,r0
MOV r0,ds
CALL rx_enable
; Skip a not-yet-executed HALT if input arrived in the EI/HALT window.
GETX xorigin_mode,r0
TST.W r0
BEQ rx_return
GETX xpc,r0
CMPI.W #33748,r0
BNE rx_return
MOV.W #33749,r0
SETX r0,xpc
rx_return:
IRET
rx_enable:
MOV.W [$402A],r0
CMPI.W #2,r0
BEQ need_rx
MOV.W [$412A],r0
CMPI.W #2,r0
BEQ need_rx
MOV.W #1,r1
BRA rx_mask
need_rx:
MOV.W #5,r1
rx_mask:
MOV.W #$00FF,r0
MOV r0,ds
MOV.W r1,[$0002]
MOV.W #0,r0
MOV r0,ds
RET
swi:
GETX xorigin_mode,r0
TST.W r0
BNE bootstrap
GETX xtrap,r0
CMPI.W #1,r0
BEQ finish
CMPI.W #2,r0
BEQ read
CMPI.W #3,r0
BEQ cancel
CMPI.W #4,r0
BEQ write
CMPI.W #5,r0
BEQ resume
JS $1400
resume:
IRET
bootstrap:
GETX xtrap,r0
TST.W r0
BNE $1400
MOV.W #$4000,r5
CALL $1600
MOV.W #$6000,r4
CALL $7038
TST.W r0
BNE $1400
IRET
write:
GETX xr0,r1
ANDI.W #$00FF,r1
MOV.W #$00FF,r0
MOV r0,ds
MOV.W r1,[$0082]
MOV.W #0,r0
MOV r0,ds
IRET
read:
CALL current
MOV r7,r0
MOV r0,r4
CALL save
GETX xr0,r0
CMPI.W #32,r0
BHI read_invalid
MOV.W r0,[r7+44]
GETX xr4,r0
MOV.W r0,[r7+46]
GETX xr1,r0
MOV.W r0,[r7+48]
MOV.W #0,r0
MOV.W r0,[r7+50]
MOV.W r0,[r7+52]
MOV.W #1,r0
MOV.W r0,[r7+54]
MOV.W [r7+44],r0
TST.W r0
BEQ read_zero
MOV.W [r7+48],r0
CMPI.W #3,r0
BNE read_invalid
CALL request_handle
CALL $703F
TST.W r0
BNE $1400
MOV.W #1,r0
MOV.W r0,[r7+56]
MOV.W #2,r0
MOV.W r0,[r7+42]
CALL service_if_blocked
MOV.W [r7+42],r0
CMPI.W #2,r0
BNE read_return
MOV.W #$00FF,r0
MOV r0,ds
MOV.W #5,r0
MOV.W r0,[$0002]
MOV.W #0,r0
MOV r0,ds
MOV.W [$300a],r0
ADDI.W #1,r0
MOV.W r0,[$300a]
JS schedule
read_invalid:
MOV.W #1,r0
SETX r0,xr1
MOV.W #0,r0
MOV.W r0,[r7+54]
SETX r0,xr0
IRET
read_zero:
MOV.W #0,r0
MOV.W r0,[r7+0]
MOV.W r0,[r7+2]
MOV.W r0,[r7+54]
read_return:
MOV r7,r0
MOV r0,r5
CALL validate
TST.W r0
BNE $1400
CALL $1600
IRET
current:
GETC process_map,r0
CMPI.W #7,r0
BEQ current_a
MOV.W #$4100,r7
RET
current_a:
MOV.W #$4000,r7
RET
save:
GETX xr0,r0
MOV.W r0,[r4+0]
GETX xr1,r0
MOV.W r0,[r4+2]
GETX xr2,r0
MOV.W r0,[r4+4]
GETX xr3,r0
MOV.W r0,[r4+6]
GETX xr4,r0
MOV.W r0,[r4+8]
GETX xr5,r0
MOV.W r0,[r4+10]
GETX xr6,r0
MOV.W r0,[r4+12]
GETX xr7,r0
MOV.W r0,[r4+14]
GETX xpc,r0
MOV.W r0,[r4+16]
GETX xcs,r0
MOV.W r0,[r4+18]
GETX xcsr,r0
MOV.W r0,[r4+20]
GETX xsp,r0
MOV.W r0,[r4+22]
GETX xsb,r0
MOV.W r0,[r4+24]
GETX xst,r0
MOV.W r0,[r4+26]
GETX xss,r0
MOV.W r0,[r4+28]
GETX xdp,r0
MOV.W r0,[r4+30]
GETX xds,r0
MOV.W r0,[r4+32]
GETX xprocess_map,r0
MOV.W r0,[r4+34]
GETX xuaccess_map,r0
MOV.W r0,[r4+36]
GETX xuaccess_segment,r0
MOV.W r0,[r4+38]
RET
; A request owns a generation lease until exactly one completion/cancellation.
request_handle:
MOV.W #$6100,r4
MOV.W [r7+34],r0
MOV.W r0,[r4]
MOV.W [r7+40],r0
MOV.W r0,[r4+4]
RET
service_if_blocked:
MOV.W [r7+42],r0
CMPI.W #2,r0
BNE service_return
MOV.W #$00FF,r0
MOV r0,ds
MOV.W [$0086],r1
MOV.W #0,r0
MOV r0,ds
TST.W r1
BEQ service_return
; An additional scoped lease validates the request owner/generation on every copy.
CALL request_handle
MOV.W [r7+48],r0
MOV.W r0,[r4+2]
CALL $702A
TST.W r0
BNE begin_failed
MOV.W [r7+46],r6
copy_loop:
MOV.W #$00FF,r0
MOV r0,ds
MOV.W [$0086],r1
TST.W r1
BEQ drained
MOV.W [$0080],r0
MOV r0,r2
MOV.W #0,r0
MOV r0,ds
MOV.W r2,[$3040]
MOV r2,r0
copy_fault:
MOVU.B r0,[r6]
MOV.W [r7+50],r0
ADDI.W #1,r0
MOV.W r0,[r7+50]
MOV.W [r7+44],r1
CMP r1,r0
BEQ copy_done
; Avoid an ADDP overflow: let the next byte access fault at the boundary sentinel.
MOV r6,r0
CMPI.W #$FFFF,r0
BEQ boundary_fault
ADDP #1,r6
BRA copy_loop
boundary_fault:
MOV.W #1,r0
MOV.W r0,[r7+52]
BRA copy_done
 drained:
MOV.W #0,r0
MOV r0,ds
copy_done:
CALL request_handle
CALL $7031
TST.W r0
BNE $1400
BRA complete
copy_error:
MOV.W #1,r0
MOV.W r0,[r7+52]
BRA copy_done
begin_failed:
MOV.W #1,r0
MOV.W r0,[r7+52]
complete:
CALL request_handle
CALL $7046
TST.W r0
BNE $1400
MOV.W #0,r0
MOV.W r0,[r7+56]
MOV.W r0,[r7+54]
MOV.W [r7+50],r0
MOV.W r0,[r7+0]
MOV.W [r7+52],r0
MOV.W r0,[r7+2]
MOV.W #1,r0
MOV.W r0,[r7+42]
MOV.W [$300c],r0
ADDI.W #1,r0
MOV.W r0,[$300c]
service_return:
RET
; Only a top-level user frame may be replaced. Calls fully return before IRET.
schedule:
GETX xorigin_mode,r0
TST.W r0
BNE $1400
GETX xframe_depth,r0
CMPI.W #1,r0
BNE $1400
MOV sp,r0
CMPI.W #$FFFE,r0
BNE $1400
CALL current
MOV r7,r0
MOV r0,r4
CALL save
MOV.W #$4200,r4
CALL save
schedule_choose:
CALL current
MOV.W #$4000,r5
MOV r7,r0
CMPI.W #$4000,r0
BNE candidate
MOV.W #$4100,r5
candidate:
MOV.W [r5+42],r0
CMPI.W #1,r0
BEQ switch
MOV r7,r0
MOV r0,r5
MOV.W [r5+42],r0
CMPI.W #1,r0
BEQ switch
; No runnable process: retain one U-origin frame, no suspended CALL chain.
EI
idle:
HALT
idle_resume:
DI
MOV.W #0,r0
MOV.W r0,[$3030]
BRA schedule_choose
switch:
CALL validate
TST.W r0
BNE rollback
CALL $1600
MOV.W [$3006],r0
ADDI.W #1,r0
MOV.W r0,[$3006]
IRET
rollback:
; The candidate has not been installed. Return the independently saved image.
MOV.W #$4200,r5
CALL $1600
IRET
; Fixed two-process ABI: immutable boot maps/backing; bound every mutable field.
validate:
GETX xframe_depth,r0
CMPI.W #1,r0
BNE invalid_candidate
MOV.W [r5+34],r0
CMPI.W #7,r0
BEQ validate_a
CMPI.W #8,r0
BNE invalid_candidate
MOV.W #$5240,r6
BRA validate_lease
validate_a:
MOV.W #$5238,r6
validate_lease:
MOV.W [r6],r0
MOV.W [r5+40],r1
CMP r1,r0
BNE invalid_candidate
MOV.W [r6+2],r0
TST.W r0
BEQ invalid_candidate
MOV.W [r6+4],r0
CMPI.W #1,r0
BNE invalid_candidate
MOV.W [r5+18],r0
CMPI.W #1,r0
BNE invalid_candidate
MOV.W [r5+24],r0
CMPI.W #4096,r0
BNE invalid_candidate
MOV.W [r5+26],r0
CMPI.W #8192,r0
BNE invalid_candidate
MOV.W [r5+28],r0
CMPI.W #2,r0
BNE invalid_candidate
MOV.W [r5+32],r0
CMPI.W #3,r0
BNE invalid_candidate
MOV.W [r5+36],r0
CMPI.W #0,r0
BNE invalid_candidate
MOV.W [r5+38],r0
CMPI.W #0,r0
BNE invalid_candidate
MOV.W [r5+16],r0
CMPI.W #$0100,r0
BCC invalid_candidate
CMPI.W #$0200,r0
BCS invalid_candidate
MOV.W [r5+22],r0
CMPI.W #$1000,r0
BCC invalid_candidate
CMPI.W #$2000,r0
BHI invalid_candidate
MOV.W [r5+20],r0
ANDI.W #$FFD0,r0
TST.W r0
BNE invalid_candidate
MOV.W #0,r0
RET
invalid_candidate:
MOV.W #1,r0
RET
; Cancel A from B: DYING prevents late IRQ completion before leases are drained.
cancel:
GETC process_map,r0
CMPI.W #8,r0
BNE $1400
MOV.W #$4000,r7
MOV.W #3,r0
MOV.W r0,[r7+42]
MOV.W [r7+56],r0
TST.W r0
BEQ cancel_pcb
CALL request_handle
CALL $7046
TST.W r0
BNE $1400
MOV.W #0,r0
MOV.W r0,[r7+56]
MOV.W r0,[r7+54]
MOV.W #2,r0
MOV.W r0,[r7+52]
cancel_pcb:
CALL request_handle
CALL $7046
TST.W r0
BNE $1400
CALL $7015
TST.W r0
BNE $1400
; Rebind selector 3 to successor backing before publishing generation 2.
MOV.W #3,r0
MOV.W r0,[r4+2]
MOV.W #7,r0
MOV.W r0,[r4+6]
MOV.W #$19,r0
MOV.W r0,[r4+8]
MOV.W #0,r0
MOV.W r0,[r4+10]
MOV.W r0,[r4+12]
MOV.W r0,[r4+16]
MOV.W r0,[r4+18]
MOV.W #1,r0
MOV.W r0,[r4+14]
CALL $7000
TST.W r0
BNE $1400
CALL $700E
TST.W r0
BNE $1400
MOV.W #1,r0
MOV.W r0,[$3020]
; The cancelled PCB still names generation 1 and must fail before access.
CALL request_handle
CALL $703F
CMPI.W #4,r0
BNE $1400
MOV.W r0,[$3022]
CALL rx_enable
IRET
finish:
; Shutdown cancels any peer read before retaining the two PCB diagnostic leases.
MOV.W #$4000,r7
CALL shutdown_request
MOV.W #$4100,r7
CALL shutdown_request
MOV.W #$00FF,r0
MOV r0,ds
MOV.W #3,r0
MOV.W r0,[$0020]
MOV.W #0,r0
MOV.W r0,[$0002]
MOV r0,ds
finished:
HALT
shutdown_request:
MOV.W [r7+56],r0
TST.W r0
BEQ shutdown_return
CALL request_handle
CALL $7046
TST.W r0
BNE $1400
MOV.W #0,r0
MOV.W r0,[r7+56]
MOV.W r0,[r7+54]
MOV.W r0,[r7+0]
MOV.W #2,r0
MOV.W r0,[r7+52]
MOV.W r0,[r7+2]
MOV.W #1,r0
MOV.W r0,[r7+42]
MOV.W [$3024],r0
ADDI.W #1,r0
MOV.W r0,[$3024]
shutdown_return:
RET
