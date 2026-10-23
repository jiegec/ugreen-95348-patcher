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
./ms2130s_patch                    # writes two images next to the input
```

| output | contents |
|---|---|
| `…_0xfc8e_0xfc8f_0x11.bin` | register fix only (proc-amp bypassed) |
| `…_0xfc8e_0xfc8f_0x11_edid_srgb_rgb444_4k30.bin` | register fix **+ EDID**: sRGB, RGB 4:4:4 only, 4K30 preferred, VCDB `QS=1` |

Flash whichever you need (see [Flashing](#flashing)).

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
| `UGREEN_undated_backup_24dc_797c_0xfc8e_0xfc8f_0x11.bin` | Pre-built patched firmware (registers only), byte-for-byte identical to the output of the tool. |
| `UGREEN_undated_backup_24dc_797c_0xfc8e_0xfc8f_0x11_edid_srgb_rgb444_4k30.bin` | Pre-built patched firmware with the EDID changes (sRGB, RGB 4:4:4 only, 4K30 preferred, `QS=1`). |

## Build

Requirements: a C compiler and `make` (tested with `gcc` on Linux and macOS).

```console
make
```

## Usage

```console
./ms2130s_patch [input.bin] [output.bin] [output_edid.bin]
```

All arguments are optional:

* input:  `./UGREEN_undated_backup_24dc_797c.bin`
* output: `./UGREEN_undated_backup_24dc_797c_0xfc8e_0xfc8f_0x11.bin`
* output with EDID fix: `./UGREEN_undated_backup_24dc_797c_0xfc8e_0xfc8f_0x11_edid_srgb_rgb444_4k30.bin`

If the input's header checksum is not `0x24dc` or its code checksum is not
`0x797c`, the tool prints an error and refuses to patch: it is hard-wired to
this one firmware revision.  The EDID is likewise only touched when it matches
the stock bytes exactly.

```console
$ ./ms2130s_patch
Length of file: 98356
Original header checksum matches: 24dc
Original code checksum matches: 797c
New code checksum: 7980
Wrote patched firmware: ./UGREEN_undated_backup_24dc_797c_0xfc8e_0xfc8f_0x11.bin
  0xfc8e bit 0 = 0x01 (processing disabled)
  0xfc8e bit 4 = 0x01 (processing disabled)
  0xfc8f bit 0 = 0x01 (processing disabled)
  0xfc8f bit 4 = 0x01 (processing disabled)

EDID patched: feature byte 0x18 = 0x0e (RGB colour + sRGB), VCDB E2 00 4f (QS=1)
  preferred timing: 3840x2160@30, CTA flags: 42 (no YCbCr 4:4:4/4:2:2, no 4:2:0)
  base checksum: 2e, CTA checksum: d9, CTA DTD offset: 2a
