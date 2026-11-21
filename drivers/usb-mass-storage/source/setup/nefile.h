#ifndef NUSB_NEFILE_H
#define NUSB_NEFILE_H
#include "base.h"
/* Locate an NE resource (type/id given without the 0x8000 flag). Returns FALSE if absent. */
BOOL ne_find_res(const u8 *d, u32 n, u16 type, u16 id, u32 *off, u32 *len);
/* USER.EXE with its registration stamp (RT_STRING block 33) zeroed, for checksums */
u32 user_exe_masked_crc(const u8 *d, u32 n);
/* copy the registration stamp from 'src' USER.EXE into 'dst' (same block size required) */
BOOL user_exe_copy_stamp(u8 *dst, u32 dn, const u8 *src, u32 sn);
/* write owner / organization into the fixed-width stamp fields (strings 514, 515; product ID 518
 * blanked) the way Windows 98 Setup does - for a USER.EXE built from an unstamped CD original */
BOOL user_exe_set_stamp(u8 *dst, u32 dn, const char *owner, const char *org);
#endif
