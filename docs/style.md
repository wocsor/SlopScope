# Source conventions

Use function names that state the operation and variable names that state the
value they hold. Avoid single-letter helpers and placeholders such as `R`,
`p`, `cfg` or `k`. Standard loop indices, plot coordinates, C library functions
and ARM ABI entry names may keep their conventional names. Public function
declarations must name their parameters.

Use names that describe the operation and unit, especially for voltages, timing,
positions and FPGA registers. Prefer a small function for a meaningful state
transition. Keep scheduler order explicit. Use `MMIO8`, `MMIO16` and `MMIO32` for
width-specific memory-mapped register access; these macros are lvalues for both
reads and writes.

Use four-space indentation and a 100-column target. `.clang-format` defines the
C style; `pyproject.toml` defines Python formatting. Small implementation comments
should be one line and explain a constraint or hardware detail. Put longer
explanations in documentation. Generated tables and upstream LittleFS retain
their own layout.

Keep device behavior in SlopScope, USB transport and reusable host operations
in the Python library, and browser/server behavior in the web UI project. Keep
reverse-engineering evidence outside the source repositories. Preserve settings/wire
formats when reorganizing runtime code. Changes to acquisition ordering or
hardware sequencing need an explicit behavioral reason.

Build after edits. Use bounded on-hardware checks for behavior changes and keep
evidence out of source control. This cleanup did not introduce or run an
automated test suite.
