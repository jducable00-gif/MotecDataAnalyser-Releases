# Release Procedure (agent-facing)

How to cut a new version of MotecDataAnalyser so that **installed apps detect it and self-update**.

## Architecture in one paragraph

- **Private repo** `jducable00-gif/MotecDataAnalyser` — all source. Branches:
  - `advanced`: development branch (work happens here).
  - `release`: contains **only two files** (`MotecDataAnalyser.exe`, `Motoc→MotecDataAnalyser.mapping.json`) at the tip of a synthetic commit whose parent is the current `advanced` tip. No source, ever.
- **Public repo** `jducable00-gif/MotecDataAnalyser-Releases` — distribution/update feed only:
  - branch `main`: same two files as private `release`.
  - GitHub Releases (tagged `vX.Y.Z`) with the exe attached as asset named exactly `MotecDataAnalyser.exe`.
- The app ([ReleaseInfo.cpp](../ReleaseInfo.cpp)) does an **unauthenticated** GET on
  `https://api.github.com/repos/jducable00-gif/MotecDataAnalyser-Releases/releases/latest`,
  compares tags, and downloads the first `.exe` asset. Private repos return flat 404s to unauthenticated calls — that is why this public repo exists. **Never point the app back at the private repo.**

Version format: `VX.Y.Z`. The in-app comparison treats a missing segment as 0 (`V5.6` < `v5.6.1`).
The version literal lives in one place only: [`kAppVersion`](../ReleaseInfo.h) (e.g. `"V5.6.2"`).

## Prerequisites / environment notes

- Windows, PowerShell **5.1** (no `&&`, no `<` redirection — use pipes; git writes to stderr on success so red text + exit code 1 is normal for push output: judge by the `[new tag]`/branch lines).
- Build toolchain via [scripts/build.ps1](../scripts/build.ps1) (`.\scripts\build.ps1 -Config Release`) → `Win32\Release\Project1.exe`. If linking fails on a locked exe, kill running instances first: `$p = Get-Process Project1; if ($p) { $p | ForEach-Object { Stop-Process -Id $_.Id } }`.
- GitHub token for API calls (release creation + asset upload): extract from the git credential manager — **never ask the user, never print it**:

  ```powershell
  $cred = 'url=https://github.com' | & git credential fill 2>$null
  $tok = ''
  foreach ($l in @($cred)) { if ("" + $l -like 'password=*') { $tok = "$l".Substring(9) } }
  # use as: Authorization "Bearer $tok"   — log only $tok.Length, never the value
  ```

**Token freshness:** these are one-time OAuth tokens (`gho_...`, length 40). Extract a FRESH token immediately before step 4 and fire release-creation + asset-upload back-to-back in ONE process. Reusing an older extracted token (or waiting minutes between calls) yields `401 Bad credentials` — that is the expected failure mode, not a missing scope; just re-extract once and retry.


- Git remotes already configured locally: `origin` → private repo, `dist` → public dist repo. If missing:
  `git remote add dist https://github.com/jducable00-gif/MotecDataAnalyser-Releases.git`.
- **Filename caveat:** the main form file is on disk as `Motoc→MotecSetupForm.cpp` (mojibake name). Direct editor tools may fail with ENOENT — use wildcard paths (`Get-ChildItem -Filter '*MotecSetupForm.cpp'`) and byte-safe PowerShell edits.

## Procedure

Let `$TAG = vX.Y.Z`, e.g. `v5.6.3`. Work from the repo root on branch `advanced` with a **clean tree**.

### 1 — Bump version, commit, tag (private)

- Minor (`X.Y`) → use [scripts/release.ps1](../scripts/release.ps1):
  ```powershell
  .\scripts\release.ps1 v5.7     # bumps kAppVersion, commits "Release vX.Y", annotated tag, pushes branch + tag to origin
  ```
  It **rejects** `X.Y.Z` patch versions — for those do the same steps manually:

- Patch (`X.Y.Z`) → manual equivalent of release.ps1:
  1. Edit [`kAppVersion`](../ReleaseInfo.h) in [ReleaseInfo.h](../ReleaseInfo.h): `"V5.6.2"` → `"V5.6.3"`. (Normal edit tools work on this file.)
  2. `git add ReleaseInfo.h; git commit -m "Release v5.6.3"`
  3. `git tag -a v5.6.3 -m "Release v5.6.3"` (optional — see warning below)
  4. Push branch + tag: `git push origin advanced` and, only if the tag was created, `git push origin v5.6.3`.

> **⚠️ The private provenance tag is NOT for distribution.** It points at a full-source commit on `advanced`, so its GitHub "zip" download contains ALL source code — never share it or link to it with users (this caused confusion twice: v5.6.1 and v5.6.3). Users only ever get updates from the **public** repo's release assets / dist-`main` zip, which contain exe + mapping.json only. The tag is purely optional provenance metadata; skipping steps 3–4 entirely has zero effect on update detection (the app reads `/releases/latest` of the public repo).

### 2 — Build the release binary

```powershell
$p = Get-Process Project1 -ErrorAction SilentlyContinue; if ($p) { $p | ForEach-Object { Stop-Process -Id $_.Id } }
.\scripts\build.ps1 -Config Release     # expect: BUILD OK -> Win32\Release\Project1.exe, EXITCODE=0
```

