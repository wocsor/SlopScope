# Source readability

- Use self-explanatory function, variable, type and constant names in owned code.
- Include units where a value could mean samples, bytes, ticks, milliseconds,
  microvolts, or millidivisions. Name parameters in header declarations.
- Avoid single-letter register helpers and placeholder locals. Conventional loop
  indices, plot coordinates, C library names and required ARM ABI names are fine.
- Use MMIO8/MMIO16/MMIO32 for register access. Preserve access width and order.
- Keep small-block comments to one line; explain broader design in documentation.
- Follow .clang-format, four-space indentation and a 100-column target. Retain upstream vendor naming and
  generated numeric table contents.
- Naming changes must preserve protocol text, serialized layouts, hardware
  sequencing and behavior. Build the affected application and loader variants.
