# Driver Change

- [ ] Verify prerequisite discovery, IRQ, process/IPC, and DMA ownership layers exist.
- [ ] Cite public device specification and exact IDs/version.
- [ ] Define MMIO/PIO, volatile access, barriers, IRQ mask/ack/teardown, DMA bounds.
- [ ] Define timeout, reset, cancellation, removal, and crash/restart behavior.
- [ ] Test malformed device data and resource revocation in QEMU first.
- [ ] Do not claim generic or physical support from one emulated device.
- [ ] Update hardware support matrix with exact evidence.
