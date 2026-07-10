---
name: github-workflows
description: create github workflows that run when code is pushed
---

<objective>
every commit is compiled and tested, the gitub release process generates artifacts for other users to download.
</objective>

<process>
create the necessary .yaml files in ./github/workflows/ so that pushing to github runs builds with every commit.
The vm setup should add all the build dependencies and test deps and tool needed for the builds.
</process>
