import json
import sys
import tempfile
import unittest
from datetime import datetime, timezone
from pathlib import Path
from unittest.mock import call, patch

ROOT = Path(__file__).resolve().parents[2]
TOOLS = ROOT / "tools"
sys.path.insert(0, str(TOOLS))

import monitor_github_run as monitor  # noqa: E402


class FakeResponse:
    def __init__(self, payload):
        self.body = json.dumps(payload).encode("utf-8")

    def __enter__(self):
        return self

    def __exit__(self, *_args):
        return False

    def read(self):
        return self.body


class GitHubRunMonitorTests(unittest.TestCase):
    def test_selects_only_expected_worker_prefix_and_sorts_numeric_suffixes(self):
        jobs = [
            {"id": 3, "name": "Smoke worker 10", "status": "queued"},
            {"id": 2, "name": "Admin monitor", "status": "in_progress"},
            {"id": 1, "name": "Smoke worker 2", "status": "in_progress"},
        ]

        selected = monitor.select_worker_jobs(jobs, "Smoke worker ")

        self.assertEqual([job["name"] for job in selected], ["Smoke worker 2", "Smoke worker 10"])

    def test_counts_queued_running_success_and_failure_states(self):
        jobs = [
            {"status": "queued", "conclusion": None},
            {"status": "in_progress", "conclusion": None},
            {"status": "completed", "conclusion": "success"},
            {"status": "completed", "conclusion": "cancelled"},
        ]

        self.assertEqual(
            monitor.worker_counts(jobs),
            {"queued": 1, "running": 1, "succeeded": 1, "failed": 1},
        )

    def test_elapsed_time_uses_started_and_completed_timestamps(self):
        job = {
            "started_at": "2026-10-07T12:00:00Z",
            "completed_at": "2026-10-07T12:02:15Z",
        }

        self.assertEqual(
            monitor.job_elapsed_seconds(job, datetime(2026, 10, 7, 12, 3, tzinfo=timezone.utc)),
            135.0,
        )
        self.assertEqual(monitor.format_duration(3661), "01:01:01")
        self.assertEqual(monitor.format_duration(None), "not started")

    def test_fetch_jobs_follows_pagination_and_uses_bearer_token(self):
        first_page = [{"id": index, "name": f"Other job {index}"} for index in range(100)]
        second_page = [{"id": 101, "name": "Smoke worker 0"}]
        with patch.object(
            monitor,
            "urlopen",
            side_effect=[FakeResponse({"jobs": first_page}), FakeResponse({"jobs": second_page})],
        ) as mocked_urlopen:
            jobs = monitor.fetch_jobs("owner/repo", 1234, "test-token")

        self.assertEqual(len(jobs), 101)
        self.assertEqual(mocked_urlopen.call_count, 2)
        requests = [invocation.args[0] for invocation in mocked_urlopen.call_args_list]
        self.assertIn("page=1", requests[0].full_url)
        self.assertIn("page=2", requests[1].full_url)
        self.assertEqual(requests[0].get_header("Authorization"), "Bearer test-token")

    def test_monitor_polls_until_every_worker_is_terminal_and_writes_job_output(self):
        snapshots = iter(
            [
                [{"id": 1, "name": "Smoke worker 0", "status": "queued"}],
                [
                    {
                        "id": 1,
                        "name": "Smoke worker 0",
                        "status": "completed",
                        "conclusion": "success",
                        "started_at": "2026-10-07T12:00:00Z",
                        "completed_at": "2026-10-07T12:00:05Z",
                    }
                ],
            ]
        )
        clock_values = iter([0.0, 0.0, 10.0])
        messages = []
        with tempfile.TemporaryDirectory() as directory:
            output_path = Path(directory) / "github-output"
            result = monitor.monitor_run(
                repository="owner/repo",
                run_id=1234,
                job_prefix="Smoke worker ",
                expected_jobs=1,
                poll_seconds=5,
                timeout_seconds=60,
                token="test-token",
                fetcher=lambda *_args: next(snapshots),
                sleep_fn=lambda _seconds: None,
                clock=lambda: next(clock_values),
                now_fn=lambda: datetime(2026, 10, 7, 12, 1, tzinfo=timezone.utc),
                output=lambda *args, **_kwargs: messages.append(" ".join(args)),
                github_output=str(output_path),
            )

            self.assertEqual(result, 0)
            self.assertIn("succeeded=1", "\n".join(messages))
            self.assertIn("duration=00:00:05", "\n".join(messages))
            self.assertIn("complete=true", output_path.read_text(encoding="utf-8"))

    def test_monitor_timeout_can_yield_to_next_continuation_window(self):
        messages = []
        clock_values = iter([0.0, 6.0])
        result = monitor.monitor_run(
            repository="owner/repo",
            run_id=1234,
            job_prefix="Smoke worker ",
            expected_jobs=1,
            poll_seconds=5,
            timeout_seconds=5,
            token="test-token",
            fetcher=lambda *_args: [
                {"id": 1, "name": "Smoke worker 0", "status": "in_progress"}
            ],
            sleep_fn=lambda _seconds: None,
            clock=lambda: next(clock_values),
            now_fn=lambda: datetime(2026, 10, 7, 12, 1, tzinfo=timezone.utc),
            output=lambda *args, **_kwargs: messages.append(" ".join(args)),
            continue_on_timeout=True,
        )

        self.assertEqual(result, 0)
        self.assertTrue(any("next supervisor segment should continue" in line for line in messages))


if __name__ == "__main__":
    unittest.main()
