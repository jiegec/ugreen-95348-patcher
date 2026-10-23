/*
 * MS2130S (UGREEN 95348) firmware patcher
 *
 * Forces the luma-processing register 0xfc8e and the chroma-processing
 * register 0xfc8f to 0x11 (processing disabled) instead of the firmware
 * default 0x00.  This bakes in the fix that hsdaoh and ms2130s_reg apply at
 * runtime:
 *
 *     write_reg(0xfc8e, 0x11);
 *     write_reg(0xfc8f, 0x11);
 *
 * and removes the full-range -> limited mid-tone lift (source 200 came out
 * as raw 204, decoded to 219) documented in
 * analysis/TRANSPARENT_MODE_HANDOFF.md.
 *
 * How the firmware writes 0x00
 * ----------------------------
 * XDATA 0xfc8e and 0xfc8f each have two relevant bits: bit 0 (mask 0x01) and
 * bit 4 (0x10).  The stream-reinit routine FUN_CODE_c220() clears all four
 * via the bit-mask helper FUN_CODE_87c7(), which sets the masked bits when
 * its value argument (R3) is non-zero and clears them when it is zero:
 *
 *   bank1 c268:  MOV R3,#01h ; JNB bit05,c26f ; MOV R3,#00h
 *                MOV R5,#01h ; MOV R7,#8eh ; MOV R6,#fch ; LJMP 87c7h
 *                -> clear bit 0 of 0xfc8e
 *
 *   bank1 c27e:  MOV R3,#01h ; JNB bit05,c285 ; MOV R3,#00h
 *                MOV R5,#10h ; MOV R7,#8eh ; MOV R6,#fch ; LJMP 87c7h
 *                -> clear bit 4 of 0xfc8e
 *
 *   bank1 c294:  MOV R3,#01h ; JNB bit05,c29b ; MOV R3,#00h
 *                MOV R5,#01h ; MOV R7,#8fh ; MOV R6,#fch ; LJMP 87c7h
 *                -> clear bit 0 of 0xfc8f
 *
 *   bank1 c234:  MOV R3,#01h ; JNB bit05,c23b ; MOV R3,#00h
 *                MOV R5,#10h ; MOV R7,#8fh ; MOV R6,#fch ; LJMP 87c7h
 *                -> clear bit 4 of 0xfc8f
 *
 * After FUN_CODE_c220() both registers are therefore 0x00.
 *
 * The patch
 * ---------
 * The four "MOV R3,#00h" (7b 00) instructions are changed to
 * "MOV R3,#01h" (7b 01), so both bits of both registers are *set*
 * regardless of the bit05 state, i.e. 0xfc8e and 0xfc8f always end up 0x11.
 *
 * The optional EDID patch
 * -----------------------
 * Two output images are written.  The second one additionally patches the
 * 256-byte EDID at file offset 0x307 and only touches what is needed to
 * declare sRGB and to permit full-range RGB:
 *
 *   base block 0x18 : 0x02 -> 0x0e
 *        bits 4:3 = 01 (RGB colour display; stock 00 = monochrome!)
 *        bit 2    = 1  (sRGB standard default colour space)
 *        bit 1    = 1  (preferred timing, already set)
 *
 *   CTA extension   : append the Video Capability Data Block
 *                      E2 00 c0   (QY=1, QS=1)
 *        QS=1 lets a source send non-default RGB quantization (AVI Q=2,
 *        full range) and QY=1 the same for YCC (AVI YQ=1).  This chip
 *        treats *both* RGB and YCbCr as full-range, so both permissions
 *        are what it wants.
 *
 *   everything else (chromaticity, gamma, DTDs, audio, VICs) is reused,
 *   only the CTA DTD offset moves by 3 and the two EDID checksums and the
 *   firmware code checksum are recomputed.
 *
 * Copyright (C) 2026
 * SPDX-License-Identifier: MIT
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#define CODE_OFFSET	0x30
#define CODE_LEN	0x18000

/* The patcher is hard-wired to this one firmware revision. */
#define FIRMWARE_CODE_CHECKSUM	0x797c
#define FIRMWARE_HEADER_CHECKSUM 0x24dc

