"""Tests for the repository-local FinnOS agent operating system."""

from pathlib import Path
import subprocess
import sys
import unittest


ROOT = Path(__file__).resolve().parents[2]


class AgentInfrastructureTests(unittest.TestCase):
    def run_script(self, *arguments: str) -> subprocess.CompletedProcess[str]:
        return subprocess.run(
            [sys.executable, *arguments], cwd=ROOT, text=True,
            capture_output=True, check=False,
        )

    def test_skill_registry_and_dependencies_validate(self) -> None:
        result = self.run_script(".agents/scripts/validate.py")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("87 skills", result.stdout)

    def test_generated_skills_and_manifest_are_current(self) -> None:
        result = self.run_script(".agents/scripts/render_skills.py", "--check")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_dirty_skill_verification_disclaims_integration(self) -> None:
        content = (ROOT / ".agents" / "skills" / "roadmap-execution" / "SKILL.md").read_text()
        self.assertIn("base commit `df5cf62` plus the dirty worktree context", content)
        self.assertIn("This is not an integrated-revision claim", content)

    def test_local_markdown_links_validate(self) -> None:
        result = self.run_script(".agents/scripts/check_links.py")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_planning_yaml_structure_validates(self) -> None:
        result = self.run_script(".agents/scripts/validate_yaml.py")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_handoff_defaults_to_stdout(self) -> None:
        result = self.run_script(".agents/scripts/new_handoff.py")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("# Agent Handoff", result.stdout)
        self.assertIn("commit:", result.stdout)
        self.assertRegex(result.stdout, r"## .+")

    def test_handoff_refuses_absolute_and_existing_outputs(self) -> None:
        absolute = self.run_script(".agents/scripts/new_handoff.py", "--output", "/tmp/finnos.md")
        self.assertNotEqual(absolute.returncode, 0)
        existing = self.run_script(".agents/scripts/new_handoff.py", "--output", "README.md")
        self.assertNotEqual(existing.returncode, 0)
        self.assertIn("refusing to overwrite", existing.stderr)

    def test_validator_detects_dependency_cycle(self) -> None:
        sys.path.insert(0, str(ROOT / ".agents" / "scripts"))
        try:
            from validate import dependency_cycles
        finally:
            sys.path.pop(0)
        cycles = dependency_cycles({"a": ["b"], "b": ["a"]})
        self.assertEqual(cycles, [["a", "b", "a"]])


if __name__ == "__main__":
    unittest.main()
