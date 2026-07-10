---
name: opus-plan
description: use Claude Opus to plan development for Claude Sonnet
allowed-tools: java, bash
---

Rather than implementing any code, output should be a markdown file in `./ai-output`
that contains a phased implementation plan where each phase fits in the Claude Sonnet 4.6 context window.

After each phase or when the context window is filled the plan should instruct the Agent to write state
to a `./ai-output/PROGRESS.md` file, so that the user can clear the context window and implementation plan
can be continued in a new session starting where the previous session left off.

No changes should be made, other than writing one new plan document.

