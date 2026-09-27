#ifndef _OBJFMT_H_
#define _OBJFMT_H_

// VM32 object files, which vmasm -c writes and vmld links, all little-endian:
//   "VMOB", u16 version, u16 flags, u32 entry offset in the text section
//   u32 text size, u32 text alignment, u32 data size, u32 data alignment, text bytes, data bytes
//   u32 symbol count, then for each: string name, u8 kind, u8 global, u32 value, u32 line, string file
//   u32 relocation count, then for each: u8 section, u8 type, u32 offset, u32 target, u32 addend
//   u32 line count, then for each: u8 section, u32 offset, u32 line, string text, string file
// Strings carry a 16-bit length. Code and data symbols hold offsets into their section.
#define VMO_MAGIC       "VMOB"
#define VMO_VERSION     1
#define VMO_HAS_ENTRY   1

enum { VMO_TEXT, VMO_DATA };

enum { VMO_CODE, VMO_DATA_SYMBOL, VMO_CONST, VMO_EXTERN };

// ABS32 stores target + addend; REL32 stores it minus the address after the patched word, as jumps take it
enum { VMO_ABS32, VMO_REL32 };

// A relocation targets the text or data section of its own file, or VMO_SYMBOL plus a symbol index
enum { VMO_TARGET_TEXT, VMO_TARGET_DATA, VMO_SYMBOL };

#endif // _OBJFMT_H_
