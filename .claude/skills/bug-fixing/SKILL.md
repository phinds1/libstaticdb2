---
name: bug-fixing
description: ensure that wi fix bugs with tests
allowed-tools: bash
---

When fixing a bug we should always write a test that catches the bug, if an existing test has not caught the bug already.

First we find the bug, and work out what a fix is, we can write the fix immediately and the user can use version control tools, 
to apply and unapply the fix.
Test the new code compiles and all existing tests run.
Then we find a way to replicate the bug in a test.
Write that test, run it, it should pass with the fix applied.
Revert the old code and verify the test fails without the fix, and then re-apply the fix again to verify the test passes with the fix.
