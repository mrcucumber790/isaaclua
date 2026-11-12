/* Taken from isaacqol. */
/* Module header for an NSO loaded by rtld as subsdk.
   rtld relocates us and then calls DT_INIT (isaaclua_init). The entry point is never used. */

.section ".text.crt0","ax"
.global __module_start
.hidden __module_start
__module_start:
    b 1f
    .word __nx_mod0 - __module_start
1:
    b 1b

.section ".rodata.mod0","a"
.align 2
.global __nx_mod0
.hidden __nx_mod0
__nx_mod0:
    .ascii "MOD0"
    .word  __dynamic_start__      - __nx_mod0
    .word  __bss_start__          - __nx_mod0
    .word  __bss_end__            - __nx_mod0
    .word  __eh_frame_hdr_start__ - __nx_mod0
    .word  __eh_frame_hdr_end__   - __nx_mod0
    .word  qol_module_runtime     - __nx_mod0   /* rtld writes its ModuleObject (0xD0 bytes) here */

/* Storage for rtld's per-module bookkeeping. Must be writable: rtld links it into its module list. */
.section ".bss.qol_module_runtime","aw",%nobits
.align 4
.global qol_module_runtime
.hidden qol_module_runtime
qol_module_runtime:
    .space 0x100

/* Supervisor calls we need. */
.section ".text.svc","ax"

.global qol_svcQueryMemory
.hidden qol_svcQueryMemory
.type qol_svcQueryMemory, %function
qol_svcQueryMemory:
    str x1, [sp, #-16]!
    svc 0x6
    ldr x2, [sp], #16
    str w1, [x2]
    ret

.global qol_svcOutputDebugString
.hidden qol_svcOutputDebugString
.type qol_svcOutputDebugString, %function
qol_svcOutputDebugString:
    svc 0x27
    ret
