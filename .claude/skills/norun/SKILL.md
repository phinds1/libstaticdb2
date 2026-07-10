---
name: norun
description: ensure agents dont run tests when background work is ongoing
allowed-tools: bash, read_file, write_file, sed, grep, find
---

This skill simply states that no project code or tests should be executed during the operation of the agent's task.
Human is running tests in teh background and ports will clash if run together.