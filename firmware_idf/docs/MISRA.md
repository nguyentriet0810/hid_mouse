# MISRA C engineering record

## Status and scope

Target: MISRA C:2012 with project-specific deviations for ESP-IDF 5.3.1.
This is a **MISRA-oriented implementation, not a verified compliance claim**.
A complete MISRA compliance report requires a licensed rule baseline, an
appropriate checker, project configuration, whole-program analysis, manual
reviews, and approval of individual deviations. Those steps are not replaced
by a clean compiler build or passing unit tests.

Production scope: `main/*.c`, `components/*/*.c`, and their public headers.
Third-party scope: ESP-IDF, FreeRTOS, compiler runtime and system headers.
Build tools, host tests and generated code are outside production scope.

## Applied conventions

- C source only; explicit-width integer types for domain data and wire formats.
- Include guards, prefixed external symbols, internal functions/data `static`.
- Public function prototypes and single return points; no recursion or goto.
- Braces on control statements, explicit switch defaults, finite startup waits.
- No application malloc/free, VLA, pointer arithmetic over arbitrary objects,
  union reinterpretation, printf in core logic, or packing-dependent reports.
- Bounds checked before array indexing, shifts and narrowing conversions.
- Saturate finite floating-point values before converting to HID int8_t.
- Decode two's complement through int32_t rather than implementation-defined
  conversion from uint16_t to int16_t.
- I2C transactions have finite deadlines. Cleanup and API return codes checked.
- Callbacks only update state/configure the SDK; they never run sensor loops
  or wait for Bluetooth notifications. Shared connection state is protected by
  a FreeRTOS spinlock, with no vendor calls while holding it.
- Button edges are acknowledged only when the SDK accepts a report. Motion
  reports may be discarded under congestion; button edges have a bounded FIFO.
- Reconnect requires physical button release. Overflow forces a release report.

## Deviation register (requires review, not blanket approval)

| ID | Area / candidate rules | Reason and containment |
|---|---|---|
| D-01 | SDK headers/macros, directives and essential types; rules 1.1, 10.x, 20.x | ESP-IDF/FreeRTOS use extensions, vendor macros, bitfields and generated config. Isolate them in adapters; analyze vendor code separately. No generic suppression for application code. |
| D-02 | GATT attribute object representations; rule 11.3, advisory 8.13 | Vendor GATT API requires uint8_t pointers for UUID/property storage. Writable objects have static lifetime; conversion is only in ATTRIBUTE macro. No const removal or pointer-to-integer cast. Review alignment and uint8_t == unsigned char on the target. |
| D-03 | SDK variadic log macros; rule 17.1 and related preprocessing rules | Logging is confined to adapters/main, uses literal formats and explicit format-compatible argument casts. No log calls while holding a spinlock. |
| D-04 | Floating math and compiler libc | atan2f/sqrtf/fabsf/isfinite are required for the complementary filter. Validate sample/config bounds, positive dt, and finite inputs before calculations; review target math library behavior and floating-point assumptions. |
| D-05 | Tick/millisecond wraparound; rule 12.4 review | Unsigned runtime subtraction intentionally uses modulo arithmetic for elapsed timers. Host tests cover UINT32_MAX rollover. No constant-expression wraparound is intentional. |
| D-06 | External app_main and vendor callback signatures; rules 8.4/8.13 review | app_main has an explicit prototype. Callback parameter types are imposed by the SDK. Read-only helpers use const-qualified pointed-to data. |
| D-07 | FreeRTOS/SDK memory lifecycle; directive 4.12 review | Application contexts and event-group storage are static or automatic. SDK I2C/Bluetooth initialization allocates internally; failure is handled at startup. This is not a claim of heap-free whole-system operation. |
| D-08 | Deliberately ignored results; rule 17.7 review | `(void)` only for APIs whose result is intentionally unused: memcpy's destination, event-group bit snapshots on mutation, and delay-until's timing status. Error-bearing vendor calls are checked or logged. |
| D-09 | OTA URL formatting and SDK byte streams; rule 21.6 and representation-cast review | snprintf uses literal formats, fixed buffers and checked lengths. HTTP char buffers view uint8_t object representations. No stdio/heap in ota_policy. TLS/HTTP/NVS allocation is owned by the vendor adapters and checked/cleaned up. |

Vendor headers use undefined CONFIG macros as zero. A GCC diagnostic push/pop
suppresses only `-Wundef` while including vendor headers, under D-01. The warning
remains enabled for application code. No conversion or general diagnostic is
disabled to compile application logic.

These rule associations are review candidates and are not an exhaustive
rule-by-rule coverage matrix. Any additional finding must be fixed or receive
a narrow documented deviation with justification and reviewer sign-off.

## Verification required for a compliance claim

1. Freeze SDK version, compiler, sdkconfig, dependency versions and target ABI.
2. Import `build/compile_commands.json` into a MISRA-capable checker such as
   Helix QAC, PC-lint Plus, or another qualified project toolchain.
3. Analyze all build variants and cross-translation-unit interfaces. Tool results
   must distinguish production source, approved deviations and vendor findings.
4. Review mandatory/required/advisory rule coverage and undecidable rules.
5. Resolve findings; approve deviations individually. Archive analyzer version,
   settings, reports, compiler diagnostics, unit tests and hardware evidence.

No MISRA-capable analyzer was found or run during source authoring. GCC's
general path analyzer is run separately and is not a MISRA checker.
Compiler diagnostics and unit tests are recorded independently in VALIDATION.md.