### 3 — Create the release-content commit (two files only) and push to BOTH remotes

Build a tree containing exactly `MotecDataAnalyser.exe` (= fresh build) + mapping.json, parented on the new `advanced` tip:

```powershell
$exeSha = & git hash-object -w 'Win32\Release\Project1.exe'
$jsonSrc = (Get-ChildItem . -Filter '*mapping.json').FullName   # wildcard: mojibake-safe
$jsonSha = & git hash-object -w $jsonSrc
# exact tree-entry name as git sees it in HEAD (avoids encoding mismatch):
$entryName = (& git ls-tree --name-only HEAD | Where-Object { $_ -like '*.mapping.json' }).Trim()

$spec = Join-Path $env:TEMP 'rel.tree'
[System.IO.File]::WriteAllLines($spec, @("100644 blob ${exeSha}`tMotecDataAnalyser.exe", "100644 blob ${jsonSha}`t${entryName}"))
$tree = (Get-Content $spec | & git mktree).Trim()               # pipe — PS 5.1 has no '<' redirection

$msgFile = Join-Path $env:TEMP 'rel.msg'
[System.IO.File]::WriteAllText($msgFile, "Release v5.6.3 distribution build`n`nMotecDataAnalyser.exe + mapping.json only (no source). Update feed for the in-app self-update; published to public dist repo.")

$commit = (& git commit-tree $tree -p (git rev-parse HEAD) -F $msgFile).Trim()
& git update-ref refs/heads/release $commit                     # move local release branch
& git push --force origin release                               # private: force — synthetic tip is not a descendant of the old one
& git push --force dist "$($commit):refs/heads/main"            # public: same commit as main
```

Verify both sides show exactly two files: `git ls-tree --name-only origin/release` and `git ls-tree --name-only dist/main`.

### 4 — Publish the GitHub Release on the PUBLIC repo with the exe asset (API)

The app only reads **public** releases, so this is what makes an update visible. Do NOT create a release on the private repo for distribution purposes:

```powershell
$headers = @{ Authorization="Bearer $tok"; Accept='application/vnd.github+json' }   # $tok from Prerequisites; never print it

# 4a. Create the release (raw JSON string — do NOT pipe an already-JSON string through ConvertTo-Json, that double-encodes and yields a 422)
$bodyRaw = '{"tag_name":"v5.6.3","name":"V5.6.3 - Motec Data Analyser","body":"<release notes>","draft":false,"prerelease":false}'
$rel = Invoke-RestMethod -Uri 'https://api.github.com/repos/jducable00-gif/MotecDataAnalyser-Releases/releases' `
        -Headers $headers -ContentType 'application/json; charset=utf-8' `
        -Body ([System.Text.Encoding]::UTF8.GetBytes($bodyRaw)) -Method Post

# 4b. Upload the binary (curl.exe handles binary + auth header reliably)
$url = "https://uploads.github.com/repos/jducable00-gif/MotecDataAnalyser-Releases/releases/$($rel.id)/assets?name=MotecDataAnalyser.exe"
& curl.exe -sS -X POST $url -H "Authorization: Bearer $tok" `
     -H 'Content-Type: application/octet-stream' --data-binary "@Win32\Release\Project1.exe"
# success = response JSON contains a browser_download_url for MotecDataAnalyser.exe
```

Asset name must end in `.exe` (the app takes the first exe asset). `Motoc→MotecDataAnalyser.exe` is the established convention.

### 5 — Verify end-to-end (unauthenticated, exactly what the app does)

```powershell
$r = Invoke-WebRequest -Uri 'https://api.github.com/repos/jducable00-gif/MotecDataAnalyser-Releases/releases/latest' `
        -UseBasicParsing -Headers @{ Accept='application/vnd.github+json'; UserAgent='motec-check' }   # expect HTTP 200, tag = $TAG
$j = $r.Content | ConvertFrom-Json
$exeAsset = ($j.assets | Where-Object { $_.name -like '*.exe' })[0]    # must exist with a browser_download_url

# download URL reachable (302 -> CDN 200, content-length == local exe size):
& curl.exe -sSIL $exeAsset.browser_download_url | Select-String '^HTTP|^content-length'
```

All green → done. Installed apps pick it up on next launch: About box shows the new release + "Update available"; clicking download replaces **only `MotecDataAnalyser.exe`** in place (mapping.json and both INI files are left untouched — they're user-specific / already present). A newer mapping.json only reaches users who install a fresh build.

## Invariants / gotchas

- **Never** commit source to `release`/dist `main`. Keep releases clean: the only asset that matters for updates is the exe (the app picks the first `.exe` asset); mapping.json lives in-repo on dist main as reference and does NOT get re-downloaded by self-update.
- **Never** create distribution Releases on the private repo — unauthenticated clients get 404s there. Private tags/commits are for provenance only.
- `kAppVersion` must be strictly greater than what is installed, or apps will report "up to date". Bump it in step 1 **before** building (step 2) so the binary reports its own version correctly.
- Force-pushes of `release` and dist `main` are expected: each release commit is a fresh synthetic tip parented on `advanced`, not a descendant of the previous one.
- If an asset/release was published wrong, fix it by deleting via API (`DELETE /repos/.../releases/{id}`, assets first) or re-uploading with `-X PUT` to the same asset URL — do not leave stale releases marked *Latest*.
