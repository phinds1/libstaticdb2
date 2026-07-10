---
name: security
description: look for security issues and crash bugs in the Java code
allowed-tools: bash
---

Occasionally we should run security anyalisis on code to see if any new changes have introduced security issues or future security risks.

The for user specified files each file should be reviewed looking for any issues where code might  or open remote execs.

- unvalidated user input that might cause issues
- unvalidated input in common public API type code where the called might reasonably not understand the consequences of passing bad input
- cache poisoning, where HTTP input can be used to manipulate data in the caches
- issues where performance might be affected by user input and occupy the CPU for too long

## Assumptions

We can safely assume input is valid HTTP in this project, but should be resilient to malformed HTTP anyway, such issues should be considered low severity.
We can safely assume that spring-boot is tested and trusted.

DO NOT USE GSD (getting-shit-done)

Do not make any changes to code or configuration, preduce a report.

