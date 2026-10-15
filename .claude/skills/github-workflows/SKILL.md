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

Build versioning the .rpm and/or .deb builds should  by default build with a SNAPSHOT tag to the version.
During the github release process the build should be made with the version wihtout the SNAPSHOT tag and the ./version file should have it PATH version incremented and this checked in.
when the user pulls the change and builds they should be building the next snapshot version.
</process>