/* Absolute file offsets of the immediate operand of the four
 * "MOV R3,#00h" instructions in physical bank 1 (file 0x10000..0x18000).
 */
struct patch_site {
	const char *desc;
	uint32_t offset;
};

static const struct patch_site patch_sites[] = {
	{ "0xfc8e bit 0", 0x1429e },	/* bank1 c26e */
	{ "0xfc8e bit 4", 0x142b4 },	/* bank1 c284 */
	{ "0xfc8f bit 0", 0x142ca },	/* bank1 c29a */
	{ "0xfc8f bit 4", 0x1426a },	/* bank1 c23a */
};

#define NUM_PATCH_SITES	(sizeof(patch_sites) / sizeof(patch_sites[0]))

/* EDID location inside the image: code offset 0x2d7 + header 0x30. */
#define EDID_OFFSET	0x307

/* CTA-861 Video Capability Data Block payload byte (Table 73):
 *   bit7 QY, bit6 QS, bits5:4 S_PT, bits3:2 S_IT, bits1:0 S_CE
 * QS=1 -> RGB quantization range selectable via AVI Q
 * QY=1 -> YCC quantization range selectable via AVI YQ
 * The chip expects full range on both inputs, so both are declared.
 */
#define VCDB_PAYLOAD	0xc0

/* Stock EDID fingerprint, used to refuse foreign images. */
#define EDID_STOCK_FEATURE	0x02
#define EDID_STOCK_BASE_CSUM	0xc0
#define EDID_STOCK_DTD_OFFSET	0x2d
#define EDID_STOCK_CTA_CSUM	0x58

static uint16_t calculate_header_checksum(const uint8_t *data)
{
	uint32_t csum = 0;
	int i;

	for (i = 0x02; i < CODE_OFFSET; i++) {
		if ((i < 0x0c) || (i > 0x0f))
			csum += data[i];
	}

	return csum & 0xffff;
}

static uint16_t calculate_code_checksum(const uint8_t *code)
{
	uint32_t csum = 0;
	int i;

	for (i = 0; i < CODE_LEN; i++)
		csum += code[i];

	return csum & 0xffff;
}

static void store_code_checksum(uint8_t *fw, uint16_t csum)
{
	fw[CODE_OFFSET + CODE_LEN + 2] = csum >> 8;
	fw[CODE_OFFSET + CODE_LEN + 3] = csum & 0xff;
}

static void fix_edid_base_checksum(uint8_t *edid)
{
	uint32_t sum = 0;
	int i;

	edid[0x7f] = 0;
	for (i = 0; i < 0x7f; i++)
		sum += edid[i];
	edid[0x7f] = (uint8_t)((-sum) & 0xff);
}

/* Append a VCDB to the CTA-861 data block collection, shifting the DTD area
 * and its offset.  Trailing padding is dropped first; a real DTD is never
 * truncated.
 */
static int edid_add_vcdb(uint8_t *edid, uint8_t payload)
{
	uint8_t *ext = edid + 0x80;
	uint8_t new_ext[0x80];
	size_t dtd_off, blocks_len, dtd_len, new_off, i;
	uint32_t sum;

	if (ext[0] != 0x02) {
		fprintf(stderr, "Unexpected CTA revision %02x, EDID patch skipped\n",
			ext[0]);
		return -1;
	}

	dtd_off = ext[2];
	if (dtd_off < 4 || dtd_off > 0x6f) {
		fprintf(stderr, "Unexpected CTA DTD offset %02x, EDID patch skipped\n",
			ext[2]);
		return -1;
	}

	blocks_len = dtd_off - 4;
	dtd_len = 0x7f - dtd_off;
	new_off = dtd_off + 3;

	while (dtd_len > (0x7f - new_off)) {
		if (ext[dtd_off + dtd_len - 1] != 0x00) {
			fprintf(stderr, "No room in the CTA extension for the VCDB\n");
			return -1;
		}
		dtd_len--;
	}

	memset(new_ext, 0, sizeof(new_ext));
	memcpy(new_ext, ext, 4);
	new_ext[2] = (uint8_t)new_off;
	memcpy(new_ext + 4, ext + 4, blocks_len);
	new_ext[4 + blocks_len + 0] = 0xe2;	/* tag 7 (extended), len 2 */
	new_ext[4 + blocks_len + 1] = 0x00;	/* extended tag: VCDB */
	new_ext[4 + blocks_len + 2] = payload;
	memcpy(new_ext + new_off, ext + dtd_off, dtd_len);

	sum = 0;
	for (i = 0; i < 0x7f; i++)
		sum += new_ext[i];
	new_ext[0x7f] = (uint8_t)((-sum) & 0xff);

	memcpy(ext, new_ext, sizeof(new_ext));
	return 0;
}