New code checksum: 7980
Wrote patched firmware with EDID fix: ./UGREEN_undated_backup_24dc_797c_0xfc8e_0xfc8f_0x11_edid_srgb_rgb444_4k30.bin
```

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

## The EDID fix (second image)

Why it is needed: this chip treats **both** its RGB and its YCbCr inputs as
full range (0–255).  A source that follows the CTA default for a CE format
sends **limited** range instead, and the result is wrong:

| source sends | chip assumes | result |
|---|---|---|
| RGB full | full | correct |
| RGB limited | full | double-compressed (16 → ~30, 235 → ~218) |
| YCbCr limited | full | black 16 / white 235, washed out (grey 16 captures as 30) |
| YCbCr full | full | correct |

On top of that the stock EDID makes sources pick YCbCr 4:4:4 (macOS does it
for 4K30 even though the TMDS clock is identical to RGB) and it lists 4K only
as CEA VICs, with 1080p as the preferred timing — which is why macOS offers a
bare resolution list instead of its HiDPI "Larger Text / More Space" picker.

The EDID fix therefore:

* declares **sRGB** and an **RGB colour** display,
* makes **3840×2160@30 the preferred timing**, so the source treats the sink
  as a native-4K panel (macOS then generates HiDPI modes and defaults to 4K),
* removes **every YCbCr colour mode** (no YCbCr 4:4:4/4:2:2 flags, no YCbCr
  4:2:0 data block), leaving RGB 4:4:4 as the only option,
* adds a **VCDB** that permits full-range RGB.

| EDID offset | original | patched | why |
|---:|---:|---:|---|
| `0x18` | `0x02` | **`0x0e`** | bits 4:3 = `01` RGB colour display (stock `00` says **monochrome**), bit 2 = **sRGB**, bit 1 = preferred timing (kept) |
| `0x36`–`0x47` (DTD1) | 1920×1080@60 | **3840×2160@30**, 297.00 MHz | preferred timing → native 4K panel |
| `0x48`–`0x59` (DTD2) | 1280×720@144 | **1920×1080@60** | the old preferred timing is kept as the second DTD; 720p144 is dropped |
| `0x83` (CTA flags) | `0x72` | **`0x42`** | clear bits 5:4 = YCbCr 4:4:4 / 4:2:2 support; basic audio is kept |
| CTA data blocks | Video, Audio, Speaker, VSDB, **YCbCr 4:2:0** | same minus the 4:2:0 block, plus **VCDB `E2 00 4f`** | no YCC mode left at all |
| `0x82` (CTA DTD offset) | `0x2d` | **`0x2a`** | six bytes removed (4:2:0 block), three added (VCDB) |
| `0x7f` (base checksum) | `0xc0` | **`0x2e`** | recomputed |
| `0xff` (CTA checksum) | `0x58` | **`0xd9`** | recomputed |
| firmware code checksum | `0x797c` | **`0x7980`** | recomputed (together with the register fix) |

The 3840×2160@30 DTD is the CEA VIC 95 timing (4400×2250 total, 297.00 MHz,
hfp 176 / hsw 88, vfp 8 / vsw 10, digital separate sync, `+/+`).  Its image
size / border / flag bytes are copied from the stock DTD.

Reused unchanged: chromaticity, gamma, established and standard timings, the
Video Data Block VIC list, audio, speaker allocation, HDMI VSDB (physical
address, 300 MHz max TMDS, HDMI VICs 1–3) and the three 2560×1440 CTA DTDs.

Consequences:

* Only RGB 4:4:4 is available, so a source cannot fall back to YCbCr.
* Preferred timing is 4K30, so sources default to 3840×2160@30 (297 MHz, within
  the declared 300 MHz TMDS limit).
* 4K50/60 is **gone**: those need YCbCr 4:2:0 (594 MHz is impossible in 4:4:4
  at 8 bit), and the 4:2:0 block was removed on purpose.
* 1080p60 is still available, as the second DTD.

The VCDB payload byte is CTA-861-G Table 73:

```
bit7 QY  bit6 QS  bits5:4 S_PT  bits3:2 S_IT  bits1:0 S_CE
QY=1 would let a source pick the YCC range via AVI YQ (left 0 here)
QS=1 lets a source pick the RGB range via AVI Q
```

The payload is `0x4f`: **`QY=0`**, **`QS=1`**, `S_PT=00` (No Data),
`S_IT=11`, `S_CE=11` ("supports both over- and underscan", the common `0x0f`
pattern).  `QY=0` because no YCbCr mode is advertised any more, so a YCC
range permission could never be used.  The low nibble must not be left at 0 —
`S_IT=00`/`S_CE=00` would declare "IT/CE Video Formats not supported".

Note that `QS=1` only makes a non-default `Q` *legal*; the source still picks
the range.  With it a GPU's "Auto"/"Default" setting normally resolves to
full, and an explicit "Full" setting becomes legal and is signalled in the AVI
InfoFrame (which this chip ignores anyway).

### Verify

After flashing, display a black and a white patch.  Black **0** and white
**255** means the source now sends full range; black 16 / white 235 means it
still sends limited, and the range still has to be set on the source side.
`ms2130s_reg --hid timing` should report RGB 4:4:4 (`0x3c47=0`, `f130=0x10`).

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
