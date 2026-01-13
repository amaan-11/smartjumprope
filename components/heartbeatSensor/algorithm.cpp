/** \file algorithm.cpp ******************************************************
 *
 * Project: MAXREFDES117#
 * Filename: algorithm.cpp
 * Description: This module calculates the heart rate/SpO2 level
 *
 * --------------------------------------------------------------------
 *
 * This code follows the following naming conventions:
 *
 * char              ch_pmod_value
 * char (array)      s_pmod_s_string[16]
 * float             f_pmod_value
 * int32_t           n_pmod_value
 * int32_t (array)   an_pmod_value[16]
 * int16_t           w_pmod_value
 * int16_t (array)   aw_pmod_value[16]
 * uint16_t          uw_pmod_value
 * uint16_t (array)  auw_pmod_value[16]
 * uint8_t           uch_pmod_value
 * uint8_t (array)   auch_pmod_buffer[16]
 * uint32_t          un_pmod_value
 * int32_t *         pn_pmod_value
 *
 * ------------------------------------------------------------------------- */

#include "algorithm.h"
#include <limits.h> // for INT32_MIN
#include <stddef.h>
#include <stdint.h>

#define FS 25 // sample rate for heart rate calculation

void maxim_heart_rate_and_oxygen_saturation(
    uint32_t *pun_ir_buffer, int32_t n_ir_buffer_length,
    uint32_t *pun_red_buffer, float *pn_spo2, int8_t *pch_spo2_valid,
    int32_t *pn_heart_rate, int8_t *pch_hr_valid) {
  uint32_t un_ir_mean;
  int32_t k, n_i_ratio_count;
  int32_t i, n_exact_ir_valley_locs_count, n_middle_idx;
  int32_t n_th1, n_npks;
  int32_t an_ir_valley_locs[15];
  int32_t n_peak_interval_sum;

  // unused variable
  (void)FS;

  int32_t n_y_ac, n_x_ac;
  int32_t n_y_dc_max, n_x_dc_max;
  int32_t n_y_dc_max_idx = 0,
          n_x_dc_max_idx = 0; // initialized to avoid compiler warnings
  int32_t an_ratio[5], n_ratio_average;
  int32_t n_nume, n_denom;
  int32_t an_x[BUFFER_SIZE]; // IR
  int32_t an_y[BUFFER_SIZE]; // RED

  // calculate DC mean and subtract
  un_ir_mean = 0;
  for (k = 0; k < n_ir_buffer_length; k++)
    un_ir_mean += pun_ir_buffer[k];
  un_ir_mean /= n_ir_buffer_length;

  // remove DC and invert signal for peak detection
  for (k = 0; k < n_ir_buffer_length; k++)
    an_x[k] = un_ir_mean - pun_ir_buffer[k];

  // 4-point Moving Average
  for (k = 0; k < BUFFER_SIZE_MA4; k++)
    an_x[k] = (an_x[k] + an_x[k + 1] + an_x[k + 2] + an_x[k + 3]) / 4;

  // calculate threshold
  n_th1 = 0;
  for (k = 0; k < BUFFER_SIZE_MA4; k++)
    n_th1 += an_x[k];
  n_th1 /= BUFFER_SIZE_MA4;
  if (n_th1 < 30)
    n_th1 = 30;
  if (n_th1 > 60)
    n_th1 = 60;

  for (k = 0; k < 15; k++)
    an_ir_valley_locs[k] = 0;

  maxim_find_peaks(an_ir_valley_locs, &n_npks, an_x, BUFFER_SIZE_MA4, n_th1, 4,
                   15);

  n_peak_interval_sum = 0;
  if (n_npks >= 2) {
    for (k = 1; k < n_npks; k++)
      n_peak_interval_sum += (an_ir_valley_locs[k] - an_ir_valley_locs[k - 1]);
    n_peak_interval_sum /= (n_npks - 1);
    *pn_heart_rate = (int32_t)((FS * 60) / n_peak_interval_sum);
    *pch_hr_valid = 1;
  } else {
    *pn_heart_rate = -999;
    *pch_hr_valid = 0;
  }

  // load raw values for SPO2 calculation
  for (k = 0; k < n_ir_buffer_length; k++) {
    an_x[k] = pun_ir_buffer[k];
    an_y[k] = pun_red_buffer[k];
  }

  n_exact_ir_valley_locs_count = n_npks;
  n_ratio_average = 0;
  n_i_ratio_count = 0;
  for (k = 0; k < 5; k++)
    an_ratio[k] = 0;

  // check for out-of-range valleys
  for (k = 0; k < n_exact_ir_valley_locs_count; k++) {
    if (an_ir_valley_locs[k] > BUFFER_SIZE) {
      *pn_spo2 = -999;
      *pch_spo2_valid = 0;
      return;
    }
  }

  // find max between valleys and compute AC/DC ratios
  for (k = 0; k < n_exact_ir_valley_locs_count - 1; k++) {
    n_y_dc_max = INT32_MIN;
    n_x_dc_max = INT32_MIN;

    if (an_ir_valley_locs[k + 1] - an_ir_valley_locs[k] > 3) {
      for (i = an_ir_valley_locs[k]; i < an_ir_valley_locs[k + 1]; i++) {
        if (an_x[i] > n_x_dc_max) {
          n_x_dc_max = an_x[i];
          n_x_dc_max_idx = i;
        }
        if (an_y[i] > n_y_dc_max) {
          n_y_dc_max = an_y[i];
          n_y_dc_max_idx = i;
        }
      }

      n_y_ac = (an_y[an_ir_valley_locs[k + 1]] - an_y[an_ir_valley_locs[k]]) *
               (n_y_dc_max_idx - an_ir_valley_locs[k]);
      n_y_ac = an_y[an_ir_valley_locs[k]] +
               n_y_ac / (an_ir_valley_locs[k + 1] - an_ir_valley_locs[k]);
      n_y_ac = an_y[n_y_dc_max_idx] - n_y_ac;

      n_x_ac = (an_x[an_ir_valley_locs[k + 1]] - an_x[an_ir_valley_locs[k]]) *
               (n_x_dc_max_idx - an_ir_valley_locs[k]);
      n_x_ac = an_x[an_ir_valley_locs[k]] +
               n_x_ac / (an_ir_valley_locs[k + 1] - an_ir_valley_locs[k]);
      n_x_ac = an_x[n_x_dc_max_idx] - n_x_ac;

      n_nume = (n_y_ac * n_x_dc_max) >> 7;
      n_denom = (n_x_ac * n_y_dc_max) >> 7;

      if (n_denom > 0 && n_i_ratio_count < 5 && n_nume != 0) {
        an_ratio[n_i_ratio_count] = (n_nume * 100) / n_denom;
        n_i_ratio_count++;
      }
    }
  }

  maxim_sort_ascend(an_ratio, n_i_ratio_count);
  n_middle_idx = n_i_ratio_count / 2;

  if (n_middle_idx > 1)
    n_ratio_average = (an_ratio[n_middle_idx - 1] + an_ratio[n_middle_idx]) / 2;
  else
    n_ratio_average = an_ratio[n_middle_idx];

  if (n_ratio_average > 2 && n_ratio_average < 184) {
    *pn_spo2 = static_cast<float>(uch_spo2_table[n_ratio_average]);
    *pch_spo2_valid = 1;
  } else {
    *pn_spo2 = -999;
    *pch_spo2_valid = 0;
  }
}

// --- Keep all helper functions unchanged ---
