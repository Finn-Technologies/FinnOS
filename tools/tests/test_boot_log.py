import unittest

from tools.finnlib.qemu import HEAP_MARKERS, MARKERS, PAGE_ALLOCATOR_MARKERS, PAGE_TABLE_MARKERS, validate_heap, validate_page_allocator, validate_page_tables, validate_smoke

class BootLogTests(unittest.TestCase):
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

if __name__ == "__main__": unittest.main()
