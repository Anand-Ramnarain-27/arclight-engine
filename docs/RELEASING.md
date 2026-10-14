# Releasing Arclight

Releases are built by GitHub Actions, not on your PC. Every step below happens in GitHub Desktop or on github.com, so no terminal is needed.

## One-time setup

The release workflow must be on `main`: `.github/workflows/release.yml`.

To add it on github.com: open the repo, then **Add file > Create new file**. Type `.github/workflows/release.yml` as the name (typing `/` creates the folders), paste the file's contents, and click **Commit changes**. Then, in GitHub Desktop, click **Fetch origin** and **Pull origin** so your PC has it too.

## Every release

### 1. Prepare the version (in your editor, then GitHub Desktop)
1. `CMakeLists.txt`: set `project(Arclight VERSION x.y.z ...)`.
2. `CHANGELOG.md`: move the items under `[Unreleased]` into a new `## [x.y.z] - YYYY-MM-DD` section, and add the compare/tag links at the bottom.
3. Create `docs/releases/vx.y.z.md`. This is the text that appears on the GitHub release page. Copy the previous one and edit it.
4. In GitHub Desktop: commit to `main` (e.g. "Prepare release x.y.z"), then **Push origin**.

### 2. (Optional) Test build before releasing
1. On github.com, go to **Actions > Release > Run workflow** (branch `main`) and click **Run workflow**.
2. Wait for both jobs to go green (about 10 minutes).
3. Open the run. Under **Artifacts**, download `windows-release`, extract it, and check that `Arclight.exe` starts.

This run builds and tests only. It does not create a release.

### 3. Publish the release

Choose **A** or **B**. Both end with the same result.

**A. On github.com (simplest)**
1. Click **Releases** (right sidebar) > **Draft a new release**.
2. **Choose a tag**: type `vx.y.z` and click **Create new tag: vx.y.z on publish**. The target is `main`.
3. Title: `Arclight x.y.z`. Description: paste the contents of `docs/releases/vx.y.z.md`.
4. Click **Publish release**.
5. Publishing creates the tag, and the tag starts the Release workflow. When it finishes (about 10 minutes, under **Actions**), the two zips are attached to the release automatically. Until then the release only has the source-code downloads.

**B. With GitHub Desktop**
1. Go to the **History** tab, right-click the release commit, choose **Create Tag…**, and enter `vx.y.z`.
2. Click **Push origin**, which pushes the tag too.
3. The workflow builds both zips and creates a **draft** release with the notes from `docs/releases/vx.y.z.md`.
4. On github.com, go to **Releases**, open the draft, check it, and click **Publish release**.

### 4. Check it
- The release has `Arclight-x.y.z-windows-x64.zip` and `Arclight-x.y.z-macos-arm64.zip`.
- Download the Windows zip, extract it, and run it.

## If something goes wrong
- **A workflow job is red:** open the run in **Actions** and click the failed step to see the log. Fix the problem, commit, and push. Then either delete the release and tag and redo step 3, or run the failed jobs again from the run page.
- **Delete a release and its tag:** Releases > the release > **Delete**. Then go to **Tags**, open the tag, and **Delete**. Deleting a release does not delete its tag.
- **Wrong version number in the zip name:** the version comes from `project(... VERSION ...)` in `CMakeLists.txt`, not from the tag. Keep them the same.

## Version numbers
`MAJOR.MINOR.PATCH` ([Semantic Versioning](https://semver.org/)). While on 0.x, each roadmap phase is a minor version (Phase 1 = 0.2.0), and bug-fix releases increase the patch number.
