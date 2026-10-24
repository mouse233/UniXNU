// SPDX-License-Identifier: BSD-2-Clause-Patent
// Internal, fixed-layout experimental RAM handoff. No wire execution API.
#ifndef UNIXNU_RAM_OBJECTS_H
#define UNIXNU_RAM_OBJECTS_H
#define UNIXNU_RAM_OBJECTS_GUID \
  {0xa6163e18,0x23b7,0x436a,{0x89,0xa1,0x74,0xd5,0xc8,0xf0,0x39,0x24}}
typedef unsigned long long UR64;
typedef unsigned char UR8;
#define UR_WINDOW (12ULL << 30)
#define UR_FRONT (512ULL << 20)
#define UR_OS_BYTES 6077580800ULL
#define UR_SYSTEM_BYTES 3303014400ULL
#define UR_OS_APFS_OFFSET 17408ULL
#define UR_OS_APFS_BYTES 6077546496ULL
#define UR_SYSTEM_OFFSET ((UR_FRONT + UR_OS_BYTES + 0x1fffffULL) & ~0x1fffffULL)
#define UR_RETAIN_END ((UR_SYSTEM_OFFSET + UR_SYSTEM_BYTES + 0x3fffULL) & ~0x3fffULL)
// DarwinBoot requires (Window & 2 MiB-1) == (Kernel.Base & 2 MiB-1): the bootstrap
// translation is mapped in 2 MiB blocks, so the window's physical base must be
// congruent with the kernel's link base. Kernel.Base is the lowest segment vmaddr
// of the delivered kernel.macho, which for the current prelinked kernel is
// 0xfffffff00427c000 (__PRELINK_TEXT), i.e. 0x7c000. Rebuilding the kernel can
// move the prelinked segments; check-usb-system-mount.py re-derives the value from
// the delivered payload and fails if it no longer matches this constant.
#define UR_SKEW_MASK ((UR64)0x1fffff)
#define UR_WINDOW_SKEW ((UR64)0x7c000)
typedef struct { UR64 Address, Bytes; UR8 Sha256[32]; } UR_OBJECT;
typedef struct {
  UR64 Version, Bytes, Flags, Window, WindowBytes;
  UR_OBJECT Object[2];
} UR_HANDOFF;
static inline int UrContains(UR64 base, UR64 size, UR64 address, UR64 bytes) {
  return bytes && base <= ~0ULL-size && address >= base &&
         bytes <= size && address-base <= size-bytes;
}
static inline UR64 UrBytes(unsigned id) { return id==1?UR_OS_BYTES:id==2?UR_SYSTEM_BYTES:0; }
static inline UR64 UrOffset(unsigned id) { return id==1?UR_FRONT:id==2?UR_SYSTEM_OFFSET:0; }
static inline const UR8 *UrDigest(unsigned id) {
  static const UR8 digest[2][32]={
    {0xe5,0x63,0x4d,0x04,0xa8,0xac,0x35,0xf0,0x24,0x84,0x5f,0x58,0x33,0x94,0xe0,0xa3,0x5d,0xeb,0x26,0xa0,0x4c,0xb0,0xc0,0x02,0x86,0xa8,0x67,0x4e,0x25,0xa9,0xa4,0xf2},
    {0xb4,0x90,0x8c,0xde,0x41,0xd2,0x3e,0x6b,0xc0,0x5e,0x6a,0xf5,0x1a,0x2c,0x9e,0xeb,0x00,0xcf,0x0f,0x86,0x3a,0x6a,0x18,0xb1,0x60,0x0c,0x96,0xa3,0xfd,0x70,0x41,0x4f}};
  return id>=1 && id<=2?digest[id-1]:0;
}
static inline int UrValid(const UR_HANDOFF *h) {
  if(!h || h->Version!=1 || h->Bytes!=sizeof(*h) || h->Flags!=3 ||
     h->WindowBytes!=UR_WINDOW || (h->Window&UR_SKEW_MASK)!=UR_WINDOW_SKEW ||
     !UrContains(h->Window,h->WindowBytes,h->Window,UR_RETAIN_END)) return 0;
  for(unsigned i=0;i<2;i++) {
    const UR_OBJECT *o=&h->Object[i];
    if(o->Bytes!=UrBytes(i+1) || o->Address-h->Window!=UrOffset(i+1) ||
       !UrContains(h->Window,h->WindowBytes,o->Address,o->Bytes)) return 0;
    for(unsigned j=0;j<32;j++) if(o->Sha256[j]!=UrDigest(i+1)[j]) return 0;
  }
  return UR_FRONT+UR_OS_BYTES<=UR_SYSTEM_OFFSET && UR_RETAIN_END<UR_WINDOW;
}
#endif
