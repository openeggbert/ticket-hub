# Final package verification

Date: 2026-07-30

The source copied into `project/` was rebuilt after the handoff documentation was added.

## Command shape

```bash
cmake -S project -B <temporary-build> \
  -DTICKETHUB_BUILD_SERVER=OFF \
  -DTICKETHUB_WITH_POSTGRES=ON \
  -DTICKETHUB_WITH_SQLITE=ON
cmake --build <temporary-build> --parallel 4
ctest --test-dir <temporary-build> --output-on-failure
node --check project/web/app.js
<temporary-build>/ticket-hub-cli version
<temporary-build>/ticket-hub-cli diagnostics
```

## Result

- Configuration succeeded with GCC 14.2.0.
- SQLite 3.46.1 found.
- libpq/PostgreSQL client 17.9 found.
- Core and CLI compiled.
- SQLite and PostgreSQL adapters compiled and linked.
- 3/3 tests passed:
  - `ticket-hub-domain-tests`
  - `ticket-hub-migration-tests`
  - `ticket-hub-sqlite-integration-tests`
- Frontend JavaScript syntax validation passed.
- CLI reported version `0.2.0`.
- CLI diagnostics correctly redacted the database URL.

The server target was not revalidated because the source environment still had no installed Crow package and could not fetch it from GitHub. Rebuild the server target in Claude Code's environment when Crow is available.
