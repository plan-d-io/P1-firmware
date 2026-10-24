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
| `belgian-1phase-derived.p1` | **Derived**, not captured — first Fluvius telegram with L2/L3 lines removed and CRC recomputed | 1, CRC valid | Single-phase field set: L1 present, L2/L3 absent. |
| `belgian-mbus-gas-water-spec.p1` | **Spec-derived** (e-MUCS P1 **v1.6**), not captured | 1, CRC computed | Older profile (`50216`). Gas on `0-1:24.2.3`, water on `0-2:24.2.1`. No capacity tariff. |
| `belgian-full-spec-v21.p1` | **Spec-derived** (e-MUCS P1 **v2.1**), not captured | 1, CRC computed | Current profile (`50221`). Capacity tariff *and* MBUS together, grid config, EAN codes, per-channel breaker state. Water on `0-2:24.2.**3**`. |
| `dutch-3phase-esmr5-simulator.p1` | Benefactus simulator, Sept 2026 | 3, all CRC-valid | Dutch ESMR 5.0 three-phase, gas on the NL code `0-1:24.2.1`. |
| `corrupt-crc.p1` | Derived from the real capture | 1, CRC invalid **by construction** | One value byte altered, original CRC kept — what a noisy P1 line produces. A parser **must reject this**. |

## Water is published on two different OBIS codes

This is not a mistake in the fixtures. The **e-MUCS P1 v2.1 prose says water is
`0-n:24.2.1`**, but **the worked example in that same document publishes water
on `0-2:24.2.3`** — the code its prose assigns to gas. The older v1.6 example
uses `0-2:24.2.1`.

**Neither is supported at present, deliberately.** Water metering over P1 is
very rarely used in Flanders. `dsmr_parser` covers `24.2.1` out of the box; the
`24.2.3` variant is not added, on the grounds that if Belgian water metering
becomes common the upstream maintainer will likely support it (the library
already carries Belgian `_be` field variants), or we send a pull request rather
than carrying a local fork of one line.

The two spec fixtures still differ on this point, which makes
`belgian-full-spec-v21.p1` useful as a test that **unknown OBIS codes are
ignored rather than fatal** — the telegram must parse fine with its water line
simply unrecognised.

## The Belgian fixtures are complementary

Neither covers everything, and it is worth knowing which is which:

- `belgian-3phase-fluvius.p1` has the **capacity-tariff** fields — `1-0:1.4.0`
  running quarter-hour average demand, `1-0:1.6.0` monthly maximum with
  timestamp, and the awkward multi-value `0-0:98.1.0` 13-month peak history.
  It has **no gas or water**, because that connection has no MBUS devices.
- `belgian-mbus-gas-water-spec.p1` (v1.6, `50216`) has **gas and water** but no
  capacity tariff, and publishes water on `24.2.1`.
- `belgian-full-spec-v21.p1` (v2.1, `50221`) has **both** capacity tariff and
  MBUS, plus fields neither of the others carries: `1-0:94.32.1` grid
  configuration (400 = 3N400V three-wattmeter, 230 = 3x230V two-wattmeter),
  `0-0:96.1.2` EAN codes, and `0-n:96.3.10` breaker state for MBUS channels 1
  to 4. It publishes water on `24.2.3`.

Test against all of them.

### Note on `0-0:98.1.0`

The v2.1 spec says **up to 15 entries**, most recent first. The real capture has
**3 entries in ascending order**, and the v2.1 example has **3 in descending
order**. So neither the count nor the ordering can be assumed — read the count
field and take the entries as they come.

The spec defines **no sentinel** for a month with no recorded peak; it says such
months are simply not published. The real meter disagrees: it emits
`(632525252525W)(00.000*kW)`, a timestamp of month 25, day 25, hour 25. Reality
wins — a parser must tolerate it.

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
the original consumption figures. Meter identifiers were replaced. Newer meters also
publish `0-0:96.1.2`, the 18-character **EAN code of the grid connection**,
which identifies the address. The
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
