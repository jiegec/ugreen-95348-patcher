# UGREEN 95348 (MS2130S) firmware patcher

Bake the runtime `0xfc8e = 0x11` (luma) and `0xfc8f = 0x11` (chroma)
processing fixes into the firmware of the UGREEN 95348 USB 3.0 HDMI capture
stick (MacroSilicon MS2130S). Once patched, the fix survives every UVC stream
(re)start and no host-side helper is needed.

The approach follows
[steve-m/ms2130_patcher](https://github.com/steve-m/ms2130_patcher), which bakes
hsdaoh's transparent-mode register writes into an MS2130 firmware.

The reverse engineering and measurements behind this fix are written up in the
accompanying [blog post](https://jia.je/hardware/2026/09/12/fix-ugreen-95348/)
(in Chinese).

> **Back up your original firmware before flashing anything.** The patcher
> refuses to touch a file whose checksums do not match the one revision it
> was written for, but flashing a wrong image can still brick the device.

## TL;DR

```console
make
# register fix only
./ms2130s_patch

# register fix + an EDID variant
./ms2130s_patch --edid edid_srgb_rgb444_4k30.bin \
    UGREEN_undated_backup_24dc_797c.bin \
    UGREEN_undated_backup_24dc_797c_0xfc8e_0xfc8f_0x11_edid_srgb_rgb444_4k30.bin
./ms2130s_patch --edid edid_srgb_rgb444_4k30_it.bin \
    UGREEN_undated_backup_24dc_797c.bin \
    UGREEN_undated_backup_24dc_797c_0xfc8e_0xfc8f_0x11_edid_srgb_rgb444_4k30_it.bin
```

| output | contents |
|---|---|
| `..._0xfc8e_0xfc8f_0x11.bin` | register fix only (proc-amp bypassed) |
| `..._edid_srgb_rgb444_4k30.bin` | register fix + sRGB/RGB 4:4:4 EDID, 4K30 CEA preferred, `QS=1` |
| `..._edid_srgb_rgb444_4k30_it.bin` | register fix + all-IT CVT-RB EDID, 4K30 CVT-RB preferred |

## The problem

The MS2130S performs a luma-processing step that lifts the mid-tones of a
full-range signal. Without the fix, a source level of 200 is delivered to the
host as raw limited-range 204 instead of 188; decoded back to full range that
becomes 219.

The luma register that controls this, XDATA `0xfc8e`, is reset to `0x00`
(luma processing enabled) by the firmware every time the UVC stream starts,
and the companion chroma register `0xfc8f` is reset the same way, so a
one-shot host-side write does not stick. Patching the firmware makes those
reset writes use `0x11` (luma/chroma processing disabled) instead.

## Repository contents

| Path | Description |
|---|---|
| `ms2130s_patch.c` | The patcher (single C file, no dependencies). |
| `Makefile` | Builds the `ms2130s_patch` binary. |
| `UGREEN_undated_backup_24dc_797c.bin` | Original UGREEN 95348 firmware dump. |
| `..._0xfc8e_0xfc8f_0x11.bin` | Pre-built register-fix image. |
| `edid_srgb_rgb444_4k30.bin` | 256-byte EDID: sRGB, RGB 4:4:4, 4K30 CEA preferred, `QS=1`. |
| `..._edid_srgb_rgb444_4k30.bin` | Pre-built image with that EDID. |
| `edid_srgb_rgb444_4k30_it.bin` | 256-byte EDID: all-IT CVT-RB (4K30, 1080p60, 1440p60, 1440p30, 1080p30). |
| `..._edid_srgb_rgb444_4k30_it.bin` | Pre-built image with that EDID. |

## Build

Requirements: a C compiler and `make` (tested with `gcc` on Linux and macOS).

```console
make
```

## Usage

```console
./ms2130s_patch [--edid FILE] [input.bin] [output.bin]
```

* `--edid FILE`: replace the 256-byte EDID at file offset `0x307` with
  `FILE`.  The file must be exactly 256 bytes with valid base and CTA
  checksums; the tool checks both and recomputes the firmware code checksum.
* Without `--edid`: only the register fix is applied.
* Defaults: input `./UGREEN_undated_backup_24dc_797c.bin`; output
  `..._0xfc8e_0xfc8f_0x11.bin` (or `..._edid.bin` with `--edid`).
* The tool refuses an input whose header/code checksums are not `0x24dc` /
  `0x797c`.

## EDID variants

The EDID is data, not code: the tool checks the length and the two EDID
checksums, copies the 256 bytes over `0x307`, and recomputes the firmware
code checksum.

| file | contents |
|---|---|
| `edid_srgb_rgb444_4k30.bin` | sRGB + RGB 4:4:4; preferred 4K30 CEA (297 MHz); no YCbCr 4:4:4/4:2:2/4:2:0; VCDB `QS=1`. |
| `edid_srgb_rgb444_4k30_it.bin` | All-IT CVT-RB: preferred 4K30 (262.75 MHz), 1080p60 (138.50), 1440p60 (241.50), 1440p30 (119.00), 1080p30 (68.25); no Video Data Block, no 4:2:0 block, no VCDB; HDMI VSDB keeps max TMDS 300 MHz but clears `HDMI_Video_Present` (no HDMI VIC list); audio and speaker allocation kept. |

The all-IT variant uses only CVT-RB timings, so no mode matches a CTA-861
Video Format; a source that follows the CTA default then sends full-range
RGB.  No CE timing remains.

Any other 256-byte EDID blob can be passed with `--edid`; generate it with
an EDID editor or your own script.

## Example

```console
$ ./ms2130s_patch --edid edid_srgb_rgb444_4k30_it.bin \
    UGREEN_undated_backup_24dc_797c.bin out_it.bin
Length of file: 98356
Original header checksum matches: 24dc
Original code checksum matches: 797c
EDID replaced from edid_srgb_rgb444_4k30_it.bin (base checksum fe, CTA checksum 18)
New code checksum: 7380
Wrote patched firmware: out_it.bin
  0xfc8e bit 0 = 0x01 (processing disabled)
  0xfc8e bit 4 = 0x01 (processing disabled)
  0xfc8f bit 0 = 0x01 (processing disabled)
  0xfc8f bit 4 = 0x01 (processing disabled)
```

## Verify the EDID

```console
dd if=out_it.bin bs=1 skip=$((0x307)) count=256 | edid-decode -
```

The all-IT variant shows five DTDs, no Video Data Block and no HDMI VICs.

## How the patch works

### The reset routine

XDATA `0xfc8e` (luma) and `0xfc8f` (chroma) each have two relevant bits:
bit 0 (mask `0x01`) and bit 4 (mask `0x10`). The stream-reinit routine
`FUN_CODE_c220()` clears all four through the bit-mask helper
`FUN_CODE_87c7(mask, addrH, addrL, value)`. The value is passed in `R3`:
non-zero sets the masked bits, zero clears them.

| CPU addr (bank 1) | code | effect |
|---|---|---|
| `c268` | `MOV R3,#01h ; JNB bit05,c26f ; MOV R3,#00h`<br>`MOV R5,#01h ; MOV R7,#8eh ; MOV R6,#fch ; LJMP 87c7h` | clear bit 0 of `0xfc8e` |
| `c27e` | `MOV R3,#01h ; JNB bit05,c285 ; MOV R3,#00h`<br>`MOV R5,#10h ; MOV R7,#8eh ; MOV R6,#fch ; LJMP 87c7h` | clear bit 4 of `0xfc8e` |
| `c294` | `MOV R3,#01h ; JNB bit05,c29b ; MOV R3,#00h`<br>`MOV R5,#01h ; MOV R7,#8fh ; MOV R6,#fch ; LJMP 87c7h` | clear bit 0 of `0xfc8f` |
| `c234` | `MOV R3,#01h ; JNB bit05,c23b ; MOV R3,#00h`<br>`MOV R5,#10h ; MOV R7,#8fh ; MOV R6,#fch ; LJMP 87c7h` | clear bit 4 of `0xfc8f` |

After these calls both registers are `0x00`.

### The change

All four `MOV R3,#00h` (`7b 00`) instructions are changed to `MOV R3,#01h`
(`7b 01`), so each mask update always takes the *set* path and both registers
end up `0x11`.

| file offset | original | patched | meaning |
|---:|---:|---:|---|
| `0x1429e` (bank1 `c26e`) | `00` | `01` | value operand for bit 0 of `0xfc8e` |
| `0x142b4` (bank1 `c284`) | `00` | `01` | value operand for bit 4 of `0xfc8e` |
| `0x142ca` (bank1 `c29a`) | `00` | `01` | value operand for bit 0 of `0xfc8f` |
| `0x1426a` (bank1 `c23a`) | `00` | `01` | value operand for bit 4 of `0xfc8f` |
| `0x18033` | `7c` | `80` | code checksum `0x797c` → `0x7980` |

## Verify the register fix

The header checksum is unchanged and the code checksum is recomputed by the
patcher:

```console
$ ./ms2130s_patch            # prints the checksums before/after
```

Disassembling the patched bytes shows every immediate now loads `0x01`:

```console
c234: 7b01  MOV R3, #01h
c236: 300502 JNB bit05, c23bh
c239: 7b01  MOV R3, #01h      <- was #00h
c23b: 7d10  MOV R5, #10h
c23d: 7f8f  MOV R7, #8fh
c23f: 7efc  MOV R6, #fch
c241: 0287c7 LJMP 87c7h
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
`0xfc8f = 0x11` additionally disables the companion chroma processing,
matching the runtime writes in `ms2130s_reg`/`hsdaoh`.

With the EDID image additionally flashed, a compliant source is now allowed to
send full-range RGB (and normally will), which matches what the chip expects —
so black arrives as 0 and white as 255 without any host-side workaround.

## License

MIT. See the SPDX header in [`ms2130s_patch.c`](ms2130s_patch.c).

## Acknowledgements

* [steve-m/ms2130_patcher](https://github.com/steve-m/ms2130_patcher) and
  [steve-m/ms213x_flash](https://github.com/steve-m/ms213x_flash) for the
  patching idea and the flashing tool.
* The [hsdaoh](https://github.com/steve-m/hsdaoh) project for documenting the
  transparent-mode registers (`0xfc8e`, `0xfc8f`, …).
