## What this changes

<!-- What it does and why it is better. Not which files you touched. -->

## How you verified it

<!--
Not "tested locally" — what you actually ran, and what it said. For a fix:
say how you checked the new test fails against the unfixed code.
-->

## Checklist

- [ ] `ctest --test-dir build --output-on-failure` passes, both suites
- [ ] A test covers this, and fails without the change
- [ ] Documentation updated in this change — `docs/` builds with `-W`, so a
      stale cross-reference fails CI
- [ ] What a user would notice is stated above, for the release notes
- [ ] One concern in this pull request

<!--
Worth running before asking for review, since CI will:

  cmake -S . -B build-asan -DCMAKE_BUILD_TYPE=RelWithDebInfo -DSIMUPY_ENABLE_ASAN=ON
  cmake --build build-asan -j$(nproc)
  ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=print_stacktrace=1 \
    QT_QPA_PLATFORM=offscreen ctest --test-dir build-asan --output-on-failure

If you touched examples/make_examples.py or make_libraries.py, run them and
commit the result.
-->
