#!/usr/bin/env python3
"""Live summary monitor for a matrix of jobs in the current GitHub Actions run."""

from __future__ import annotations

import argparse
import json
import os
import re
import sys
import time
from datetime import datetime, timezone
from pathlib import Path
from typing import Any, Callable
from urllib.error import HTTPError, URLError
from urllib.request import Request, urlopen

API_ROOT = "https://api.github.com"
API_VERSION = "2022-11-28"


def fetch_jobs(repository: str, run_id: int, token: str) -> list[dict[str, Any]]:
    """Fetch every job in a workflow run, following GitHub's pagination."""
    all_jobs: list[dict[str, Any]] = []
    page = 1
    while True:
        url = (
            f"{API_ROOT}/repos/{repository}/actions/runs/{run_id}/jobs"
            f"?per_page=100&page={page}"
        )
        request = Request(
            url,
            headers={
                "Accept": "application/vnd.github+json",
                "Authorization": f"Bearer {token}",
                "X-GitHub-Api-Version": API_VERSION,
            },
        )
        try:
            with urlopen(request, timeout=30) as response:
                payload = json.loads(response.read().decode("utf-8"))
        except HTTPError as exc:
            raise RuntimeError(f"GitHub Actions API returned HTTP {exc.code}.") from exc
        except URLError as exc:
            raise RuntimeError("Could not reach the GitHub Actions API.") from exc
        except (UnicodeDecodeError, json.JSONDecodeError) as exc:
            raise RuntimeError("GitHub Actions API returned invalid JSON.") from exc

        jobs = payload.get("jobs")
        if not isinstance(jobs, list):
            raise RuntimeError("GitHub Actions API response did not include a jobs list.")
        all_jobs.extend(job for job in jobs if isinstance(job, dict))
        if len(jobs) < 100:
            return all_jobs
        page += 1


def select_worker_jobs(jobs: list[dict[str, Any]], job_prefix: str) -> list[dict[str, Any]]:
    """Select matrix jobs by their configured display-name prefix."""
    selected = [
        job for job in jobs
        if isinstance(job.get("name"), str) and job["name"].startswith(job_prefix)
    ]

    def key(job: dict[str, Any]) -> tuple[int, str]:
        match = re.search(r"(\d+)$", job.get("name", ""))
        return (int(match.group(1)) if match else sys.maxsize, job.get("name", ""))

    return sorted(selected, key=key)


def parse_timestamp(value: str | None) -> datetime | None:
    if not value:
        return None
    try:
        parsed = datetime.fromisoformat(value.replace("Z", "+00:00"))
    except (AttributeError, ValueError):
        return None
    return parsed.replace(tzinfo=timezone.utc) if parsed.tzinfo is None else parsed


def job_elapsed_seconds(job: dict[str, Any], now: datetime | None = None) -> float | None:
    """Return wall time since a job started, using its finish time if completed."""
    started = parse_timestamp(job.get("started_at"))
    if started is None:
        return None
    completed = parse_timestamp(job.get("completed_at"))
    endpoint = completed or now or datetime.now(timezone.utc)
    return max(0.0, (endpoint - started).total_seconds())


def worker_counts(jobs: list[dict[str, Any]]) -> dict[str, int]:
    counts = {"queued": 0, "running": 0, "succeeded": 0, "failed": 0}
    for job in jobs:
        if job.get("status") == "completed":
            if job.get("conclusion") == "success":
                counts["succeeded"] += 1
            else:
                counts["failed"] += 1
        elif job.get("status") == "in_progress":
            counts["running"] += 1
        else:
            counts["queued"] += 1
    return counts


def format_duration(seconds: float | None) -> str:
    if seconds is None:
        return "not started"
    total = int(seconds)
    hours, remainder = divmod(total, 3600)
    minutes, secs = divmod(remainder, 60)
    return f"{hours:02d}:{minutes:02d}:{secs:02d}"


def _write_github_outputs(path: str | None, *, complete: bool, failed: int) -> None:
    if not path:
        return
    with Path(path).open("a", encoding="utf-8") as output:
        output.write(f"complete={'true' if complete else 'false'}\n")
        output.write(f"failed_workers={failed}\n")