static int edid_patch(uint8_t *fw)
{
	uint8_t *edid = fw + EDID_OFFSET;
	uint32_t sum;
	int i;

	if (edid[0x18] != EDID_STOCK_FEATURE ||
	    edid[0x7f] != EDID_STOCK_BASE_CSUM ||
	    edid[0x82] != EDID_STOCK_DTD_OFFSET ||
	    edid[0xff] != EDID_STOCK_CTA_CSUM ||
	    memcmp(edid, "\x00\xff\xff\xff\xff\xff\xff\x00", 8)) {
		fprintf(stderr, "EDID does not match the expected stock value, "
				"EDID patch not applied\n");
		return -1;
	}

	/* RGB colour display (bits 4:3 = 01), sRGB, preferred timing */
	edid[0x18] = (uint8_t)((edid[0x18] & 0x01) | 0x0e);

	if (edid_add_vcdb(edid, VCDB_PAYLOAD) < 0)
		return -1;

	fix_edid_base_checksum(edid);

	sum = 0;
	for (i = 0; i < 0x80; i++)
		sum += edid[i];
	if (sum & 0xff) {
		fprintf(stderr, "Internal error: base checksum wrong\n");
		return -1;
	}
	sum = 0;
	for (i = 0x80; i < 0x100; i++)
		sum += edid[i];
	if (sum & 0xff) {
		fprintf(stderr, "Internal error: CTA checksum wrong\n");
		return -1;
	}

	return 0;
}

static int write_image(const char *name, const uint8_t *fw, size_t len)
{
	FILE *fp = fopen(name, "wb");

	if (!fp) {
		fprintf(stderr, "Error opening %s for writing\n", name);
		return -1;
	}
	if (fwrite(fw, 1, len, fp) != len) {
		fprintf(stderr, "Error writing %s\n", name);
		fclose(fp);
		return -1;
	}
	fclose(fp);
	return 0;
}

