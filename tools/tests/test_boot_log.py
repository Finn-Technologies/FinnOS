import unittest
from pathlib import Path
import re

from tools.finnlib.qemu import ARM64_EXCEPTION_MARKERS, ARM64_GIC_MARKERS, ARM64_MARKERS, ARM64_MEMORY_MAP_MARKERS, ARM64_PAGE_TABLE_MARKERS, COOPERATIVE_TASK_MARKERS, HEAP_MARKERS, MARKERS, PAGE_ALLOCATOR_MARKERS, PAGE_TABLE_MARKERS, PREEMPTION_CONTEXT_MARKERS, TIMER_MARKERS, qemu_command, validate_arm64_exception_fatal, validate_arm64_exceptions, validate_arm64_gic, validate_arm64_memory_map, validate_arm64_page_tables, validate_arm64_smoke, validate_cooperative_tasks, validate_heap, validate_page_allocator, validate_page_tables, validate_preemption_context, validate_smoke, validate_timer

class BootLogTests(unittest.TestCase):
    def preemption_context_log(self):
        numeric = {
            "FRAME_SIZE": 176, "FRAME_PREFIX_SIZE": 136, "FRAME_IRET_SIZE": 176, "FRAME_FOOTPRINT_SIZE": 191,
            "SOFTWARE_LAYOUT": 8, "TIMER_LAYOUT": 8, "IDLE_LAYOUT": 0,
            "WORKER_SOFTWARE_LAYOUT": 8, "WORKER_TIMER_LAYOUT": 8,
            "SOFTWARE_FRAME": 0x1000, "SOFTWARE_RETURN_FRAME": 0x1000, "SOFTWARE_VECTOR": 0x41, "SOFTWARE_CS": 0x8, "SOFTWARE_RFLAGS": 0x202,
            "SOFTWARE_SAVED_RIP": 0x2000, "SOFTWARE_EXPECTED_RIP": 0x2000, "SOFTWARE_INTERRUPTED_RSP": 0x10b8, "SOFTWARE_EXPECTED_RSP": 0x10b8, "SOFTWARE_POST_RSP": 0x10b8, "SOFTWARE_SAVED_RSP_FIELD": 0x10a0, "SOFTWARE_SAVED_SS": 0x10,
            "TIMER_FRAME": 0x4000, "TIMER_RETURN_FRAME": 0x4000, "TIMER_VECTOR": 0x40, "TIMER_CS": 0x8, "TIMER_RFLAGS": 0x202, "TIMER_SAVED_RIP": 0x5000, "TIMER_LOOP_START": 0x4fff, "TIMER_LOOP_END": 0x5001, "TIMER_INTERRUPTED_RSP": 0x40b8, "TIMER_EXPECTED_RSP": 0x40b8, "TIMER_POST_RSP": 0x40b8, "TIMER_SAVED_RSP_FIELD": 0x40a0, "TIMER_SAVED_SS": 0x10,
            "IDLE_FRAME": 0x7000, "IDLE_INTERRUPTED_RSP": 0x70b0, "IDLE_SAVED_RSP_FIELD": 0x70a0, "IDLE_SAVED_SS": 0x10,
            "TEST_IDLE_SLOT": 1, "TEST_IDLE_GENERATION": 1, "BOOTSTRAP_SLOT": 0, "BOOTSTRAP_GENERATION": 1, "WORKER_SLOT": 2, "WORKER_GENERATION": 1, "IDLE_SLOT": 1, "IDLE_GENERATION": 1,
            "WORKER_SOFTWARE_FRAME": 0x8000, "WORKER_SOFTWARE_TASK_SLOT": 2, "WORKER_SOFTWARE_GENERATION": 1, "WORKER_SOFTWARE_RSP": 0x80b8, "WORKER_SOFTWARE_SAVED_SS": 0x10,
            "WORKER_TIMER_FRAME": 0x9000, "WORKER_TIMER_TASK_SLOT": 2, "WORKER_TIMER_GENERATION": 1, "WORKER_TIMER_RSP": 0x90b8, "WORKER_TIMER_SAVED_SS": 0x10,
            "DEPTH_NESTED": 2, "DEPTH_INNER_DROPPED": 1, "DEPTH_OUTER_DROPPED": 0,
            "REQUEST_WHILE_NESTED": 1, "REQUEST_AFTER_INNER_DROP": 1, "REQUEST_AFTER_OUTER_DROP": 1, "REQUEST_TAKEN": 1, "REQUEST_AFTER_TAKE": 0,
            "TICK_DELTA": 1, "DELIVERY_DELTA": 1, "EOI_DELTA": 1, "SWITCHES_BEFORE": 4, "SWITCHES_AFTER": 4, "CR3_BEFORE": 0x1000, "CR3_AFTER": 0x1000,
            "CURRENT_TASK_BEFORE_SLOT": 0, "CURRENT_TASK_BEFORE_GENERATION": 1, "CURRENT_TASK_AFTER_SLOT": 0, "CURRENT_TASK_AFTER_GENERATION": 1,
            "IF_ENABLED": 1, "INTERRUPT_DEPTH": 0, "FAULTED": 0, "INTERRUPT_CONTEXT_FAULT": 0, "SCHEDULER_ISR_ENTRIES": 0,
        }
        lines = list(PREEMPTION_CONTEXT_MARKERS)
        lines.extend(f"FINNOS:PREEMPT:{key}={value:#x}" if key.endswith(("FRAME", "RIP", "RSP", "CR3", "LOOP_START", "LOOP_END")) else f"FINNOS:PREEMPT:{key}={value}" for key, value in numeric.items())
        patterns = [0x1111111111111111 * (index + 1) for index in range(9)] + [0xAAAAAAAAAAAAAAAA, 0xBBBBBBBBBBBBBBBB, 0xCCCCCCCCCCCCCCCC, 0xDDDDDDDDDDDDDDDD, 0xEEEEEEEEEEEEEEEE, 0xFFFFFFFFFFFFFFFF]
        for phase in ("SOFTWARE_SAVED", "SOFTWARE_POST", "TIMER_SAVED", "TIMER_POST"):
            lines.extend(f"FINNOS:PREEMPT:{phase}_R{index}=0x{value:x}" for index, value in enumerate(patterns))
        return "\n".join(lines)

    def test_preemption_context_complete_contract(self):
        self.assertEqual(validate_preemption_context(33, self.preemption_context_log()), [])

    def test_preemption_context_rejects_marker_spoofs(self):
        output = self.preemption_context_log()
        self.assertTrue(validate_preemption_context(0, output))
        self.assertTrue(validate_preemption_context(33, output + "\n" + PREEMPTION_CONTEXT_MARKERS[-1]))
        for marker in PREEMPTION_CONTEXT_MARKERS:
            with self.subTest(marker=marker):
                self.assertTrue(validate_preemption_context(33, output.replace(marker, marker + "_SPOOF")))
                self.assertTrue(validate_preemption_context(33, output.replace(marker, " " + marker)))

    def test_preemption_context_rejects_frame_contract_spoofs(self):
        output = self.preemption_context_log()
        cases = (
            ("SOFTWARE_RETURN_FRAME=0x1000", "SOFTWARE_RETURN_FRAME=0x1008"),
            ("SOFTWARE_LAYOUT=8", "SOFTWARE_LAYOUT=16"),
            ("SOFTWARE_INTERRUPTED_RSP=0x10b8", "SOFTWARE_INTERRUPTED_RSP=0x10b0"),
            ("WORKER_SOFTWARE_RSP=0x80b8", "WORKER_SOFTWARE_RSP=0x8100"),
            ("WORKER_TIMER_SAVED_SS=16", "WORKER_TIMER_SAVED_SS=0"),
        )
        for old, new in cases:
            with self.subTest(field=old):
                self.assertTrue(validate_preemption_context(33, output.replace(old, new)))

    def arm64_gic_log(self):
        numeric = {
            "DISTRIBUTOR_BASE": 0x08000000,
            "CPU_INTERFACE_BASE": 0x08010000,
            "TYPER": 0x0000001f,
            "IIDR": 0x0000043b,
            "IAR_RAW": 1,
            "INTID": 1,
            "DELIVERY_DELTA": 1,
            "EOI_DELTA": 1,
            "SPURIOUS_BEFORE": 1023,
            "SPURIOUS_AFTER": 1023,
            "INTERRUPT_DEPTH": 0,
            "FRAME_SENTINEL": 1,
            "DAIF_BEFORE": 0x3c0,
            "IRQ_SPSR": 0x340,
            "DAIF_AFTER": 0x3c0,
        }
        evidence = tuple(
            f"FINNOS:GIC:{name}=0x{value:016x}" for name, value in numeric.items()
        )
        return "\n".join(ARM64_GIC_MARKERS + evidence)

    def cooperative_log(self):
        evidence = [f"FINNOS:TASKS:EVENT_{index}={value}" for index, value in enumerate((11, 21, 31, 12, 22, 32, 13, 23, 33))]
        evidence += [
            "FINNOS:TASKS:EVENT_COUNT=9", "FINNOS:TASKS:A_STACK_START=0x1000", "FINNOS:TASKS:A_STACK_END=0x3000", "FINNOS:TASKS:A_SENTINEL=0x1800",
            "FINNOS:TASKS:B_STACK_START=0x4000", "FINNOS:TASKS:B_STACK_END=0x6000", "FINNOS:TASKS:B_SENTINEL=0x4800",
            "FINNOS:TASKS:C_STACK_START=0x7000", "FINNOS:TASKS:C_STACK_END=0x9000", "FINNOS:TASKS:C_SENTINEL=0x7800",
            "FINNOS:TASKS:IDLE_STACK_START=0xa000", "FINNOS:TASKS:IDLE_STACK_END=0xc000", "FINNOS:TASKS:IDLE_RSP=0xa800",
            "FINNOS:TASKS:COMPLETED_DELTA=4", "FINNOS:TASKS:EXITED_BEFORE_REAP=4", "FINNOS:TASKS:QUEUE_LENGTH_BEFORE_REAP=0",
            "FINNOS:TASKS:PHYSICAL_FREE_BASELINE=100", "FINNOS:TASKS:PHYSICAL_FREE_AFTER_REAP=100", "FINNOS:TASKS:MAPPED_BASELINE=50", "FINNOS:TASKS:MAPPED_AFTER_REAP=50",
            "FINNOS:TASKS:VACANT_BASELINE=6", "FINNOS:TASKS:VACANT_AFTER_REAP=6", "FINNOS:TASKS:REAPED_DELTA=4", "FINNOS:TASKS:REUSED_SLOT=2",
            "FINNOS:TASKS:OLD_GENERATION=1", "FINNOS:TASKS:NEW_GENERATION=2", "FINNOS:TASKS:STALE_ID_REJECTED=1", "FINNOS:TASKS:REUSE_RUNS=1",
            "FINNOS:TASKS:IDLE_TICK_DELTA=1", "FINNOS:TASKS:TIMER_START_TICKS=10", "FINNOS:TASKS:TIMER_END_TICKS=12", "FINNOS:TASKS:TICK_DELTA=2",
            "FINNOS:TASKS:DELIVERY_DELTA=2", "FINNOS:TASKS:EOI_DELTA=2", "FINNOS:TASKS:CR3_BEFORE=0x1000", "FINNOS:TASKS:CR3_AFTER=0x1000", "FINNOS:TASKS:SCHEDULER_ISR_ENTRIES=0",
        ]
        return "\n".join(COOPERATIVE_TASK_MARKERS + tuple(evidence))

    def test_cooperative_tasks_complete_sequence(self):
        self.assertEqual(validate_cooperative_tasks(33, self.cooperative_log()), [])

    def test_cooperative_tasks_rejects_status_events_and_generation(self):
        self.assertTrue(validate_cooperative_tasks(35, self.cooperative_log()))
        self.assertTrue(validate_cooperative_tasks(33, self.cooperative_log().replace("EVENT_4=22", "EVENT_4=99")))
        self.assertTrue(validate_cooperative_tasks(33, self.cooperative_log().replace("NEW_GENERATION=2", "NEW_GENERATION=1")))

    def test_cooperative_tasks_rejects_missing_marker_and_panic(self):
        output = self.cooperative_log().replace("FINNOS:TEST:COOPERATIVE_TASKS:IDLE_CONTEXT_OK\n", "")
        self.assertTrue(validate_cooperative_tasks(33, output))
        self.assertTrue(validate_cooperative_tasks(33, self.cooperative_log() + "\nFINNOS:KERNEL:PANIC"))

    def test_cooperative_tasks_rejects_each_numeric_contract_violation(self):
        cases = {
            "duplicate numeric": lambda log: log + "\nFINNOS:TASKS:TICK_DELTA=2",
            "missing numeric": lambda log: log.replace("FINNOS:TASKS:EOI_DELTA=2\n", ""),
            "overflow numeric": lambda log: log.replace("CR3_AFTER=0x1000", "CR3_AFTER=0x10000000000000000"),
            "sentinel range": lambda log: log.replace("A_SENTINEL=0x1800", "A_SENTINEL=0x3000"),
            "worker overlap": lambda log: log.replace("B_STACK_START=0x4000", "B_STACK_START=0x2000"),
            "idle overlap": lambda log: log.replace("IDLE_STACK_START=0xa000", "IDLE_STACK_START=0x8000"),
            "idle rsp": lambda log: log.replace("IDLE_RSP=0xa800", "IDLE_RSP=0xc000"),
            "completion": lambda log: log.replace("COMPLETED_DELTA=4", "COMPLETED_DELTA=3"),
            "exited": lambda log: log.replace("EXITED_BEFORE_REAP=4", "EXITED_BEFORE_REAP=3"),
            "queue": lambda log: log.replace("QUEUE_LENGTH_BEFORE_REAP=0", "QUEUE_LENGTH_BEFORE_REAP=1"),
            "physical baseline": lambda log: log.replace("PHYSICAL_FREE_AFTER_REAP=100", "PHYSICAL_FREE_AFTER_REAP=99"),
            "mapped baseline": lambda log: log.replace("MAPPED_AFTER_REAP=50", "MAPPED_AFTER_REAP=49"),
            "vacant baseline": lambda log: log.replace("VACANT_AFTER_REAP=6", "VACANT_AFTER_REAP=5"),
            "reaped": lambda log: log.replace("REAPED_DELTA=4", "REAPED_DELTA=3"),
            "reuse slot": lambda log: log.replace("REUSED_SLOT=2", "REUSED_SLOT=3"),
            "generation exact": lambda log: log.replace("NEW_GENERATION=2", "NEW_GENERATION=3"),
            "stale id": lambda log: log.replace("STALE_ID_REJECTED=1", "STALE_ID_REJECTED=0"),
            "reuse runs": lambda log: log.replace("REUSE_RUNS=1", "REUSE_RUNS=2"),
            "idle tick": lambda log: log.replace("IDLE_TICK_DELTA=1", "IDLE_TICK_DELTA=0"),
            "tick delta": lambda log: log.replace("TICK_DELTA=2", "TICK_DELTA=1"),
            "delivery": lambda log: log.replace("DELIVERY_DELTA=2", "DELIVERY_DELTA=0"),
            "eoi": lambda log: log.replace("EOI_DELTA=2", "EOI_DELTA=1"),
            "cr3": lambda log: log.replace("CR3_AFTER=0x1000", "CR3_AFTER=0x2000"),
            "scheduler isr": lambda log: log.replace("SCHEDULER_ISR_ENTRIES=0", "SCHEDULER_ISR_ENTRIES=1"),
        }
        for name, mutate in cases.items():
            with self.subTest(name=name):
                self.assertTrue(validate_cooperative_tasks(33, mutate(self.cooperative_log())))

    def test_cooperative_tasks_rejects_duplicate_marker(self):
        marker = "FINNOS:TEST:COOPERATIVE_TASKS:TASK_EXIT_OK"
        self.assertTrue(validate_cooperative_tasks(33, self.cooperative_log() + "\n" + marker))

    def test_markers_in_order(self):
        self.assertEqual(validate_smoke(33, "\n".join(MARKERS)), [])

    def test_missing_and_failure_markers(self):
        errors = validate_smoke(35, MARKERS[0] + "\nFINNOS:KERNEL:PANIC")
        self.assertTrue(any("missing marker" in error for error in errors))
        self.assertIn("kernel panic marker found", errors)

    def test_status_zero_is_not_success(self):
        self.assertTrue(validate_smoke(0, "\n".join(MARKERS)))

    def test_arm64_markers_and_semihosting_status(self):
        output = "\n".join(ARM64_MARKERS)
        self.assertEqual(validate_arm64_smoke(0, output), [])
        self.assertTrue(validate_arm64_smoke(1, output))
        self.assertTrue(validate_arm64_smoke(0, "\n".join(ARM64_MARKERS[:-1])))
        self.assertTrue(validate_arm64_smoke(0, output + "\n" + ARM64_MARKERS[-1]))
        self.assertTrue(validate_arm64_smoke(0, output + "\nFINNOS:TEST:ARM64_EXCEPTIONS:PASS"))
        self.assertTrue(validate_arm64_smoke(0, output + "\nFINNOS:TEST:ARM64_PAGE_TABLES:PASS"))
        self.assertTrue(validate_arm64_smoke(0, output + "\nFINNOS:EXCEPTION:ARM64_PAGE_FAULT"))
        self.assertTrue(validate_arm64_smoke(0, output + "\nFINNOS:TEST:ARM64_GIC:PASS"))

    def test_arm64_gic_complete_contract(self):
        self.assertEqual(validate_arm64_gic(0, self.arm64_gic_log()), [])

    def test_arm64_gic_rejects_status_spoof_duplicate_and_order(self):
        output = self.arm64_gic_log()
        self.assertTrue(validate_arm64_gic(1, output))
        self.assertTrue(validate_arm64_gic(0, "\n".join(ARM64_GIC_MARKERS)))
        self.assertTrue(validate_arm64_gic(0, output + "\n" + ARM64_GIC_MARKERS[-1]))
        self.assertTrue(
            validate_arm64_gic(
                0,
                output.replace(ARM64_GIC_MARKERS[-1], " " + ARM64_GIC_MARKERS[-1]),
            )
        )
        swapped = list(ARM64_GIC_MARKERS)
        swapped[-4], swapped[-3] = swapped[-3], swapped[-4]
        numeric = output.splitlines()[len(ARM64_GIC_MARKERS):]
        self.assertTrue(validate_arm64_gic(0, "\n".join(swapped + numeric)))
        self.assertTrue(
            validate_arm64_gic(
                0,
                output + "\nFINNOS:GIC:EOI_DELTA=0x0000000000000001",
            )
        )
        for marker in ARM64_GIC_MARKERS:
            with self.subTest(suffixed=marker):
                self.assertTrue(validate_arm64_gic(0, output.replace(marker, marker + "_SPOOF")))

    def test_arm64_gic_rejects_numeric_lifecycle_spoofs(self):
        cases = {
            "distributor": ("DISTRIBUTOR_BASE", 0x08001000),
            "cpu interface": ("CPU_INTERFACE_BASE", 0x08011000),
            "zero typer": ("TYPER", 0),
            "zero iidr": ("IIDR", 0),
            "wrong iar": ("IAR_RAW", 2),
            "wrong intid": ("INTID", 2),
            "no delivery": ("DELIVERY_DELTA", 0),
            "missing eoi": ("EOI_DELTA", 0),
            "pre-spurious eoi": ("SPURIOUS_BEFORE", 1),
            "post-spurious eoi": ("SPURIOUS_AFTER", 1),
            "depth leak": ("INTERRUPT_DEPTH", 1),
            "frame clobber": ("FRAME_SENTINEL", 0),
            "unmasked before": ("DAIF_BEFORE", 0x340),
            "masked irq origin": ("IRQ_SPSR", 0x3c0),
            "unmasked after": ("DAIF_AFTER", 0x340),
        }
        output = self.arm64_gic_log()
        for label, (name, value) in cases.items():
            with self.subTest(label=label):
                pattern = rf"FINNOS:GIC:{name}=0x[0-9a-f]{{16}}"
                replacement = f"FINNOS:GIC:{name}=0x{value:016x}"
                self.assertTrue(validate_arm64_gic(0, re.sub(pattern, replacement, output)))

    def test_arm64_gic_rejects_cross_mode_and_fatal_evidence(self):
        output = self.arm64_gic_log()
        for marker in (
            "FINNOS:TEST:ARM64_EXCEPTIONS:PASS",
            "FINNOS:TEST:ARM64_MEMORY_MAP:PASS",
            "FINNOS:TEST:ARM64_PAGE_TABLES:PASS",
            "FINNOS:TEST:ARM64_PAGE_FAULTS:PASS",
            "FINNOS:EXCEPTION:ARM64_FATAL",
            "FINNOS:KERNEL:GIC_ERROR:STATE",
            "FINNOS:KERNEL:PANIC",
        ):
            with self.subTest(marker=marker):
                self.assertTrue(validate_arm64_gic(0, output + "\n" + marker))
        page_errors = validate_arm64_page_tables(
            0,
            "\n".join(ARM64_PAGE_TABLE_MARKERS)
            + "\nFINNOS:TEST:ARM64_GIC:PASS",
        )
        self.assertTrue(any("forbidden marker" in error for error in page_errors))
        exception_errors = validate_arm64_exceptions(
            0,
            "\n".join(ARM64_EXCEPTION_MARKERS)
            + "\nFINNOS:TEST:ARM64_GIC:PASS",
        )
        self.assertTrue(any("forbidden marker" in error for error in exception_errors))

    def test_arm64_memory_map_contract(self):
        numeric = {
            "DESCRIPTORS": 8,
            "REGIONS": 10,
            "USABLE_BYTES": 0x400000,
            "RESERVED_BYTES": 0x100000,
            "KERNEL_BYTES": 0x20000,
            "BOOT_INFO_BYTES": 0x1000,
            "MEMORY_MAP_STORAGE_BYTES": 0x200,
            "FRAMEBUFFER_BYTES": 0,
            "TOTAL_PAGES": 0x400,
            "FREE_PAGES": 0x400,
            "ALLOCATED_PAGES": 0,
            "MANAGED_EXTENTS": 2,
            "FREE_EXTENTS": 2,
            "TEST_ALLOCATED_PAGE": 0x200000,
        }
        evidence = tuple(
            f"FINNOS:MEMORY:{name}=0x{value:016x}" for name, value in numeric.items()
        )
        output = "\n".join(ARM64_MEMORY_MAP_MARKERS + evidence)
        self.assertEqual(validate_arm64_memory_map(0, output), [])
        self.assertTrue(validate_arm64_memory_map(1, output))
        self.assertTrue(validate_arm64_memory_map(0, output.replace("USABLE_BYTES=0x0000000000400000", "USABLE_BYTES=0x0000000000000000")))
        self.assertTrue(validate_arm64_memory_map(0, output + "\nFINNOS:MEMORY:FREE_PAGES=0x0000000000000400"))
        self.assertTrue(validate_arm64_memory_map(0, output + "\nFINNOS:EXCEPTION:ARM64_FATAL"))
        self.assertTrue(
            validate_arm64_memory_map(
                0,
                output.replace(
                    "FINNOS:TEST:ARM64_MEMORY_MAP:ALLOC_OK",
                    "FINNOS:TEST:ARM64_MEMORY_MAP:ALLOC_OK_SPOOF",
                ),
            )
        )
        self.assertTrue(
            validate_arm64_memory_map(
                0,
                output.replace(
                    "FINNOS:TEST:ARM64_MEMORY_MAP:ALLOC_OK",
                    " FINNOS:TEST:ARM64_MEMORY_MAP:ALLOC_OK",
                ),
            )
        )

    def test_arm64_exception_markers_and_semihosting_status(self):
        output = "\n".join(ARM64_EXCEPTION_MARKERS)
        self.assertEqual(validate_arm64_exceptions(0, output), [])
        self.assertTrue(validate_arm64_exceptions(1, output))
        self.assertTrue(
            validate_arm64_exceptions(0, "\n".join(ARM64_EXCEPTION_MARKERS[:-1]))
        )
        self.assertTrue(
            validate_arm64_exceptions(
                0, output + "\n" + ARM64_EXCEPTION_MARKERS[-1]
            )
        )
        self.assertTrue(
            validate_arm64_exceptions(
                0, output.replace(ARM64_EXCEPTION_MARKERS[9], "")
            )
        )
        self.assertTrue(
            validate_arm64_exceptions(0, output + "\nFINNOS:EXCEPTION:ARM64_FATAL")
        )
        swapped = list(ARM64_EXCEPTION_MARKERS)
        swapped[10], swapped[11] = swapped[11], swapped[10]
        self.assertTrue(validate_arm64_exceptions(0, "\n".join(swapped)))
        self.assertTrue(
            validate_arm64_exceptions(
                0,
                output.replace(
                    "FINNOS:TEST:ARM64_EXCEPTIONS:BRK_PASS",
                    "FINNOS:TEST:ARM64_EXCEPTIONS:BRK_PASS_SPOOF",
                ),
            )
        )
        self.assertTrue(
            validate_arm64_exceptions(
                0,
                output.replace(
                    "FINNOS:TEST:ARM64_EXCEPTIONS:BRK_PASS",
                    "FINNOS:TEST:ARM64_EXCEPTIONS:BRK_PASS ",
                ),
            )
        )
        for marker in (
            "FINNOS:EXCEPTION:ARM64_ELR_OVERFLOW",
            "FINNOS:EXCEPTION:ARM64_UNEXPECTED",
            "FINNOS:KERNEL:PANIC:ARM64_EXCEPTION_INIT",
        ):
            with self.subTest(marker=marker):
                self.assertTrue(validate_arm64_exceptions(0, output + "\n" + marker))

    def test_arm64_page_table_contract(self):
        numeric = {
            "ROOT": 0x40000000,
            "TTBR0": 0x40000000,
            "TTBR1": 0,
            "TCR": 0x0000000480903510,
            "MAIR": 0x4400ff,
            "SCTLR": 0x30D8198D,
            "TABLE_PAGES_RESERVED": 64,
            "TABLE_PAGES_USED": 8,
            "MAPPED_PAGES": 128,
        }
        evidence = tuple(
            f"FINNOS:PAGING:{name}=0x{value:016x}" for name, value in numeric.items()
        )
        ordered = []
        fault_begins = {
            f"FINNOS:TEST:ARM64_PAGE_FAULTS:{name}_BEGIN"
            for name in ("NULL_READ", "LOW_GUARD_READ", "TEXT_WRITE", "DATA_EXECUTE")
        }
        for marker in ARM64_PAGE_TABLE_MARKERS:
            ordered.append(marker)
            if marker in fault_begins:
                ordered.append("FINNOS:EXCEPTION:ARM64_PAGE_FAULT")
        output = "\n".join(tuple(ordered) + evidence)
        self.assertEqual(validate_arm64_page_tables(0, output), [])
        self.assertTrue(validate_arm64_page_tables(1, output))
        self.assertTrue(
            validate_arm64_page_tables(0, "\n".join(ARM64_PAGE_TABLE_MARKERS[:-1] + evidence))
        )
        self.assertTrue(
            validate_arm64_page_tables(
                0,
                output.replace(
                    "TABLE_PAGES_USED=0x0000000000000008",
                    "TABLE_PAGES_USED=0x0000000000000041",
                ),
            )
        )
        self.assertTrue(validate_arm64_page_tables(0, output + "\nFINNOS:EXCEPTION:ARM64_FATAL"))

        for marker in (
            "FINNOS:EXCEPTION:ARM64_TEST_STATE_ERROR",
            "FINNOS:EXCEPTION:ARM64_SOURCE=0x0000000000000004",
            "FINNOS:TEST:ARM64_EXCEPTIONS:PASS",
            "FINNOS:TEST:ARM64_EXCEPTION_FATAL:BEGIN",
            "FINNOS:TEST:ARM64_MEMORY_MAP:PASS",
        ):
            with self.subTest(marker=marker):
                self.assertTrue(validate_arm64_page_tables(0, output + "\n" + marker))

        for marker in ARM64_PAGE_TABLE_MARKERS:
            with self.subTest(suffixed=marker):
                self.assertTrue(
                    validate_arm64_page_tables(0, output.replace(marker, marker + "_SUFFIX"))
                )
        self.assertTrue(
            validate_arm64_page_tables(
                0,
                output.replace(
                    "FINNOS:EXCEPTION:ARM64_PAGE_FAULT",
                    "FINNOS:EXCEPTION:ARM64_PAGE_FAULT_SUFFIX",
                ),
            )
        )
        first_fault = (
            "FINNOS:TEST:ARM64_PAGE_FAULTS:NULL_READ_BEGIN\n"
            "FINNOS:EXCEPTION:ARM64_PAGE_FAULT\n"
            "FINNOS:TEST:ARM64_PAGE_FAULTS:NULL_READ_PASS"
        )
        self.assertTrue(
            validate_arm64_page_tables(
                0,
                output.replace(
                    first_fault,
                    first_fault.replace(
                        "FINNOS:EXCEPTION:ARM64_PAGE_FAULT\n",
                        "FINNOS:EXCEPTION:ARM64_PAGE_FAULT\n"
                        "FINNOS:EXCEPTION:ARM64_PAGE_FAULT\n",
                    ),
                ),
            )
        )
        self.assertTrue(
            validate_arm64_page_tables(
                0,
                output.replace(
                    first_fault,
                    "FINNOS:TEST:ARM64_PAGE_FAULTS:NULL_READ_BEGIN\n"
                    "FINNOS:TEST:ARM64_PAGE_FAULTS:NULL_READ_PASS\n"
                    "FINNOS:EXCEPTION:ARM64_PAGE_FAULT",
                ),
            )
        )

        for name, value in (
            ("ROOT", 0x40001000),
            ("TTBR1", 0x1000),
            ("MAIR", 0x4400FE),
            ("TCR", 0x0000000480903511),
            ("SCTLR", 0x00001),
        ):
            with self.subTest(field=name):
                original = f"FINNOS:PAGING:{name}=0x{numeric[name]:016x}"
                replacement = f"FINNOS:PAGING:{name}=0x{value:016x}"
                self.assertTrue(validate_arm64_page_tables(0, output.replace(original, replacement)))

    def test_arm64_fatal_exception_contract(self):
        output = "\n".join(
            ARM64_MARKERS[:-1]
            + (
                "FINNOS:KERNEL:ARM64_CURRENT_EL=1",
                "FINNOS:KERNEL:ARM64_EXCEPTION_VECTORS_READY",
                "FINNOS:KERNEL:ARM64_SERIAL_READY",
                "FINNOS:TEST:ARM64_EXCEPTION_FATAL:BEGIN",
                "FINNOS:EXCEPTION:ARM64_SOURCE=0x0000000000000004",
                "FINNOS:EXCEPTION:ARM64_ESR=0x00000000f200f101",
                "FINNOS:EXCEPTION:ARM64_ELR=0x0000000040200000",
                "FINNOS:EXCEPTION:ARM64_FAR=0x0000000000000000",
                "FINNOS:EXCEPTION:ARM64_SPSR=0x00000000600003c5",
                "FINNOS:EXCEPTION:ARM64_X0=0x0000000000000000",
                "FINNOS:EXCEPTION:ARM64_FATAL",
            )
        )
        self.assertEqual(validate_arm64_exception_fatal(1, output), [])
        self.assertTrue(validate_arm64_exception_fatal(0, output))
        self.assertTrue(validate_arm64_exception_fatal(1, output.replace("ARM64_FATAL", "ARM64_MISSING")))
        self.assertTrue(
            validate_arm64_exception_fatal(
                1,
                output.replace(
                    "ARM64_SOURCE=0x0000000000000004",
                    "ARM64_SOURCE=0x0000000000000005",
                ),
            )
        )
        self.assertTrue(
            validate_arm64_exception_fatal(
                1,
                output.replace(
                    "FINNOS:TEST:ARM64_EXCEPTION_FATAL:BEGIN",
                    " FINNOS:TEST:ARM64_EXCEPTION_FATAL:BEGIN",
                ),
            )
        )
        self.assertTrue(
            validate_arm64_exception_fatal(
                1,
                output.replace(
                    "ARM64_ESR=0x00000000f200f101",
                    "ARM64_ESR=0x00000000f200f102",
                ),
            )
        )
        self.assertTrue(
            validate_arm64_exception_fatal(
                1,
                output.replace(
                    "FINNOS:TEST:ARM64_EXCEPTION_FATAL:BEGIN",
                    "FINNOS:TEST:ARM64_EXCEPTION_FATAL:BEGIN_SPOOF",
                ),
            )
        )

    def test_arm64_qemu_command_uses_virtio_and_semihosting(self):
        command = qemu_command(
            "qemu-system-aarch64",
            "/firmware/AAVMF_CODE.fd",
            Path("/images/finnos.img"),
            headless=True,
            test_exit=True,
            machine="virt,gic-version=2,secure=off",
            architecture="arm64",
            cpu="cortex-a72",
        )
        rendered = " ".join(str(part) for part in command)
        self.assertIn("-cpu cortex-a72", rendered)
        self.assertIn("-machine virt,gic-version=2,secure=off", rendered)
        self.assertIn("-smp 1", rendered)
        self.assertIn("virtio-blk-pci", rendered)
        self.assertIn("-semihosting-config enable=on,target=native", rendered)
        self.assertNotIn("isa-debug-exit", rendered)

    def test_page_allocator_markers(self):
        self.assertEqual(validate_page_allocator(33, "\n".join(PAGE_ALLOCATOR_MARKERS)), [])
        self.assertTrue(validate_page_allocator(35, "\n".join(PAGE_ALLOCATOR_MARKERS)))
        self.assertTrue(validate_page_allocator(33, "\n".join(PAGE_ALLOCATOR_MARKERS[:-1])))
        self.assertTrue(validate_page_allocator(33, "\n".join(PAGE_ALLOCATOR_MARKERS) + "\nFINNOS:KERNEL:PAGE_ALLOCATOR_ERROR"))

    def test_page_table_markers_and_status(self):
        self.assertEqual(validate_page_tables(33, "\n".join(PAGE_TABLE_MARKERS)), [])
        self.assertTrue(validate_page_tables(35, "\n".join(PAGE_TABLE_MARKERS)))
        self.assertTrue(validate_page_tables(33, "\n".join(PAGE_TABLE_MARKERS[:-1])))
        self.assertTrue(validate_page_tables(33, "\n".join(PAGE_TABLE_MARKERS) + "\nFINNOS:KERNEL:PAGE_TABLE_ERROR"))

    def test_page_table_unexpected_fault_and_order(self):
        output = "\n".join(PAGE_TABLE_MARKERS[:-1]) + "\nFINNOS:EXCEPTION:PAGE_FAULT"
        errors = validate_page_tables(33, output)
        self.assertTrue(any("unexpected page fault" in error for error in errors))

    def test_heap_complete_sequence(self):
        self.assertEqual(validate_heap(33, "\n".join(HEAP_MARKERS)), [])

    def test_heap_wrong_status(self):
        self.assertTrue(validate_heap(35, "\n".join(HEAP_MARKERS)))

    def test_heap_missing_heap_mapped(self):
        self.assertTrue(validate_heap(33, "\n".join(HEAP_MARKERS[1:])))

    def test_heap_missing_heap_ready(self):
        output = "\n".join(HEAP_MARKERS[:1] + HEAP_MARKERS[2:])
        self.assertTrue(validate_heap(33, output))

    def test_heap_duplicate_heap_ready(self):
        heap_ready = "FINNOS:KERNEL:HEAP_READY"
        output = "\n".join(HEAP_MARKERS + (heap_ready,))
        self.assertTrue(validate_heap(33, output))

    def test_heap_missing_box(self):
        output = "\n".join(marker for marker in HEAP_MARKERS if "BOX_OK" not in marker)
        self.assertTrue(validate_heap(33, output))

    def test_heap_missing_exhaustion(self):
        output = "\n".join(marker for marker in HEAP_MARKERS if "EXHAUSTION_OK" not in marker)
        self.assertTrue(validate_heap(33, output))

    def test_heap_out_of_order_markers(self):
        output = "\n".join(HEAP_MARKERS[:2] + (HEAP_MARKERS[3], HEAP_MARKERS[2]) + HEAP_MARKERS[4:])
        self.assertTrue(validate_heap(33, output))

    def test_heap_error_marker(self):
        self.assertTrue(
            validate_heap(33, "\n".join(HEAP_MARKERS) + "\nFINNOS:KERNEL:HEAP_ERROR:OutOfMemory")
        )

    def test_heap_oom_marker(self):
        self.assertTrue(validate_heap(33, "\n".join(HEAP_MARKERS) + "\nFINNOS:KERNEL:HEAP_OOM"))

    def test_heap_fatal_exception_marker(self):
        self.assertTrue(
            validate_heap(33, "\n".join(HEAP_MARKERS) + "\nFINNOS:EXCEPTION:PAGE_FAULT")
        )

    def test_heap_panic_marker(self):
        self.assertTrue(validate_heap(33, "\n".join(HEAP_MARKERS) + "\nFINNOS:KERNEL:PANIC"))

    def test_heap_missing_final_marker(self):
        self.assertTrue(validate_heap(33, "\n".join(HEAP_MARKERS[:-1])))

    def test_timer_complete_sequence(self):
        output = "\n".join(TIMER_MARKERS) + "\nFINNOS:INTERRUPTS:PIC_MASTER_MASK=0xff\nFINNOS:INTERRUPTS:PIC_SLAVE_MASK=0xff\nFINNOS:APIC:PHYSICAL_BASE=0xfee00000\nFINNOS:APIC:VIRTUAL_BASE=0x0000300000000000\nFINNOS:APIC:ID=0\nFINNOS:APIC:VERSION=0x50014\nFINNOS:TIMER:FREQUENCY_HZ=100\nFINNOS:TIMER:TICK_MILLISECONDS=10\nFINNOS:TIMER:PIT_REFERENCE_COUNT=11931\nFINNOS:TIMER:APIC_CALIBRATION_ELAPSED_COUNTS=100\nFINNOS:TIMER:APIC_INITIAL_COUNT=100\nFINNOS:TIMER:FREQUENCY_WINDOW_MS=50\nFINNOS:TIMER:FREQUENCY_WINDOW_TICKS=5\nFINNOS:INTERRUPTS:CALL_ALIGNMENT=0\nFINNOS:TIMER:TEST_START_TICKS=1\nFINNOS:TIMER:TEST_END_TICKS=9\nFINNOS:TIMER:TEST_ELAPSED_TICKS=8\nFINNOS:TIMER:TEST_UPTIME_MS=90"
        self.assertEqual(validate_timer(33, output), [])

    def test_timer_rejects_wrong_status_and_short_ticks(self):
        output = "\n".join(TIMER_MARKERS) + "\nFINNOS:TIMER:TEST_START_TICKS=1\nFINNOS:TIMER:TEST_END_TICKS=2\nFINNOS:TIMER:TEST_ELAPSED_TICKS=1"
        self.assertTrue(validate_timer(35, output))

    def test_timer_rejects_error_marker_and_duplicate_ready(self):
        output = "\n".join(TIMER_MARKERS) + "\nFINNOS:KERNEL:TIMER_READY\nFINNOS:KERNEL:TIMER_ERROR:Timeout"
        errors = validate_timer(33, output)
        self.assertTrue(any("TIMER_READY" in error for error in errors))
        self.assertTrue(any("forbidden" in error for error in errors))

if __name__ == "__main__": unittest.main()
