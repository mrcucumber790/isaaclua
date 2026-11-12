OUTPUT_FORMAT(elf64-littleaarch64)
OUTPUT_ARCH(aarch64)
ENTRY(__module_start)

PHDRS
{
  text PT_LOAD FLAGS(5);
  rodata PT_LOAD FLAGS(4);
  data PT_LOAD FLAGS(6);
  dynamic PT_DYNAMIC;
}

SECTIONS
{
  . = 0;
  .text : {
    KEEP (*(.text.crt0))
    *(.text .text.*)
  } :text
  .plt : { *(.plt .plt.*) } :text

  . = ALIGN(0x1000);
  .rodata : {
    KEEP (*(.rodata.mod0))
    *(.rodata .rodata.*)
  } :rodata
  .hash     : { *(.hash) } :rodata
  .gnu.hash : { *(.gnu.hash) } :rodata
  .dynsym   : { *(.dynsym .dynsym.*) } :rodata
  .dynstr   : { *(.dynstr .dynstr.*) } :rodata
  .rela.dyn : { *(.rela.dyn) } :rodata
  .rela.plt : { *(.rela.plt) } :rodata
  .eh_frame_hdr : {
    HIDDEN(__eh_frame_hdr_start__ = .);
    *(.eh_frame_hdr)
    HIDDEN(__eh_frame_hdr_end__ = .);
  } :rodata
  .eh_frame : { KEEP (*(.eh_frame)) } :rodata
  .note.gnu.build-id : { *(.note.gnu.build-id) } :rodata

  . = ALIGN(0x1000);
  .data : { *(.data .data.*) } :data
  .data.rel.ro : { *(.data.rel.ro.local*) *(.data.rel.ro .data.rel.ro.*) } :data
  .got : { *(.got.plt) *(.igot.plt) *(.got) *(.igot) } :data
  .dynamic : {
    HIDDEN(__dynamic_start__ = .);
    *(.dynamic)
  } :data :dynamic
  .init_array : { KEEP (*(.init_array*)) } :data
  .fini_array : { KEEP (*(.fini_array*)) } :data

  . = ALIGN(0x1000);
  .bss : {
    HIDDEN(__bss_start__ = .);
    *(.bss .bss.*)
    *(COMMON)
    . = ALIGN(8);
    HIDDEN(__bss_end__ = .);
    . = ALIGN(0x1000);
  } :data

  /DISCARD/ : { *(.interp) *(.comment) }
}
