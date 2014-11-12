/* Copyright 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Rockchip VPU (video processing unit) encoder library module. */

#ifndef LIBVPU_RK_VEPU_INTERFACE_H_
#define LIBVPU_RK_VEPU_INTERFACE_H_

#include <stdint.h>

struct rk_vepu_param {
  uint32_t width; /* video width */
  uint32_t height; /* video height */
  int32_t framerate_numer; /* frame rate */
  int32_t framerate_denom;
  int32_t bitrate; /* bitrate per second */
  uint32_t input_format; /* V4L2 fourcc pixel format */
};

/**
 * Get and initialize an encoder instance with encode parameters.
 *
 * @param param: vpu encoder parameters, see struct rk_vepu_param.
 *
 * @return the encoder instance, will use in other interface.
 */
void *rk_vepu_init(struct rk_vepu_param *param);

/**
 * Deinitialize and destroy the encoder instance.
 *
 * @param enc: the instance generated by rk_vepu_init.
 *
 * @return -1 failure, 0 success.
 */
int rk_vepu_deinit(void *enc);

/**
 * Get configuration for driver to configure the hardware.
 *
 * @param enc: the instance generated by rk_vepu_init.
 * @param config: the encoder configuration.
 * @param size: size of configuration.
 *
 * @return -1 failure, 0 success.
 */
int rk_vepu_get_config(void *enc, void **config, uint32_t *size);

/**
 * Update the encoder configuration by previous encoding output.
 *
 * @param enc: the instance generated by rk_vepu_init.
 * @param config: the configuration got from driver.
 * @param size: size of the configuration.
 *
 * @return -1 failure, 0 success.
 */
int rk_vepu_update_config(void *enc, void *config, uint32_t size);

#endif  // LIBVPU_RK_VEPU_INTERFACE_H_

