# fixtures — P1 telegrams for host-side tests

Byte-exact. The DSMR CRC16 (CRC-16/ARC) is computed over the telegram from `/`
through `!` inclusive, so a single added, dropped or rewritten byte invalidates
a fixture. Never "tidy" these files: no line-ending conversion, no trailing
newline, no editor reformatting. `.gitattributes` marks `*.p1` binary to stop
git normalising them.

Check them at any time with:

    python tools/p1crc.py fixtures/<name>.p1

| File | Source | Telegrams | Covers |
|---|---|---|---|
| `belgian-3phase-fluvius.p1` | **Real Fluvius capture**, Aug 2023 (identifiers replaced) | 10, all CRC-valid | Fluvius three-phase, capacity-tariff fields. No MBUS devices. |
| `belgian-mbus-gas-water-spec.p1` | **Spec-derived**, not captured | 1, CRC computed | Gas on MBUS channel 1 (`0-1:24.2.3`), water on channel 2 (`0-2:24.2.1`), device types and equipment IDs. |
| `dutch-3phase-esmr5-simulator.p1` | Benefactus simulator, Sept 2026 | 3, all CRC-valid | Dutch ESMR 5.0 three-phase, gas on the NL code `0-1:24.2.1`. |
| `corrupt-crc.p1` | Derived from the real capture | 1, CRC invalid **by construction** | One value byte altered, original CRC kept — what a noisy P1 line produces. A parser **must reject this**. |

## The two Belgian fixtures are complementary

Neither covers everything, and it is worth knowing which is which:

- `belgian-3phase-fluvius.p1` has the **capacity-tariff** fields — `1-0:1.4.0`
  running quarter-hour average demand, `1-0:1.6.0` monthly maximum with
  timestamp, and the awkward multi-value `0-0:98.1.0` 13-month peak history.
  It has **no gas or water**, because that connection has no MBUS devices.
- `belgian-mbus-gas-water-spec.p1` has **gas and water**, but is an older
  profile (`0-0:96.1.4(50216)`) without the capacity-tariff fields.

Test against both.

## Honesty about `belgian-mbus-gas-water-spec.p1`

This one was **not captured from a meter**. It is the worked example from
Fluvius's *Smart Metering System — e-MUCS P1 v1.6* specification
(`partner.fluvius.be`), which ships with a `!XXX` placeholder instead of a real
checksum. The telegram body is the spec's verbatim, assembled with CRLF line
endings and a blank line after the identification line to match how real meters
frame telegrams (confirmed against the real capture), and the CRC was then
computed with `tools/p1crc.py`.

So it is structurally authentic and self-consistent, but the byte stream never
crossed a wire. It exercises field handling; it does not prove anything about
real-world quirks. If a real Belgian meter with gas ever becomes available,
replace it.

## Provenance note

`belgian-3phase-fluvius.p1` contains a real meter identifier (`0-0:96.1.1`) and
real consumption figures from Joannes's own connection. Decide whether to
identifiers in this capture and the files derived from it have been replaced. Changing telegram bytes
invalidates the CRC; the valid fixtures were recomputed:

    python tools/p1crc.py fixtures/belgian-3phase-fluvius.p1 --fix

## Recovering old Arduino captures

`archive/P1logthuis2.txt` was pasted out of the Arduino IDE serial monitor and
is triply mangled: per-line timestamps, CR and LF rewritten as the literal
two-character sequences `\r` and `\n`, and real CRLFs added around them. Every
CRC failed. `tools/recover_arduino_log.py` reverses all three:

    python tools/recover_arduino_log.py archive/P1logthuis2.txt --check
    python tools/recover_arduino_log.py archive/P1logthuis2.txt -o fixtures/name.p1

It refuses to write unless every telegram validates, which is what makes the
recovery byte-exact rather than merely plausible.