def monitor_run(
    *,
    repository: str,
    run_id: int,
    job_prefix: str,
    expected_jobs: int,
    poll_seconds: int,
    timeout_seconds: int,
    token: str,
    fetcher: Callable[[str, int, str], list[dict[str, Any]]] = fetch_jobs,
    sleep_fn: Callable[[float], None] = time.sleep,
    clock: Callable[[], float] = time.monotonic,
    now_fn: Callable[[], datetime] = lambda: datetime.now(timezone.utc),
    output: Callable[..., None] = print,
    github_output: str | None = None,
    continue_on_timeout: bool = False,
) -> int:
    start = clock()
    last_states: dict[int, tuple[str, str | None]] = {}

    while True:
        try:
            all_jobs = fetcher(repository, run_id, token)
        except RuntimeError as exc:
            output(f"[monitor error] {exc}", flush=True)
            _write_github_outputs(github_output, complete=False, failed=0)
            return 1

        workers = select_worker_jobs(all_jobs, job_prefix)
        if len(workers) > expected_jobs:
            output(
                f"[monitor error] Found {len(workers)} jobs beginning with "
                f"{job_prefix!r}; expected {expected_jobs}.",
                flush=True,
            )
            _write_github_outputs(github_output, complete=False, failed=0)
            return 1

        now = now_fn()
        for job in workers:
            job_id = int(job.get("id", 0))
            state = (str(job.get("status", "unknown")), job.get("conclusion"))
            if last_states.get(job_id) != state:
                output(
                    f"[{now.isoformat(timespec='seconds')}] {job.get('name', 'unnamed job')}: "
                    f"{state[0]}"
                    f"{('/' + str(state[1])) if state[1] else ''}; "
                    f"elapsed={format_duration(job_elapsed_seconds(job, now))}",
                    flush=True,
                )
                last_states[job_id] = state

        counts = worker_counts(workers)
        elapsed = clock() - start
        running_durations = [
            job_elapsed_seconds(job, now)
            for job in workers
            if job.get("status") == "in_progress"
        ]
        longest = max((duration for duration in running_durations if duration is not None), default=None)
        output(
            f"[supervisor] workers={len(workers)}/{expected_jobs} "
            f"queued={counts['queued']} running={counts['running']} "
            f"succeeded={counts['succeeded']} failed={counts['failed']} "
            f"longest_running={format_duration(longest)} "
            f"monitor_elapsed={format_duration(elapsed)}",
            flush=True,
        )

        terminal = len(workers) == expected_jobs and all(
            job.get("status") == "completed" for job in workers
        )
        if terminal:
            for job in workers:
                conclusion = job.get("conclusion") or "unknown"
                output(
                    f"[worker result] {job.get('name', 'unnamed job')}: {conclusion}; "
                    f"duration={format_duration(job_elapsed_seconds(job, now))}; "
                    f"job_id={job.get('id', 'unknown')}",
                    flush=True,
                )
            _write_github_outputs(github_output, complete=True, failed=counts["failed"])
            return 0 if counts["failed"] == 0 else 1

        if elapsed >= timeout_seconds:
            output(
                f"[monitor window ended] Workers are not all terminal after "
                f"{format_duration(elapsed)}; the next supervisor segment should continue.",
                flush=True,
            )
            _write_github_outputs(github_output, complete=False, failed=counts["failed"])
            return 0 if continue_on_timeout else 1

        sleep_fn(min(poll_seconds, max(0.0, timeout_seconds - elapsed)))


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repository", required=True, help="GitHub owner/repository")
    parser.add_argument("--run-id", required=True, type=int, help="GitHub Actions run ID")
    parser.add_argument("--job-prefix", required=True, help="Worker job display-name prefix")
    parser.add_argument("--expected-jobs", required=True, type=int)
    parser.add_argument("--poll-seconds", type=int, default=20)
    parser.add_argument("--timeout-seconds", type=int, required=True)
    parser.add_argument("--github-output", help="Optional GITHUB_OUTPUT file for continuation jobs")
    parser.add_argument(
        "--continue-on-timeout",
        action="store_true",
        help="End this time-bounded supervisor segment successfully so a chained segment can continue.",
    )
    return parser


def main(argv: list[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    if not re.fullmatch(r"[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+", args.repository):
        print("--repository must be formatted as owner/repository.", file=sys.stderr)
        return 2
    if args.expected_jobs < 1 or args.poll_seconds < 1 or args.timeout_seconds < 1:
        print("Job count, polling interval, and timeout must all be positive.", file=sys.stderr)
        return 2
    token = os.environ.get("GITHUB_TOKEN")
    if not token:
        print("GITHUB_TOKEN is required for read-only Actions API access.", file=sys.stderr)
        return 2
    return monitor_run(
        repository=args.repository,
        run_id=args.run_id,
        job_prefix=args.job_prefix,
        expected_jobs=args.expected_jobs,
        poll_seconds=args.poll_seconds,
        timeout_seconds=args.timeout_seconds,
        token=token,
        github_output=args.github_output,
        continue_on_timeout=args.continue_on_timeout,
    )


if __name__ == "__main__":
    raise SystemExit(main())
