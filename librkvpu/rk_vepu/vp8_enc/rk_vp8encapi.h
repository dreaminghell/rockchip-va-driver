/* Copyright 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef _RK_VP8ENCAPI_H_
#define _RK_VP8ENCAPI_H_
#include "rk_venc.h"
#include "vp8encapi.h"
#include "rk_vepu_interface.h"

enum VP8E_PRIVATE_DATA_TYPE {
	VP8E_PRIVATE_DATA_TYPE_END,
	VP8E_PRIVATE_DATA_TYPE_FRAMETAG,
	VP8E_PRIVATE_DATA_TYPE_FRAMEHEADER,
	VP8E_PRIVATE_DATA_TYPE_CABAC,
	VP8E_PRIVATE_DATA_TYPE_SEGMAP,
	VP8E_PRIVATE_DATA_TYPE_REG,
	VP8E_PRIVATE_DATA_TYPE_PROBCNT
};

#define NUM_CTRLS 3
struct rk_vp8_encoder {
	struct rk_venc *parent;

	EncoderParameters cmdl;
	VP8EncInst encoder;
	VP8EncIn encIn;
	int intraPeriodCnt;
	int codedFrameCnt;
	int src_img_size;
	int next;
	uint8_t *priv_data;
	uint32_t priv_offset;	/* offset of current private data */
	uint32_t hdr_idx;
	bool first_frame;
	uint32_t rk_ctrl_ids[NUM_CTRLS];
	void *rk_payloads[NUM_CTRLS];
	uint32_t rk_payload_sizes[NUM_CTRLS];
};

struct rk_venc *rk_vp8_encoder_alloc_ctx(void);
void rk_vp8_encoder_free_ctx(struct rk_venc *enc);
#endif				/* _RK_VP8ENCAPI_H_ */
