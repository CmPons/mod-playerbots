# Playerbots fork workflow

This is our maintained `CmPons/mod-playerbots` fork, branch `local-playerbot`.

- Every completed source/config-template/test/documentation change MUST be committed and pushed: `git push fork HEAD:local-playerbot`.
- Inspect diffs and untracked source files. Do not leave work only in the parent deployment repo's historical patches or temporary backups.
- Verify that the remote branch tip equals local HEAD. If a push fails, report the failure and unpushed commits; do not call the work saved.
- Update this module's commit in the parent deployment repo's `repo-pins.txt`, then commit/push that repo too. Follow its `AGENTS.md` and `Documents/source-workflow.md`.
- Never reset/clean away changes, force-push, or automatically stash work. Preserve unexpected changes and report them.
- Never commit credentials, live configs, database dumps or build output.
- No server/container/Pi bridge stop, restart or recreation without explicit user authorization. Publishing source does not authorize deployment.
