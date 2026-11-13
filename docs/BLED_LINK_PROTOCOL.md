# BLED display <-> measurement unit link

UART0-OUT, 115200 baud, 8N1, 3.3 V logic. Display: **TX = IO43, RX = IO44** (UART0-OUT port); the measurement unit
crosses the lines (unit TX -> display RX IO44, unit RX <- display TX IO43) and shares GND.

## Frame

```
$<TYPE>[,<arg>[,<arg>...]]*<CRC>\r\n
```

* `TYPE`: 1-7 characters `A-Z0-9`. Arguments: comma separated; allowed characters `A-Z a-z 0-9 , . - _ + :`
* `CRC`: **4 upper-case hex digits**, CRC-16/CCITT-FALSE (poly 0x1021, init 0xFFFF, no reflection, no final XOR)
  over the bytes between `$` and `*`. Check value: `CRC("123456789") = 0x29B1`.
* Maximum body 91 bytes, at most 8 arguments. `\n` ends a frame (`\r` optional on receive). A `$` always restarts
  parsing, so garbage between frames is harmless. Frames with a bad CRC or syntax are dropped.
* Example: `$PAD,3,41.2*33E9\r\n`

`src/bled_proto.c/.h` implements all of this (encode, byte-wise parser, strict number parsing) in portable C -
**use the same files in the measurement unit firmware**.

## Messages

| Direction | Frame | Meaning |
|---|---|---|
| display -> unit | `PING` | heartbeat, every 1 s |
| display -> unit | `BASE` | run the optical baseline (re-sent every 2 s, max 3 times, until progress is seen) |
| display -> unit | `CASE,<id>,<hb>` | case started; `<id>` = `[A-Za-z0-9_-]{1,12}`, `<hb>` = patient baseline Hb in g/dL with one decimal (`13.2`). Re-sent every 3 s while the unit reports state 0 (idle) during an active case |
| display -> unit | `ENDC` | case ended |
| unit -> display | `STAT,<state>,<cart>,<pwr>,<batt>` | at least once per second. `state`: 0 idle, 1 baseline running, 2 case active, 3 error. `cart`: 0 none, 1 loaded. `pwr`: 0 AC, 1 battery. `batt`: 0-100 %, -1 unknown. Extra trailing arguments are ignored (forward compatible) |
| unit -> display | `BPRG,<pct>` | baseline progress 0-100 |
| unit -> display | `BDON,<ok>,<i540>,<i850>` | baseline finished. `ok` 1/0; `i540`, `i850` = baseline readings (integers, stored in the case file) |
| unit -> display | `PAD,<n>,<ebl_ml>` | pad `n` (1, 2, 3 ... within the case) measured; `ebl_ml` = blood volume of this pad in mL, 0-500 |
| unit -> display | `ERR,<code>` | 1 no cartridge, 2 sensor fault, 3 reagent empty, 4 optical over-range; other codes shown as "Unit error N" |

The display considers the unit **offline** when no checksum-valid frame arrived for 3 s. Duplicate pad numbers
(`n` not greater than the last accepted one), values outside 0-500 mL and pads received with no active case are
ignored; an out-of-range value additionally raises a warning on the active-case panel.

## Typical sequence

```
display: $PING*....            unit:  $STAT,0,1,0,87*....
display: $BASE*....            unit:  $STAT,1,1,0,87*....  $BPRG,10*....  ...  $BPRG,100*....  $BDON,1,48213,51877*....  $STAT,0,...
display: $CASE,20260413,13.2*....   unit: $STAT,2,1,0,87*....
                               unit:  $PAD,1,38.5*....   (every time a pad is measured)
display: $ENDC*....            unit:  $STAT,0,...
```