int main(int argc, char *argv[])
{
	const char *names[3];
	int nnames = 0;
	const char *in_name, *out_name, *out_edid_name;
	uint8_t *fw = NULL;
	FILE *fp = NULL;
	uint16_t calc_header_csum, orig_header_csum;
	uint16_t calc_code_csum, orig_code_csum;
	uint8_t *code;
	size_t i;
	int ret = 1;

	for (i = 1; i < (size_t)argc; i++) {
		if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) {
			printf("usage: %s [input.bin] [output_regs.bin] "
			       "[output_regs_edid.bin]\n", argv[0]);
			return 0;
		}
		if (nnames < 3)
			names[nnames++] = argv[i];
	}

	in_name = (nnames > 0) ? names[0] : "./UGREEN_undated_backup_24dc_797c.bin";
	out_name = (nnames > 1) ? names[1] :
		"./UGREEN_undated_backup_24dc_797c_0xfc8e_0xfc8f_0x11.bin";
	out_edid_name = (nnames > 2) ? names[2] :
			"./UGREEN_undated_backup_24dc_797c_0xfc8e_0xfc8f_0x11_edid_srgb_qsqy.bin";

	fp = fopen(in_name, "rb");
	if (!fp) {
		fprintf(stderr, "Error opening %s\n", in_name);
		goto out;
	}

	if (fseek(fp, 0, SEEK_END) != 0) {
		fprintf(stderr, "Error seeking %s\n", in_name);
		goto out;
	}
	long file_len = ftell(fp);
	if (file_len < CODE_OFFSET + CODE_LEN + 4) {
		fprintf(stderr, "File too small (%ld bytes)\n", file_len);
		goto out;
	}
	rewind(fp);

	fw = malloc(file_len);
	if (!fw) {
		fprintf(stderr, "Out of memory\n");
		goto out;
	}
	if (fread(fw, 1, file_len, fp) != (size_t)file_len) {
		fprintf(stderr, "Error reading firmware file!\n");
		goto out;
	}
	fclose(fp);
	fp = NULL;

	printf("Length of file: %ld\n", file_len);

	calc_header_csum = calculate_header_checksum(fw);
	orig_header_csum = (fw[CODE_OFFSET + CODE_LEN + 0] << 8) |
			    fw[CODE_OFFSET + CODE_LEN + 1];
	if (calc_header_csum != orig_header_csum)
		printf("Original header checksum mismatch: %04x != %04x\n",
		       orig_header_csum, calc_header_csum);
	else
		printf("Original header checksum matches: %04x\n",
		       orig_header_csum);

	if (calc_header_csum != FIRMWARE_HEADER_CHECKSUM) {
		fprintf(stderr, "Unexpected header checksum, patch not applied!\n");
		goto out;
	}

	calc_code_csum = calculate_code_checksum(fw + CODE_OFFSET);
	orig_code_csum = (fw[CODE_OFFSET + CODE_LEN + 2] << 8) |
			  fw[CODE_OFFSET + CODE_LEN + 3];
	if (calc_code_csum != orig_code_csum)
		printf("Original code checksum mismatch: %04x != %04x\n",
		       orig_code_csum, calc_code_csum);
	else
		printf("Original code checksum matches: %04x\n",
		       orig_code_csum);

	if (FIRMWARE_CODE_CHECKSUM != calc_code_csum) {
		fprintf(stderr, "The code checksum does not match the firmware file "
				"this tool was written for, patch not applied!\n");
		goto out;
	}

	code = fw + CODE_OFFSET;

	for (i = 0; i < NUM_PATCH_SITES; i++) {
		uint32_t off = patch_sites[i].offset;

		if (fw[off - 1] != 0x7b || fw[off] != 0x00) {
			fprintf(stderr, "Patch site '%s' (0x%x) does not contain "
					"the expected 'MOV R3,#00h' instruction, "
					"aborting\n",
				patch_sites[i].desc, off);
			goto out;
		}
	}

	/* force the value argument (R3) of every mask update to 1 so the
	 * set path is taken and both registers become 0x11 */
	for (i = 0; i < NUM_PATCH_SITES; i++)
		fw[patch_sites[i].offset] = 0x01;

	/* replace the code checksum */
	calc_code_csum = calculate_code_checksum(code);
	store_code_checksum(fw, calc_code_csum);
	printf("New code checksum: %04x\n", calc_code_csum);

	if (write_image(out_name, fw, file_len) < 0)
		goto out;
	printf("Wrote patched firmware: %s\n", out_name);
	for (i = 0; i < NUM_PATCH_SITES; i++)
		printf("  %s = 0x01 (processing disabled)\n",
		       patch_sites[i].desc);

	/* optional second image: the same register fix plus the EDID changes */
	if (edid_patch(fw) == 0) {
		calc_code_csum = calculate_code_checksum(code);
		store_code_checksum(fw, calc_code_csum);
		printf("\nEDID patched: feature byte 0x18 = 0x%02x "
		       "(RGB colour + sRGB), VCDB E2 00 %02x (QY=1, QS=1)\n",
		       fw[EDID_OFFSET + 0x18], VCDB_PAYLOAD);
		printf("  base checksum: %02x, CTA checksum: %02x, "
		       "CTA DTD offset: %02x\n",
		       fw[EDID_OFFSET + 0x7f], fw[EDID_OFFSET + 0xff],
		       fw[EDID_OFFSET + 0x82]);
		printf("New code checksum: %04x\n", calc_code_csum);
		if (write_image(out_edid_name, fw, file_len) < 0)
			goto out;
		printf("Wrote patched firmware with EDID fix: %s\n",
		       out_edid_name);
	}

	ret = 0;

out:
	if (fp)
		fclose(fp);
	if (fw)
		free(fw);

	return ret;
}
