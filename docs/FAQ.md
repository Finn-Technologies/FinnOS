# Frequently Asked Questions

## Is FinnOS usable?

No. It boots a tested x86-64 kernel prototype but has no userspace, shell, storage, input, network, desktop, or applications.

## Does ARM64 work?

No. ARM64 has planning metadata only. See [PORTING.md](../PORTING.md).

## Is Peony implemented?

No. The visible framebuffer fill is a diagnostic. Peony is a planned compositor, shell, toolkit, input, text, accessibility, and app platform.

## Can FinnOS boot on my computer?

Physical hardware is unsupported. The only verified target is QEMU `q35` with x86-64 OVMF.

## Is FinnOS secure?

No product security claim is made. Current W^X and guard mechanisms help catch kernel faults, but all tasks are ring 0 and there is no authentication, sandbox, signed update, or capability enforcement.

## What should I work on?

Start with the [top ten tasks](../ROADMAP.md#next-10-engineering-tasks) and only claim completion when the listed acceptance and verification criteria pass.
