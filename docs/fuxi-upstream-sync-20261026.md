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

## feat/ramdisk-test compatibility

The complete RAM system/user/main/cache/trust observation patches remain on
this branch. DarwinBoot stays pinned to the fork's da5b43b, preserving its
RAM-object handoff; the shared uncommitted patch is not included. Canonical
upstream driver/kit pins coexist with the old pinned Native* dependencies
and NativePrelink for restored standalone candidate builders. Legacy AES and
Virtio script entry points delegate to the canonical scripts. New format-2
configuration/ordered patches remain available. Historical v47/v48 build and
hardware reports are not revalidated by this source merge.
