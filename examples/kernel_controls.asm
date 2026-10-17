; Guest control library, K CS=DS=SS=0, code $7000; see ISA_EXAMPLES.md.
; Public jump slots are seven bytes. r4=request (32 pinned bytes at $6000..$67DF).
; Request: DESC/1 or entry payload at +0..16, generation at +18, query output +20..28.
; r0=status (0 OK,1 INVALID,2 BUSY,3 FAULT,4 STALE), r1=committed.
; Preserve r4-r7 and caller NZCVI; caller owns map-generation handles.
descriptor_install:
MOV.W #1,r0
JS control_call
descriptor_remove:
MOV.W #2,r0
JS control_call
map_publish:
MOV.W #3,r0
JS control_call
map_invalidate:
MOV.W #4,r0
JS control_call
entry_configure:
MOV.W #5,r0
JS control_call
backing_query:
MOV.W #6,r0
JS control_call
uaccess_begin:
MOV.W #7,r0
JS control_call
uaccess_end:
MOV.W #8,r0
JS control_call
prepare_user_return:
MOV.W #9,r0
JS control_call
map_acquire:
MOV.W #10,r0
JS control_call
map_release:
MOV.W #11,r0
JS control_call
uaccess_clear:
MOV.W #12,r0
JS control_call
initialize:
MOV.W #13,r0
JS initialize_unowned
control_abort:
JS abort_impl
descriptor_readback:
MOV.W #14,r0
JS control_call
control_call:
PUSH r4
PUSH r5
PUSH r6
PUSH r7
MOV csr,r2
PUSH r2
DI
MOV.W [$5000],r2
TST.W r2
BNE busy_unowned
GETC frame_depth,r2
CMPI.W #8,r2
BCS busy_unowned
MOV.W #1,r2
MOV.W r2,[$5000]
GETC frame_depth,r2
MOV.W r2,[$5002]
MOV.W r0,[$5004]
MOV sp,r2
MOV.W r2,[$5006]
MOV.W r4,[$500C]
MOV.W #0,r2
MOV.W r2,[$5008]
MOV.W r2,[$500A]
MOV.W r2,[$500E]
MOV.W r2,[$5010]
MOV.W r2,[$5018]
GETC desc_map,r2
MOV.W r2,[$5040]
GETC desc_segment,r2
MOV.W r2,[$5042]
GETC desc_frame,r2
MOV.W r2,[$5044]
GETC desc_flags,r2
MOV.W r2,[$5046]
GETC desc_lower,r2
MOV.W r2,[$5048]
GETC desc_upper_lo,r2
MOV.W r2,[$504A]
GETC desc_upper_hi,r2
MOV.W r2,[$504C]
GETC desc_sb,r2
MOV.W r2,[$504E]
GETC desc_st,r2
MOV.W r2,[$5050]
GETC entry_ds,r2
MOV.W r2,[$5052]
GETC entry_ss,r2
MOV.W r2,[$5054]
GETC entry_sb,r2
MOV.W r2,[$5056]
GETC entry_st,r2
MOV.W r2,[$5058]
GETC entry_sp,r2
MOV.W r2,[$505A]
GETC backing_addr_lo,r2
MOV.W r2,[$505C]
GETC backing_addr_hi,r2
MOV.W r2,[$505E]
GETC uaccess_map,r2
MOV.W r2,[$5060]
GETC uaccess_segment,r2
MOV.W r2,[$5062]
owned_start:
MOV r4,r2
CMPI.W #$6000,r2
BCC invalid
CMPI.W #$67DF,r2
BHI invalid
CMPI.W #1,r0
BEQ do_descriptor_install
CMPI.W #2,r0
BEQ do_descriptor_remove
CMPI.W #3,r0
BEQ do_map_publish
CMPI.W #4,r0
BEQ do_map_invalidate
CMPI.W #5,r0
BEQ do_entry_configure
CMPI.W #6,r0
BEQ do_backing_query
CMPI.W #7,r0
BEQ do_uaccess_begin
CMPI.W #8,r0
BEQ do_uaccess_end
CMPI.W #9,r0
BEQ do_prepare_user_return
CMPI.W #10,r0
BEQ do_map_acquire
CMPI.W #11,r0
BEQ do_map_release
CMPI.W #12,r0
BEQ do_uaccess_clear
CMPI.W #14,r0
BEQ do_descriptor_readback
JS invalid
do_descriptor_install:
CALL verify_descriptor
commit_descriptor_install:
LDC [r4],DESC
committed_descriptor_install:
JS success
end_descriptor_install:
do_descriptor_remove:
CALL verify_map
CALL verify_segment
; Zero is hoisted across all seven scalar fields; no temporary RAM image needed.
MOV.W [r4],r2
SETC r2,desc_map
MOV.W [r4+2],r2
SETC r2,desc_segment
MOV.W #0,r2
SETC r2,desc_frame
SETC r2,desc_flags
SETC r2,desc_lower
SETC r2,desc_upper_lo
SETC r2,desc_upper_hi
SETC r2,desc_sb
SETC r2,desc_st
MOV.W #1,r2
commit_descriptor_remove:
SETC r2,command
committed_descriptor_remove:
JS success
end_descriptor_remove:
do_descriptor_readback:
CALL verify_map
CALL verify_segment
commit_descriptor_readback:
STC DESC,[r4]
committed_descriptor_readback:
JS success
end_descriptor_readback:
do_map_publish:
CALL verify_map
MOV.W [r4],r2
TST.W r2
BEQ invalid
MOV.W [r5+4],r2
CMPI.W #2,r2
BEQ stale
MOV.W [r4],r2
SETC r2,desc_map
MOV.W #3,r2
commit_map_publish:
SETC r2,command
committed_map_publish:
CALL finish_publish
JS success
end_map_publish:
do_map_invalidate:
CALL verify_map
MOV.W [r4],r3
TST.W r3
BEQ invalid
MOV.W [r5+2],r2
TST.W r2
BNE busy
GETC process_map,r2
CMP r3,r2
BEQ busy
GETC uaccess_map,r2
CMP r3,r2
BEQ busy
GETC frame_depth,r2
CMPI.W #1,r2
BHI busy
BEQ check_saved_refs
BRA invalidate_unreferenced
check_saved_refs:
GETX xprocess_map,r2
CMP r3,r2
BEQ busy
GETX xuaccess_map,r2
CMP r3,r2
BEQ busy
invalidate_unreferenced:
SETC r3,desc_map
MOV.W #4,r2
commit_map_invalidate:
SETC r2,command
committed_map_invalidate:
CALL finish_invalidate
JS success
end_map_invalidate:
do_entry_configure:
MOV.W [r4+4],r2
MOV.W [r4+6],r3
OR r3,r2
TST.W r2
BNE invalid
MOV.W [r4+8],r2
CMPI.W #$C000,r2
BCC invalid
MOV.W [r4+12],r3
CMP r2,r3
BCC invalid
SUBI.W #2,r3
CMP r2,r3
BCC invalid
MOV.W [r4+10],r2
CMPI.W #$FFFE,r2
BHI invalid
MOV.W [r4+12],r3
CMP r3,r2
BCC invalid
MOV.W [r4+4],r2
SETC r2,entry_ds
MOV.W [r4+6],r2
SETC r2,entry_ss
MOV.W [r4+8],r2
SETC r2,entry_sb
MOV.W [r4+10],r2
SETC r2,entry_st
MOV.W [r4+12],r2
SETC r2,entry_sp
MOV.W #5,r2
commit_entry_configure:
SETC r2,command
committed_entry_configure:
JS success
end_entry_configure:
do_backing_query:
MOV.W [r4+2],r2
CMPI.W #255,r2
BHI invalid
MOV.W [r4],r2
SETC r2,backing_addr_lo
MOV.W [r4+2],r2
SETC r2,backing_addr_hi
MOV.W #0,r2
MOV.W r2,[r4+28]
MOV.W #8,r2
commit_backing_query:
SETC r2,command
committed_backing_query:
GETC backing_kind,r2
MOV.W r2,[r4+20]
GETC backing_run_lo,r2
MOV.W r2,[r4+22]
GETC backing_run_hi,r2
MOV.W r2,[r4+24]
GETC backing_valid,r2
MOV.W r2,[r4+26]
MOV.W #1,r2
MOV.W r2,[r4+28]
JS success
end_backing_query:
do_uaccess_begin:
CALL verify_live
CALL verify_scope
MOV.W [r4+2],r2
CMPI.W #255,r2
BHI invalid
MOV.W [r6],r2
TST.W r2
BNE busy
MOV.W [r5+2],r2
CMPI.W #$FFFF,r2
BEQ busy
MOV.W #1,r2
MOV.W r2,[$500E]
MOV.W [r4],r2
MOV.W r2,[r6+2]
MOV.W [r4+18],r2
MOV.W r2,[r6+4]
MOV.W [$5060],r2
MOV.W r2,[r6+6]
MOV.W [$5062],r2
MOV.W r2,[r6+8]
MOV.W [r5+2],r2
ADDI.W #1,r2
MOV.W r2,[r5+2]
MOV.W [r4+2],r2
SETC r2,uaccess_segment
MOV.W [r4],r2
SETC r2,uaccess_map
MOV.W #1,r2
commit_uaccess_begin:
MOV.W r2,[r6]
committed_uaccess_begin:
JS success
end_uaccess_begin:
do_uaccess_end:
CALL verify_live
CALL verify_scope
MOV.W [r6],r2
CMPI.W #1,r2
BNE invalid
MOV.W [r6+2],r2
MOV.W [r4],r3
CMP r2,r3
BNE stale
MOV.W [r6+4],r2
MOV.W [r4+18],r3
CMP r2,r3
BNE stale
MOV.W [r5+2],r2
TST.W r2
BEQ invalid
MOV.W #1,r2
MOV.W r2,[$500E]
commit_uaccess_end:
CALL finish_uaccess_end
committed_uaccess_end:
JS success
end_uaccess_end:
do_prepare_user_return:
; The validator call stack is empty here: [sp] contains saved caller flags.
MOV.W [sp+0],r2
ANDI.W #$20,r2
BNE invalid
GETC frame_depth,r2
CMPI.W #1,r2
BNE invalid
GETX xorigin_mode,r2
CMPI.W #1,r2
BNE invalid
GETX xbootstrap,r2
TST.W r2
BNE invalid
GETC entry_configured,r2
CMPI.W #1,r2
BNE invalid
MOV.W #7,r2
commit_prepare_user_return:
SETC r2,command
committed_prepare_user_return:
JS success
end_prepare_user_return:
do_map_acquire:
CALL verify_live
MOV.W [r5+2],r2
CMPI.W #$FFFF,r2
BEQ busy
MOV.W #1,r3
MOV.W r3,[$500E]
ADDI.W #1,r2
commit_map_acquire:
MOV.W r2,[r5+2]
committed_map_acquire:
JS success
end_map_acquire:
do_map_release:
CALL verify_live
MOV.W [r5+2],r2
TST.W r2
BEQ invalid
MOV.W #1,r3
MOV.W r3,[$500E]
SUBI.W #1,r2
commit_map_release:
MOV.W r2,[r5+2]
committed_map_release:
JS success
end_map_release:
do_uaccess_clear:
MOV.W #0,r2
commit_uaccess_clear:
SETC r2,uaccess_map
committed_uaccess_clear:
JS success
end_uaccess_clear:
verify_map:
MOV.W [r4],r2
CMPI.W #255,r2
BHI invalid
SHLI.W #3,r2
MOV.W #$5200,r3
ADD r2,r3
MOV r3,r5
MOV.W r3,[$5010]
MOV.W [r5],r2
MOV.W r2,[$5012]
MOV.W [r5+2],r2
MOV.W r2,[$5014]
MOV.W [r5+4],r2
MOV.W r2,[$5016]
CMPI.W #1,r2
BHI stale
MOV.W [r5],r2
MOV.W [r4+18],r3
CMP r2,r3
BNE stale
RET
verify_live:
CALL verify_map
MOV.W [r4],r2
TST.W r2
BEQ invalid
MOV.W [r5+4],r2
CMPI.W #1,r2
BNE stale
RET
verify_segment:
MOV.W [r4+2],r2
CMPI.W #255,r2
BHI invalid
MOV.W [r4],r3
TST.W r3
BNE segment_valid
TST.W r2
BEQ invalid
CMPI.W #255,r2
BEQ invalid
segment_valid:
RET
verify_scope:
GETC frame_depth,r2
SHLI.W #4,r2
MOV.W #$5A00,r3
ADD r2,r3
MOV r3,r6
MOV.W r3,[$5018]
MOV.W [r6+0],r2
MOV.W r2,[$501A]
MOV.W [r6+2],r2
MOV.W r2,[$501C]
MOV.W [r6+4],r2
MOV.W r2,[$501E]
MOV.W [r6+6],r2
MOV.W r2,[$5020]
MOV.W [r6+8],r2
MOV.W r2,[$5022]
MOV.W [r6+10],r2
MOV.W r2,[$5024]
MOV.W [r6+12],r2
MOV.W r2,[$5026]
MOV.W [r6+14],r2
MOV.W r2,[$5028]
RET
verify_descriptor:
CALL verify_map
CALL verify_segment
MOV.W [r4+4],r2
CMPI.W #255,r2
BHI invalid
MOV.W [r4+6],r2
CMPI.W #$1FF,r2
BHI invalid
ANDI.W #1,r2
BNE present_descriptor
MOV.W #0,r3
MOV.W [r4+4],r2
OR r2,r3
MOV.W [r4+6],r2
OR r2,r3
MOV.W [r4+8],r2
OR r2,r3
MOV.W [r4+10],r2
OR r2,r3
MOV.W [r4+12],r2
OR r2,r3
MOV.W [r4+14],r2
OR r2,r3
MOV.W [r4+16],r2
OR r2,r3
TST.W r3
BNE invalid
RET
present_descriptor:
MOV.W [r4+12],r2
CMPI.W #1,r2
BHI invalid
BEQ full_bound
MOV.W [r4+10],r2
MOV.W [r4+8],r3
CMP r3,r2
BCC invalid
BRA bounds_valid
full_bound:
MOV.W [r4+10],r2
TST.W r2
BNE invalid
bounds_valid:
MOV.W [r4],r2
TST.W r2
BNE descriptor_kind
MOV.W [r4+6],r2
ANDI.W #$38,r2
BNE invalid
descriptor_kind:
MOV.W [r4+6],r2
TSTI.W #2,r2
BEQ descriptor_stack
ANDI.W #$13C,r2
BNE invalid
descriptor_stack:
MOV.W [r4+6],r2
TSTI.W #4,r2
BNE stack_descriptor
MOV.W [r4+14],r2
MOV.W [r4+16],r3
OR r3,r2
BNE invalid
RET
stack_descriptor:
MOV.W [r4+14],r2
MOV.W [r4+8],r3
CMP r3,r2
BCC invalid
MOV.W [r4+16],r3
CMP r2,r3
BCC invalid
MOV.W [r4+12],r2
CMPI.W #1,r2
BEQ descriptor_valid
MOV.W [r4+10],r2
CMP r3,r2
BCC invalid
descriptor_valid:
RET
owned_end:
finish_publish:
MOV.W [$5010],r5
MOV.W [$5012],r2
TST.W r2
BNE publish_generation
MOV.W #1,r2
publish_generation:
MOV.W r2,[r5]
MOV.W [$500C],r4
MOV.W r2,[r4+18]
MOV.W #1,r2
MOV.W r2,[r5+4]
RET
finish_invalidate:
MOV.W [$5010],r5
MOV.W [$5012],r2
CMPI.W #$FFFF,r2
BEQ quarantine
ADDI.W #1,r2
MOV.W r2,[r5]
MOV.W [$500C],r4
MOV.W r2,[r4+18]
MOV.W #0,r2
MOV.W r2,[r5+4]
RET
quarantine:
MOV.W #2,r2
MOV.W r2,[r5+4]
RET
finish_uaccess_end:
MOV.W [$5010],r5
MOV.W [$5018],r6
MOV.W [$5014],r2
SUBI.W #1,r2
MOV.W r2,[r5+2]
MOV.W [$5020],r2
SETC r2,uaccess_map
MOV.W [$5022],r2
SETC r2,uaccess_segment
MOV.W #0,r2
MOV.W r2,[r6]
RET
abort_impl:
MOV.W [$5006],r2
MOV r2,sp
MOV.W [$5004],r2
CMPI.W #7,r2
BEQ abort_compensate
CMPI.W #10,r2
BEQ abort_compensate
CMPI.W #11,r2
BEQ abort_compensate
CMPI.W #8,r2
BEQ abort_end
MOV.W [$5008],r3
TST.W r3
BEQ abort_report
CMPI.W #3,r2
BEQ abort_publish
CMPI.W #4,r2
BEQ abort_invalidate
BRA abort_report
abort_publish:
CALL finish_publish
BRA abort_report
abort_invalidate:
CALL finish_invalidate
BRA abort_report
abort_end:
MOV.W [$500E],r2
TST.W r2
BEQ abort_report
CALL finish_uaccess_end
MOV.W #1,r2
MOV.W r2,[$5008]
BRA abort_report
abort_compensate:
MOV.W [$500E],r2
TST.W r2
BEQ abort_report
MOV.W [$5010],r5
MOV.W [$5014],r2
MOV.W r2,[r5+2]
MOV.W [$5060],r2
SETC r2,uaccess_map
MOV.W [$5062],r2
SETC r2,uaccess_segment
MOV.W [$5018],r6
MOV r6,r2
TST.W r2
BEQ compensation_done
MOV.W [$501A],r2
MOV.W r2,[r6+0]
MOV.W [$501C],r2
MOV.W r2,[r6+2]
MOV.W [$501E],r2
MOV.W r2,[r6+4]
MOV.W [$5020],r2
MOV.W r2,[r6+6]
MOV.W [$5022],r2
MOV.W r2,[r6+8]
MOV.W [$5024],r2
MOV.W r2,[r6+10]
MOV.W [$5026],r2
MOV.W r2,[r6+12]
MOV.W [$5028],r2
MOV.W r2,[r6+14]
compensation_done:
MOV.W #0,r2
MOV.W r2,[$5008]
abort_report:
MOV.W #3,r0
JS cleanup
success:
MOV.W #1,r2
MOV.W r2,[$5008]
MOV.W #0,r0
JS cleanup
invalid:
MOV.W #1,r0
JS cleanup
busy:
MOV.W #2,r0
JS cleanup
stale:
MOV.W #4,r0
cleanup:
MOV.W [$5006],r2
MOV r2,sp
MOV.W [$5040],r2
SETC r2,desc_map
MOV.W [$5042],r2
SETC r2,desc_segment
MOV.W [$5044],r2
SETC r2,desc_frame
MOV.W [$5046],r2
SETC r2,desc_flags
MOV.W [$5048],r2
SETC r2,desc_lower
MOV.W [$504A],r2
SETC r2,desc_upper_lo
MOV.W [$504C],r2
SETC r2,desc_upper_hi
MOV.W [$504E],r2
SETC r2,desc_sb
MOV.W [$5050],r2
SETC r2,desc_st
MOV.W [$5052],r2
SETC r2,entry_ds
MOV.W [$5054],r2
SETC r2,entry_ss
MOV.W [$5056],r2
SETC r2,entry_sb
MOV.W [$5058],r2
SETC r2,entry_st
MOV.W [$505A],r2
SETC r2,entry_sp
MOV.W [$505C],r2
SETC r2,backing_addr_lo
MOV.W [$505E],r2
SETC r2,backing_addr_hi
MOV.W [$5008],r1
MOV.W #0,r2
MOV.W r2,[$5000]
JS epilogue
busy_unowned:
MOV.W #2,r0
MOV.W #0,r1
epilogue:
POP r2
POP r7
POP r6
POP r5
POP r4
MOV r2,csr
RET

