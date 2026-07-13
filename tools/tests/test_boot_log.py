import unittest

from tools.finnlib.qemu import COOPERATIVE_TASK_MARKERS, HEAP_MARKERS, MARKERS, PAGE_ALLOCATOR_MARKERS, PAGE_TABLE_MARKERS, PREEMPTION_CONTEXT_MARKERS, TIMER_MARKERS, validate_cooperative_tasks, validate_heap, validate_page_allocator, validate_page_tables, validate_preemption_context, validate_smoke, validate_timer

class BootLogTests(unittest.TestCase):
    def test_preemption_context_markers_are_strict(self):
        numeric = {
            "FRAME_SIZE": 160, "SOFTWARE_FRAME": 0x1000, "SOFTWARE_RETURN_FRAME": 0x1000,
            "SOFTWARE_SAVED_RIP": 0x2000, "SOFTWARE_EXPECTED_RIP": 0x2000,
            "SOFTWARE_INTERRUPTED_RSP": 0x3000, "SOFTWARE_EXPECTED_RSP": 0x3000, "SOFTWARE_POST_RSP": 0x3018,
            "TIMER_FRAME": 0x4000, "TIMER_RETURN_FRAME": 0x4000, "TIMER_SAVED_RIP": 0x5000,
            "TIMER_LOOP_START": 0x4fff, "TIMER_LOOP_END": 0x5001, "TIMER_INTERRUPTED_RSP": 0x6000,
            "TIMER_EXPECTED_RSP": 0x6000, "TIMER_POST_RSP": 0x6018, "IDLE_FRAME": 0x7000,
            "IDLE_INTERRUPTED_RSP": 0x7100, "BOOTSTRAP_SLOT": 0, "BOOTSTRAP_GENERATION": 1,
            "WORKER_SLOT": 2, "WORKER_GENERATION": 1, "IDLE_SLOT": 1, "IDLE_GENERATION": 1,
            "DEPTH_NESTED": 2, "DEPTH_INNER_DROPPED": 1, "DEPTH_OUTER_DROPPED": 0,
            "REQUEST_WHILE_NESTED": 1, "REQUEST_AFTER_INNER_DROP": 1, "REQUEST_AFTER_OUTER_DROP": 1,
            "REQUEST_TAKEN": 1, "REQUEST_AFTER_TAKE": 0, "TICK_DELTA": 1, "DELIVERY_DELTA": 1,
            "EOI_DELTA": 1, "SWITCHES_BEFORE": 4, "SWITCHES_AFTER": 4, "CR3_BEFORE": 0x1000,
            "CR3_AFTER": 0x1000, "IF_ENABLED": 1, "INTERRUPT_DEPTH": 0, "FAULTED": 0,
        }
        lines = ["\n".join(PREEMPTION_CONTEXT_MARKERS)]
        lines.extend(f"FINNOS:PREEMPT:{key}={value:#x}" if key.endswith(("FRAME", "RIP", "RSP", "CR3", "LOOP_START", "LOOP_END")) else f"FINNOS:PREEMPT:{key}={value}" for key, value in numeric.items())
        patterns = [0x1111111111111111, 0x2222222222222222, 0x3333333333333333, 0x4444444444444444, 0x5555555555555555, 0x6666666666666666, 0x7777777777777777, 0x8888888888888888, 0x9999999999999999, 0xAAAAAAAAAAAAAAAA, 0xBBBBBBBBBBBBBBBB, 0xCCCCCCCCCCCCCCCC, 0xDDDDDDDDDDDDDDDD, 0xEEEEEEEEEEEEEEEE, 0xFFFFFFFFFFFFFFFF]
        for phase in ("SOFTWARE_SAVED", "SOFTWARE_POST", "TIMER_SAVED", "TIMER_POST"):
            lines.extend(f"FINNOS:PREEMPT:{phase}_R{index}=0x{value:x}" for index, value in enumerate(patterns))
        log = "\n".join(lines)
        self.assertEqual(validate_preemption_context(33, log), [])
        self.assertTrue(validate_preemption_context(0, log))
        self.assertTrue(validate_preemption_context(33, log + "\n" + PREEMPTION_CONTEXT_MARKERS[-1]))
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
