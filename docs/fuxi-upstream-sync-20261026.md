# fuxi upstream integration — 2026-10-26

Merged UniXNU/UniXNU main ae687009348296e97e49ce6e925795044f363ce6.
Upstream rewrote history: the former upstream snapshot is fork main 8612a53's
second parent 245aca1. The nine-file fork delta was reapplied to the current
upstream tree, preserving bounded RAM-root/user-entry patches, static storage
audit opt-in, interrupted-bootstrap recovery and payload OS environment.
Canonical renamed script/component paths and format-2 configuration are retained.
Linux libsa copying and kern_exec's two RAM guard defines are preserved.

Validation: Python syntax across scripts; shell syntax; bootstrap 3 tests;
UDIF 6 tests (LZFSE present); terminal 45 tests (44 passed, one optional external
C packet fixture skipped). This is a source integration, without a new XNU
kernel, packaged system image or hardware acceptance.
