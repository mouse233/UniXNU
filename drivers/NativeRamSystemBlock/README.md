# NativeRamSystemBlock

Mount-only transport for the two immutable IPSW SystemObjects received into the
owned USB RAM window. This is separate from NativeRamBlock's writable Data and
persistent overlay. There are no UFS imports, disk-content allocations, write
operations or automatic personality.

The pre-userland probe first verifies each entire actual-RAM SHA256, then calls
`fuxi_ram_system_publish` with that object's bounded APFS slice. Publication is
allowed only with `ram-system-probe=1`; the transport checks physical window
bounds, exact sector geometry, object ID and duplicate publication. It borrows
the loader memory until reboot and reports hardware write protection. Output or
bidirectional descriptors, format and cache-enable requests are refused.

Original IOBlockStorageDriver and APFS personalities must create the genuine
IOMedia/BSD volume nodes. The private mount resolver waits at most 30 seconds and
requires a real AppleAPFSVolume with exactly one `diskNs1` name, valid UUID and
BSD major/minor, read-only media, and a provider chain reaching the exact device
instance published for that verified object. Registry invalidation retries;
ambiguous matches stop. No manually assigned IOMedia/BSD identity or APFS policy
bypass is used. The two pinned containers each have only volume index zero.

`XNU_NATIVE_RAM_SYSTEM_BLOCK=1` requires the existing RAM root probe and NativeAES
storage closure; UFS, writable NativeRamBlock and Virtio block are excluded.
`build-arm-driver.py NativeRamSystemBlock` compiles against the real kernel recipe
and records source/object hashes. `audit-storage-interface.py` and
`build-native-virtio-bindings.py --component NativeRamSystemBlock` bind all 190
virtual slots and original storage imports. Static qualification does not prove
live metaclass initialization, original driver matching or APFS mounts.

The device repository's `check-usb-system-iokit.py` compiles this exact C++ source
with host IOKit dependencies and ASan/UBSan. It tests real >4 GiB end-of-window
reads, short reads, write refusal, overflow/bounds, completions, ownership,
malformed identities, ambiguous/invalidated matches and timeout. It also binds
the transport archive and unmodified original APFS/storage code to the delivered
candidate. A phone photo is still required for integration acceptance.
