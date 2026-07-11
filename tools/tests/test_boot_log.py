import unittest

from tools.finnlib.qemu import MARKERS, PAGE_ALLOCATOR_MARKERS, PAGE_TABLE_MARKERS, validate_page_allocator, validate_page_tables, validate_smoke

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

if __name__ == "__main__": unittest.main()
