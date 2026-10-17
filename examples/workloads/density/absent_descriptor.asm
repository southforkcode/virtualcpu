; Measured alternative only: pinned private RAM record within the owned journal.
do_descriptor_remove:
CALL verify_map
CALL verify_segment
; The owned journal contains a private absent-DESC/1 image; caller record stays intact.
MOV.W #$5080,r6
MOV.W [r4],r2
MOV.W r2,[r6]
MOV.W [r4+2],r2
MOV.W r2,[r6+2]
MOV.W #0,r2
MOV.W r2,[r6+4]
MOV.W r2,[r6+6]
MOV.W r2,[r6+8]
MOV.W r2,[r6+10]
MOV.W r2,[r6+12]
MOV.W r2,[r6+14]
MOV.W r2,[r6+16]
commit_descriptor_remove:
LDC [r6],DESC
committed_descriptor_remove:
JS success
end_descriptor_remove:
