#!/usr/bin/env python3
"""Execute the real XNU bootstrap mapping macros on an arm64 macOS host.

Uses heap pages as synthetic page tables; never installs them in the MMU.
The old and new pool sizes execute the same macros extracted from start.s.
"""
import argparse
import json
import platform
import re
import subprocess
import tempfile
from pathlib import Path

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--xnu-work', type=Path, required=True)
p.add_argument('--report', type=Path)
a = p.parse_args()
if platform.system() != 'Darwin' or platform.machine() != 'arm64':
    p.error('this execution test requires an arm64 macOS host')
source = (a.xnu_work / 'osfmk/arm64/start.s').read_text()
macros = []
for name in ['create_l1_table_entry', 'create_l2_block_entries', 'create_bootstrap_mapping']:
    macros.append(re.search(r'(?m)^\.macro ' + name + r'\b.*?^\.endmacro', source, re.S).group())
if 'b.hs Lbootstrap_tables_exhausted' not in macros[-1]:
    raise SystemExit('the production bounds guard is missing')
patch = (Path(__file__).resolve().parents[1] / 'patches/xnu/native-4k-bootstrap-capacity.patch').read_text()
new_pages = int(re.search(r'^\+#define BOOTSTRAP_TABLE_SIZE \(ARM_PGBYTES \* (\d+)\)', patch, re.M)[1])
# 4 KiB, three-level translation geometry. The descriptor flags do not affect
# capacity; representative valid flags keep this fixture independent of MAIR.
constants = '''
.equ PGBYTES, 4096
.equ TTE_PGENTRIES, 512
.equ TTE_SHIFT, 3
.equ ARM_TT_L1_INDEX_MASK, 0x7fc0000000
.equ ARM_TT_L1_SHIFT, 30
.equ ARM_TT_L2_INDEX_MASK, 0x3fe00000
.equ ARM_TT_L2_SHIFT, 21
.equ ARM_TTE_BLOCK_L2_SHIFT, 21
.equ ARM_TT_L2_SIZE, 0x200000
.equ ARM_TTE_TABLE_MASK, 0x0000fffffffff000
.equ ARM_TTE_BLOCK_L2_MASK, 0x0000ffffffe00000
.equ ARM_TTE_BOOT_TABLE, 3
.equ ARM_TTE_BOOT_BLOCK, 0x701
'''
wrapper = '''
.text
.globl _bootstrap{pages}
_bootstrap{pages}:
  stp x25, x30, [sp, #-16]!
  mov x25, x3
  mov x4, x0
  mov x14, x1
  mov x15, x0
  lsr x5, x2, #21
  mov x16, x5
  mov x1, x3
  add x3, x1, #4096
  mov x2, x3
  create_bootstrap_mapping x0, x4, x5, x1, x2, x6, x10, x11, x12, x13
  mov x5, x16
  create_bootstrap_mapping x14, x15, x5, x3, x2, x9, x10, x11, x12, x13
  sub x0, x2, x25
  lsr x0, x0, #12
  add x0, x0, #1
  b Ldone
Lbootstrap_tables_exhausted:
  mov x0, #-1
Ldone:
  ldp x25, x30, [sp], #16
  ret
'''
driver = r'''
#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
typedef unsigned long long u64;
extern u64 bootstrap16(u64,u64,u64,void *);
extern u64 bootstrapNEW(u64,u64,u64,void *);
typedef u64 (*mapping)(u64,u64,u64,void *);
static void run(mapping fn, unsigned pages, u64 pa, u64 va, u64 size, u64 expected) {
  unsigned char *pool = aligned_alloc(4096, (pages + 1) * 4096);
  assert(pool); memset(pool, 0, pages * 4096);
  memset(pool + pages * 4096, 0xa5, 4096);
  u64 count = fn(pa, va, size, pool);
  assert(count == expected);
  for (unsigned i = 0; i < 4096; ++i) assert(pool[pages * 4096 + i] == 0xa5);
  free(pool);
}
int main(void) {
  const u64 pa=0xa00004000ULL, va=0xfffffff007004000ULL;
  run(bootstrap16,16,pa,va,6ULL<<30,15);
  run(bootstrap16,16,pa,va,12ULL<<30,~0ULL);
  run(bootstrapNEW,NEW,pa,va,12ULL<<30,27);
  run(bootstrapNEW,NEW,pa,va,16ULL<<30,35);
  run(bootstrapNEW,NEW,0xa3fe04000ULL,0xfffffff03fe04000ULL,16ULL<<30,36);
  run(bootstrapNEW,NEW,pa,va,18ULL<<30,~0ULL);
  puts("PASS: real ARM64 mapping macros; old 12 GiB exhaustion, new 12/16 GiB, worst alignment and pool canaries");
}
'''.replace('NEW', str(new_pages))
with tempfile.TemporaryDirectory(prefix='bootstrap-capacity-') as directory:
    work = Path(directory)
    objects = []
    for pages in [16, new_pages]:
        asm, obj = work / f'pool{pages}.s', work / f'pool{pages}.o'
        asm.write_text(constants + f'\n.equ BOOTSTRAP_TABLE_BYTES, {pages * 4096}\n' +
                       '\n'.join(macros) + wrapper.format(pages=pages))
        subprocess.run(['clang', '-c', str(asm), '-o', str(obj)], check=True)
        objects.append(str(obj))
    c = work / 'test.c'
    c.write_text(driver)
    subprocess.run(['clang', '-Wall', '-Wextra', '-Werror', str(c), *objects, '-o', str(work / 'test')], check=True)
    subprocess.run([str(work / 'test')], check=True)
report = dict(production_mapping_macros_executed=True, old_pool_pages=16, new_pool_pages=new_pages,
              cases=6, required_pages_12g=27, required_pages_16g_worst_alignment=36,
              canary_pages_intact=True, device_result='not tested by this host fixture')
if a.report:
    a.report.write_text(json.dumps(report, indent=2) + '\n')
print(json.dumps(report, indent=2))
