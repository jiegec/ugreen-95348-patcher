/*
 * MS2130S (UGREEN 95348) firmware patcher
 *
 * Forces the luma-processing register 0xfc8e to 0x11 (processing disabled)
 * instead of the firmware default 0x00.  This bakes in the fix that hsdaoh
 * and ms2130s_reg apply at runtime:
 *
 *     write_reg(0xfc8e, 0x11);
 *
 * and removes the full-range -> limited mid-tone lift (source 200 came out
 * as raw 204, decoded to 219).
 *
 * How the firmware writes 0x00
 * ----------------------------
 * XDATA 0xfc8e has two relevant bits: bit 0 (mask 0x01) and bit 4 (0x10).
 * The stream-reinit routine FUN_CODE_c220() clears both via the bit-mask
 * helper FUN_CODE_87c7(), which sets the masked bits when its value argument
 * (R3) is non-zero and clears them when it is zero:
 *
 *   bank1 c268:  MOV R3,#01h ; JNB bit05,c26f ; MOV R3,#00h
 *                MOV R5,#01h ; MOV R7,#8eh ; MOV R6,#fch ; LJMP 87c7h
 *                -> clear bit 0 of 0xfc8e
 *
 *   bank1 c27e:  MOV R3,#01h ; JNB bit05,c285 ; MOV R3,#00h
 *                MOV R5,#10h ; MOV R7,#8eh ; MOV R6,#fch ; LJMP 87c7h
 *                -> clear bit 4 of 0xfc8e
 *
 * After FUN_CODE_c220() the register is therefore 0x00.  (The companion
 * FUN_CODE_c294() does the same for the chroma register 0xfc8f.)
 *
 * The patch
 * ---------
 * The two "MOV R3,#00h" (7b 00) instructions are changed to
 * "MOV R3,#01h" (7b 01), so both bit 0 and bit 4 of 0xfc8e are *set*
 * regardless of the bit05 state, i.e. the register always ends up 0x11.
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

/* Absolute file offsets of the immediate operand of the two
 * "MOV R3,#00h" instructions in physical bank 1 (file 0x10000..0x18000):
 *   bank1:c26e (clears bit 0 of 0xfc8e)
 *   bank1:c284 (clears bit 4 of 0xfc8e)
 */
#define PATCH_BIT0	0x1429e
#define PATCH_BIT4	0x142b4

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

int main(int argc, char *argv[])
{
	const char *in_name  = (argc > 1) ? argv[1] : "./UGREEN_undated_backup_24dc_797c.bin";
	const char *out_name = (argc > 2) ? argv[2] : "./UGREEN_undated_backup_24dc_797c_0xfc8e_0x11.bin";
	uint8_t *fw = NULL;
	FILE *fp = NULL;
	uint16_t calc_header_csum, orig_header_csum;
	uint16_t calc_code_csum, orig_code_csum;
	uint8_t *code;
	int ret = 1;

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

	if (fw[PATCH_BIT0 - 1] != 0x7b || fw[PATCH_BIT0] != 0x00 ||
	    fw[PATCH_BIT4 - 1] != 0x7b || fw[PATCH_BIT4] != 0x00) {
		fprintf(stderr, "Patch sites do not contain the expected "
				"'MOV R3,#00h' instruction, aborting\n");
		goto out;
	}

	/* force the value argument (R3) of both 0xfc8e mask updates to 1
	 * so the set path is taken and the register becomes 0x11 */
	fw[PATCH_BIT0] = 0x01;
	fw[PATCH_BIT4] = 0x01;

	/* replace the code checksum */
	calc_code_csum = calculate_code_checksum(code);
	fw[CODE_OFFSET + CODE_LEN + 2] = calc_code_csum >> 8;
	fw[CODE_OFFSET + CODE_LEN + 3] = calc_code_csum & 0xff;
	printf("New code checksum: %04x\n", calc_code_csum);

	fp = fopen(out_name, "wb");
	if (!fp) {
		fprintf(stderr, "Error opening %s for writing\n", out_name);
		goto out;
	}
	if (fwrite(fw, 1, file_len, fp) != (size_t)file_len) {
		fprintf(stderr, "Error writing firmware file!\n");
		goto out;
	}
	printf("Wrote patched firmware: %s\n", out_name);
	printf("  0xfc8e = 0x11 (luma processing disabled)\n");
	ret = 0;

out:
	if (fp)
		fclose(fp);
	if (fw)
		free(fw);

	return ret;
}
