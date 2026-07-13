import unittest

from tools.finnlib.qemu import COOPERATIVE_TASK_MARKERS, HEAP_MARKERS, MARKERS, PAGE_ALLOCATOR_MARKERS, PAGE_TABLE_MARKERS, TIMER_MARKERS, validate_cooperative_tasks, validate_heap, validate_page_allocator, validate_page_tables, validate_smoke, validate_timer

class BootLogTests(unittest.TestCase):
    def cooperative_log(self):
        evidence = [f"FINNOS:TASKS:EVENT_{index}={value}" for index, value in enumerate((11, 21, 31, 12, 22, 32, 13, 23, 33))]
        evidence += ["FINNOS:TASKS:EVENT_COUNT=9", "FINNOS:TASKS:OLD_GENERATION=1", "FINNOS:TASKS:NEW_GENERATION=2", "FINNOS:TASKS:TIMER_START_TICKS=10", "FINNOS:TASKS:TIMER_END_TICKS=12"]
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