; Reset-boot-only initialization. All old software handles are discarded by boot.
initialize_unowned:
PUSH r4
PUSH r5
PUSH r6
PUSH r7
MOV csr,r2
PUSH r2
DI
GETC frame_depth,r2
TST.W r2
BNE initialize_invalid
GETC process_map,r2
TST.W r2
BNE initialize_invalid
GETC uaccess_map,r2
TST.W r2
BNE initialize_invalid
GETC entry_configured,r2
TST.W r2
BNE initialize_invalid
MOV.W [sp+0],r2
ANDI.W #$20,r2
BNE initialize_invalid
GETC desc_map,r2
MOV r2,r7
MOV.W #1,r1
initialize_check_maps:
SETC r1,desc_map
GETC map_published,r2
TST.W r2
BNE initialize_maps_live
ADDI.W #1,r1
CMPI.W #256,r1
BNE initialize_check_maps
MOV r7,r2
SETC r2,desc_map
MOV.W #$5000,r4
MOV.W #1352,r1
MOV.W #0,r2
initialize_zero:
MOV.W r2,[r4+]
SUBI.W #1,r1
BNE initialize_zero
MOV.W #0,r0
MOV.W #1,r1
JS epilogue
initialize_invalid:
MOV.W #1,r0
MOV.W #0,r1
JS epilogue
initialize_maps_live:
MOV r7,r2
SETC r2,desc_map
JS initialize_invalid
