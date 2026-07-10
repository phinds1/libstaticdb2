---
name: req-bug-impact
description: Look for lingering issues in code when a requirements bug is found and fixed in one area of the code
allowed-tools: bash
---

Occasionally, we find bugs in requirement and a lot of code has been based on the incorrect requirement.
When we find and fix such and issue this skill simply indicates we should check through the rest of the code base looking for 
impact of this earlier bug.

Look at the following things specifically

- requirements docs and .md file sin ai-* directories
- Look at code comments, that might reference the older understanding
- Look at test data in req.js and .json files and JUnit setup methods
- Look as JUnit and jamcrest matcher assertions

Generate a Markdown report of the findings and any code that needs to be 
updated to reflect the new understanding of the requirements in ./ai-output/.