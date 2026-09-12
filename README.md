# UGREEN 95348 (MS2130S) firmware patcher

Bake the runtime `0xfc8e = 0x11` luma-processing fix into the firmware of the
UGREEN 95348 USB 3.0 HDMI capture stick (MacroSilicon MS2130S). Once patched,
the fix survives every UVC stream (re)start and no host-side helper is needed.

The approach follows
[steve-m/ms2130_patcher](https://github.com/steve-m/ms2130_patcher), which bakes
hsdaoh's transparent-mode register writes into an MS2130 firmware.

> **Back up your original firmware before flashing anything.** The patcher
> refuses to touch a file whose checksums do not match the one revision it
> was written for, but flashing a wrong image can still brick the device.

## TL;DR

```console
make
./ms2130s_patch                    # writes ..._0xfc8e_0x11.bin next to the input
```

Then flash `UGREEN_undated_backup_24dc_797c_0xfc8e_0x11.bin` (see
[Flashing](#flashing)).

## The problem

The MS2130S performs a luma-processing step that lifts the mid-tones of a
full-range signal. Without the fix, a source level of 200 is delivered to the
host as raw limited-range 204 instead of 188; decoded back to full range that
becomes 219.

The register that controls this, XDATA `0xfc8e`, is reset to `0x00` (luma
processing enabled) by the firmware every time the UVC stream starts, so a
one-shot host-side write does not stick. Patching the firmware makes that
reset write `0x11` (luma processing disabled) instead.

## Repository contents

| Path | Description |
|---|---|
| `ms2130s_patch.c` | The patcher (single C file, no dependencies). |
| `Makefile` | Builds the `ms2130s_patch` binary. |
| `UGREEN_undated_backup_24dc_797c.bin` | Original UGREEN 95348 firmware dump. |
| `UGREEN_undated_backup_24dc_797c_0xfc8e_0x11.bin` | Pre-built patched firmware, byte-for-byte identical to the output of the tool. |

## Build

Requirements: a C compiler and `make` (tested with `gcc` on Linux and macOS).

```console
make
```

## Usage

```console
./ms2130s_patch [input.bin] [output.bin]
```

Both arguments are optional and default to the files checked into this
repository:

* input:  `./UGREEN_undated_backup_24dc_797c.bin`
* output: `./UGREEN_undated_backup_24dc_797c_0xfc8e_0x11.bin`

If the input's header checksum is not `0x24dc` or its code checksum is not
`0x797c`, the tool prints an error and refuses to patch: it is hard-wired to
this one firmware revision.

```console
$ ./ms2130s_patch
Length of file: 98356
Original header checksum matches: 24dc
Original code checksum matches: 797c
New code checksum: 797e
Wrote patched firmware: ./UGREEN_undated_backup_24dc_797c_0xfc8e_0x11.bin
  0xfc8e = 0x11 (luma processing disabled)
```

## How the patch works

### The reset routine

`0xfc8e` has two relevant bits: bit 0 (mask `0x01`) and bit 4 (mask `0x10`).
The stream-reinit routine `FUN_CODE_c220()` clears both through the bit-mask
helper `FUN_CODE_87c7(mask, addrH, addrL, value)`. The value is passed in
`R3`: non-zero sets the masked bits, zero clears them.

| CPU addr (bank 1) | code | effect |
|---|---|---|
| `c268` | `MOV R3,#01h ; JNB bit05,c26f ; MOV R3,#00h`<br>`MOV R5,#01h ; MOV R7,#8eh ; MOV R6,#fch ; LJMP 87c7h` | clear bit 0 of `0xfc8e` |
| `c27e` | `MOV R3,#01h ; JNB bit05,c285 ; MOV R3,#00h`<br>`MOV R5,#10h ; MOV R7,#8eh ; MOV R6,#fch ; LJMP 87c7h` | clear bit 4 of `0xfc8e` |

After both calls `0xfc8e = 0x00`. The companion `0xfc8f` (chroma) writes at
`c294`/`c2a4` are **not** touched; the grey-level fix only needs `0xfc8e`.

### The change

Both `MOV R3,#00h` (`7b 00`) instructions are changed to `MOV R3,#01h`
(`7b 01`), so each mask update always takes the *set* path and the register
ends up `0x11`.

| file offset | original | patched | meaning |
|---:|---:|---:|---|
| `0x1429e` (bank1 `c26e`) | `00` | `01` | value operand for bit 0 of `0xfc8e` |
| `0x142b4` (bank1 `c284`) | `00` | `01` | value operand for bit 4 of `0xfc8e` |
| `0x18033` | `7c` | `7e` | code checksum `0x797c` → `0x797e` |

## Verify

The header checksum is unchanged and the code checksum is recomputed by the
patcher:

```console
$ ./ms2130s_patch            # prints the checksums before/after
```

Disassembling the patched bytes shows both immediates now load `0x01`:

```console
c268: 7b01  MOV R3, #01h
c26a: 300502 JNB bit05, c26fh
c26d: 7b01  MOV R3, #01h      <- was #00h
c26f: 7d01  MOV R5, #01h
c271: 7f8e  MOV R7, #8eh
c273: 7efc  MOV R6, #fch
c275: 0287c7 LJMP 87c7h
```

## Flashing

Use [steve-m/ms213x_flash](https://github.com/steve-m/ms213x_flash) (back up
first). Its `known_devices[]` table does not know the UGREEN stick's USB ID
`2b89:5348`, so add it (line numbers may drift):

```diff
--- a/ms213x_flash.c
+++ b/ms213x_flash.c
@@ -58,6 +58,7 @@ static adapter_t known_devices[] = {
 	{ 0x345f, 0x2132, 4, true,  "MS2130S" },
 	{ 0x345f, 0x2133, 4, true,  "MS2131S" },
 	{ 0x345f, 0x0001, 0, true,  "MS213xS-ROM" },
+	{ 0x2b89, 0x5348, 4, true,  "UGREEN" },
 };
```

On macOS, force the use of hidapi, otherwise libusb will not be able to claim
the interface.

## Expected result

With `0xfc8e = 0x11`, the chip's luma processing is disabled and the mid-tone
lift disappears: a full-range source level of 200 is delivered as raw
limited-range 188 (decoding to 200) instead of 204 (decoding to 219).

## License

MIT. See the SPDX header in [`ms2130s_patch.c`](ms2130s_patch.c).

## Acknowledgements

* [steve-m/ms2130_patcher](https://github.com/steve-m/ms2130_patcher) and
  [steve-m/ms213x_flash](https://github.com/steve-m/ms213x_flash) for the
  patching idea and the flashing tool.
* The [hsdaoh](https://github.com/steve-m/hsdaoh) project for documenting the
  transparent-mode registers (`0xfc8e`, `0xfc8f`, …).
