/*------------------------------------------------------------------------------
--                                                                            --
--       This software is confidential and proprietary and may be used        --
--        only as expressly authorized by a licensing agreement from          --
--                                                                            --
--                            Hantro Products Oy.                             --
--                                                                            --
--                   (C) COPYRIGHT 2006 HANTRO PRODUCTS OY                    --
--                            ALL RIGHTS RESERVED                             --
--                                                                            --
--                 The entire notice above must be reproduced                 --
--                  on all copies and should not be removed.                  --
--                                                                            --
--------------------------------------------------------------------------------
*/

#include "vp8entropy.h"

#include <memory.h>
#include "enccommon.h"
#include "vp8entropytable.h"
#include "vp8macroblocktools.h"

/* Approximate bit cost of bin at given probability prob */
#define COST_BOOL(prob, bin)\
      ((bin) ? vp8_prob_cost[255 - (prob)] : vp8_prob_cost[prob])

enum {
  MAX_BAND = 7,
  MAX_CTX  = 3,
};

static int32_t CostTree(tree const* tree, int32_t* prob);
static void UpdateEntropy(vp8Instance_s* inst);

void EncSwapEndianess(uint32_t* buf, uint32_t sizeBytes) {
#if (ENCH1_OUTPUT_SWAP_8 == 1)
  uint32_t i = 0;
  int32_t words = sizeBytes / 4;

  ASSERT((sizeBytes % 8) == 0);

  while (words > 0) {
    uint32_t val = buf[i];
    uint32_t tmp = 0;

    tmp |= (val & 0xFF) << 24;
    tmp |= (val & 0xFF00) << 8;
    tmp |= (val & 0xFF0000) >> 8;
    tmp |= (val & 0xFF000000) >> 24;

#if(ENCH1_OUTPUT_SWAP_32 == 1)    /* need this for 64-bit HW */
    {
      uint32_t val2 = buf[i + 1];
      uint32_t tmp2 = 0;

      tmp2 |= (val2 & 0xFF) << 24;
      tmp2 |= (val2 & 0xFF00) << 8;
      tmp2 |= (val2 & 0xFF0000) >> 8;
      tmp2 |= (val2 & 0xFF000000) >> 24;

      buf[i] = tmp2;
      words--;
      i++;
    }
#endif
    buf[i] = tmp;
    words--;
    i++;
  }
#endif

}

void InitEntropy(vp8Instance_s* inst) {
  entropy* entropy = inst->entropy;

  ASSERT(sizeof(defaultCoeffProb) == sizeof(entropy->coeffProb));
  ASSERT(sizeof(defaultCoeffProb) == sizeof(coeffUpdateProb));
  ASSERT(sizeof(defaultMvProb) == sizeof(mvUpdateProb));
  ASSERT(sizeof(defaultMvProb) == sizeof(entropy->mvProb));

  UpdateEntropy(inst);

  /* Default propability */
  entropy->lastProb = 255;        /* Stetson-Harrison method TODO */
  entropy->gfProb = 128;          /* Stetson-Harrison method TODO */
  memcpy(entropy->YmodeProb, YmodeProb, sizeof(YmodeProb));
  memcpy(entropy->UVmodeProb, UVmodeProb, sizeof(UVmodeProb));
}

void WriteEntropyTables(vp8Instance_s* inst) {
  entropy* entropy = inst->entropy;
  uint8_t* table = (uint8_t*)inst->asic.cabacCtx.vir_addr;
  int32_t i, j, k, l;

  ASSERT(table);

  /* Write probability tables to ASIC linear memory, reg + mem */
  memset(table, 0, 56);
  table[0] = entropy->skipFalseProb;
  table[1] = entropy->intraProb;
  table[2] = entropy->lastProb;
  table[3] = entropy->gfProb;
  table[4] = entropy->segmentProb[0];
  table[5] = entropy->segmentProb[1];
  table[6] = entropy->segmentProb[2];

  table[8] = entropy->YmodeProb[0];
  table[9] = entropy->YmodeProb[1];
  table[10] = entropy->YmodeProb[2];
  table[11] = entropy->YmodeProb[3];
  table[12] = entropy->UVmodeProb[0];
  table[13] = entropy->UVmodeProb[1];
  table[14] = entropy->UVmodeProb[2];

  /* MV probabilities x+y: short, sign, size 8-9 */
  table[16] = entropy->mvProb[1][0];
  table[17] = entropy->mvProb[0][0];
  table[18] = entropy->mvProb[1][1];
  table[19] = entropy->mvProb[0][1];
  table[20] = entropy->mvProb[1][17];
  table[21] = entropy->mvProb[1][18];
  table[22] = entropy->mvProb[0][17];
  table[23] = entropy->mvProb[0][18];

  /* MV X size */
  for (i = 0; i < 8; i++)
    table[24 + i] = entropy->mvProb[1][9 + i];

  /* MV Y size */
  for (i = 0; i < 8; i++)
    table[32 + i] = entropy->mvProb[0][9 + i];

  /* MV X short tree */
  for (i = 0; i < 7; i++)
    table[40 + i] = entropy->mvProb[1][2 + i];

  /* MV Y short tree */
  for (i = 0; i < 7; i++)
    table[48 + i] = entropy->mvProb[0][2 + i];

  /* Update the ASIC table when needed. */
  if (entropy->updateCoeffProbFlag) {
    table += 56;
    /* DCT coeff probabilities 0-2, two fields per line. */
    for (i = 0; i < 4; i++)
      for (j = 0; j < 8; j++)
        for (k = 0; k < 3; k++) {
          for (l = 0; l < 3; l++) {
            *table++ = entropy->coeffProb[i][j][k][l];
          }
          *table++ = 0;
        }

    /* ASIC second probability table in ext mem.
     * DCT coeff probabilities 4 5 6 7 8 9 10 3 on each line.
     * coeff 3 was moved from first table to second so it is last. */
    for (i = 0; i < 4; i++)
      for (j = 0; j < 8; j++)
        for (k = 0; k < 3; k++) {
          for (l = 4; l < 11; l++) {
            *table++ = entropy->coeffProb[i][j][k][l];
          }
          *table++ = entropy->coeffProb[i][j][k][3];
        }
  }

  table = (uint8_t*)inst->asic.cabacCtx.vir_addr;
  if (entropy->updateCoeffProbFlag)
    EncSwapEndianess((uint32_t*)table, 56 + 8 * 48 + 8 * 96);
  else
    EncSwapEndianess((uint32_t*)table, 56);
}

void InitTreePenaltyTables(vp8Instance_s* container) {
  mbs* mbs = &container->mbs;     /* Macroblock related stuff */
  int32_t tmp, i;

  /* Calculate bit cost for each 16x16 mode, uses p-frame probs */
  for (i = DC_PRED; i <= B_PRED; i++) {
    tmp = CostTree(YmodeTree[i], (int32_t*)YmodeProb);
    mbs->intra16ModeBitCost[i] = tmp;
  }

  /* Calculate bit cost for each 4x4 mode, uses p-frame probs */
  for (i = B_DC_PRED; i <= B_HU_PRED; i++) {
    tmp = CostTree(BmodeTree[i], (int32_t*)BmodeProb);
    mbs->intra4ModeBitCost[i] = tmp;
  }
}

int32_t CostTree(tree const* tree, int32_t* prob) {
  int32_t value = tree->value;
  int32_t number = tree->number;
  int32_t const* index = tree->index;
  int32_t bitCost = 0;

  while (number--) {
    bitCost += COST_BOOL(prob[*index++], (value >> number) & 1);
  }

  return bitCost;
}

int32_t CostMv(int32_t mvd, int32_t* mvProb) {
  int32_t i, tmp, bitCost = 0;

  /* Luma motion vectors are doubled, see 18.1 in "VP8 Data Format and
   * Decoding Guide". */
  ASSERT(!(mvd & 1));
  tmp = ABS(mvd >> 1);

  /* Short Tree */
  if (tmp < 8) {
    bitCost += COST_BOOL(mvProb[0], 0);
    bitCost += CostTree(&mvTree[tmp], mvProb + 2);
    if (!tmp) return bitCost;

    /* Sign */
    bitCost += COST_BOOL(mvProb[1], mvd < 0);
    return bitCost;
  }

  /* Long Tree */
  bitCost += COST_BOOL(mvProb[0], 1);

  /* Bits 0, 1, 2 */
  for (i = 0; i < 3; i++) {
    bitCost += COST_BOOL(mvProb[9 + i], (tmp >> i) & 1);
  }

  /* Bits 9, 8, 7, 6, 5, 4 */
  for (i = 9; i > 3; i--) {
    bitCost += COST_BOOL(mvProb[9 + i], (tmp >> i) & 1);
  }

  /* Bit 3: if ABS(mvd) < 8, it is coded with short tree, so if here
   * ABS(mvd) <= 15, bit 3 must be one (because here we code values
   * 8,...,15) and is not explicitly coded. */
  if (tmp > 15) {
    bitCost += COST_BOOL(mvProb[9 + 3], (tmp >> 3) & 1);
  }

  /* Sign */
  bitCost += COST_BOOL(mvProb[1], mvd < 0);

  return bitCost;
}

void CoeffProb(vp8buffer* buffer, int32_t curr[4][8][3][11],
               int32_t prev[4][8][3][11]) {
  int32_t i, j, k, l;
  int32_t prob, new, old;

  for (i = 0; i < 4; i++) {
    for (j = 0; j < 8; j++) {
      for (k = 0; k < 3; k++) {
        for (l = 0; l < 11; l++) {
          prob = coeffUpdateProb[i][j][k][l];
          old = prev[i][j][k][l];
          new = curr[i][j][k][l];

          if (new == old) {
            VP8PutBool(buffer, prob, 0);
            COMMENT("Coeff prob update");
          } else {
            VP8PutBool(buffer, prob, 1);
            COMMENT("Coeff prob update");
            VP8PutLit(buffer, new, 8);
            COMMENT("New prob");
          }
        }
      }
    }
  }
}

void MvProb(vp8buffer* buffer, int32_t curr[2][19], int32_t prev[2][19]) {
  int32_t i, j;
  int32_t prob, new, old;

  for (i = 0; i < 2; i++) {
    for (j = 0; j < 19; j++) {
      prob = mvUpdateProb[i][j];
      old = prev[i][j];
      new = curr[i][j];

      if (new == old) {
        VP8PutBool(buffer, prob, 0);
        COMMENT("MV prob update");
      } else {
        VP8PutBool(buffer, prob, 1);
        COMMENT("MV prob update");
        VP8PutLit(buffer, new >> 1, 7);
        COMMENT("New prob");
      }
    }
  }
}

/*------------------------------------------------------------------------------
    update
    determine if given probability is to be updated (savings larger than
    cost of update)
------------------------------------------------------------------------------*/
uint32_t update(uint32_t updP, uint32_t left, uint32_t right, uint32_t oldP,
                uint32_t newP, uint32_t fixed) {
  int32_t u, s;

  /* how much it costs to update a coeff */
  u = (int32_t)fixed + ((vp8_prob_cost[255 - updP] - vp8_prob_cost[updP]) >> 8);
  /* bit savings if updated */
  s = ((int32_t)left * /* zero branch count */
       /* diff cost for '0' bin */
       (vp8_prob_cost[oldP] - vp8_prob_cost[newP]) +
       (int32_t)right * /* one branch count */
       /* diff cost for '1' bin */
       (vp8_prob_cost[255 - oldP] - vp8_prob_cost[255 - newP])) >> 8;

  return (s > u);
}

/*------------------------------------------------------------------------------
    mvprob
    compute new mv probability
------------------------------------------------------------------------------*/
uint32_t mvprob(uint32_t left, uint32_t right, uint32_t oldP) {
  uint32_t p;

  if (left + right) {
    p = (left * 255) / (left + right);
    p &= -2;
    if (!p) p = 1;
  } else
    p = oldP;

  return p;
}

/*------------------------------------------------------------------------------
    UpdateEntropy
------------------------------------------------------------------------------*/
void UpdateEntropy(vp8Instance_s* inst) {
  entropy* entropy = inst->entropy;
  int32_t i, j, k, l, tmp, ii;
  uint16_t* pCnt = (uint16_t*)inst->asic.probCount.vir_addr;
  uint16_t* pTmp;
  uint32_t p, left, right, oldP, updP;
  uint32_t type;
  uint32_t branchCnt[2];
  const int32_t offset[] = {
    -1, -1, -1,  0,  1,  2, -1,  3,  4, -1,  5,  6, -1,  7,  8, -1,
    9, 10, -1, 11, 12, 13, 14, 15, -1, 16, 17, -1, 18, 19, -1, 20,
    21, -1, 22, 23, -1, 24, 25, -1, 26, 27, 28, 29, 30, -1, 31, 32,
    -1, 33, 34, -1, 35, 36, -1, 37, 38, -1, 39, 40, -1, 41, 42, 43,
    44, 45, -1, 46, 47, -1, 48, 49, -1, 50, 51, -1, 52, 53, -1, 54,
    55, -1, 56, 57, -1, -1, -1, 58, 59, 60, 61, 62, 63, 64, 65, 66,
    67, 68, 69, 70, 71, 72, 73, 74, 75, 76, 77, 78, 79, 80, 81, 82,
    83, 84, 85, 86, 87, 88, 89, 90, 91, 92, 93, 94, 95, 96, 97, 98,
    99, 100, 101, 102, 103, 104, 105, 106, 107, 108, 109, 110, 111, 112, 113, 114,
    115, 116, 117, 118, 119, 120, 121, 122, 123, 124, 125, 126, 127, 128, 129, 130,
    131, 132, 133, 134, 135, 136, 137, 138, -1, -1, -1, 139, 140, 141, 142, 143,
    144, 145, 146, 147, 148, 149, 150, 151, 152, 153, 154, 155, 156, 157, 158, 159,
    160, 161, 162, 163, 164, 165, 166, 167, 168, 169, 170, 171, 172, 173, 174, 175,
    176, 177, 178, 179, 180, 181, 182, 183, 184, 185, 186, 187, 188, 189, 190, 191,
    192, 193, 194, 195, 196, 197, 198, 199, 200, 201, 202, 203, 204, 205, 206, 207,
    208, 209, 210, 211, 212, 213, 214, 215, 216, 217, 218, 219 };

  /* Update the HW prob table only when needed. */
  entropy->updateCoeffProbFlag = 0;

  /* Use default propabilities as reference when needed. */
  if (!inst->sps.refreshEntropy || inst->picBuffer.cur_pic->i_frame) {
    /* Only do the copying when something has changed. */
    if (!entropy->defaultCoeffProbFlag) {
      memcpy(entropy->coeffProb, defaultCoeffProb,
             sizeof(defaultCoeffProb));
      entropy->updateCoeffProbFlag = 1;
    }
    memcpy(entropy->mvProb, defaultMvProb, sizeof(defaultMvProb));
    entropy->defaultCoeffProbFlag = 1;
  }

  /* store current probs */
  memcpy(entropy->oldCoeffProb, entropy->coeffProb, sizeof(entropy->coeffProb));
  if (inst->frameCnt == 0 || !inst->picBuffer.last_pic->i_frame)
    memcpy(entropy->oldMvProb, entropy->mvProb, sizeof(entropy->mvProb));

  /* init probs */
  entropy->skipFalseProb = defaultSkipFalseProb[inst->rateControl.qpHdr];

  /* Do not update on first frame, token/branch counters not valid yet. */
  if (inst->frameCnt == 0) return;

  /* Do not update probabilities for droppable frames. */
  if (!inst->picBuffer.cur_pic->ipf && !inst->picBuffer.cur_pic->grf &&
      !inst->picBuffer.cur_pic->arf) return;

  /* If previous frame was lost the prob counters are not valid. */
  if (inst->prevFrameLost) return;

#ifdef TRACE_PROBS
  /* Trace HW output prob counters into file */
  EncTraceProbs(pCnt, ASIC_VP8_PROB_COUNT_SIZE);
#endif

  /* All four block types */
  for (i = 0; i < 4; i++) {
    /* All but last (==7) bands */
    for (j = 0; j < MAX_BAND; j++)
      /* All three neighbour contexts */
      for (k = 0; k < MAX_CTX; k++) {
        /* last token of current (type,band,ctx) */
        tmp = i * MAX_BAND * MAX_CTX + j * MAX_CTX + k;
        tmp += 2 * 4 * MAX_BAND * MAX_CTX;
        ii = offset[tmp];

        right = ii >= 0 ? pCnt[ii] : 0;

        /* first two branch probabilities */
        for (l = 2; l--;) {
          oldP = entropy->coeffProb[i][j][k][l];
          updP = coeffUpdateProb[i][j][k][l];

          tmp -= 4 * MAX_BAND * MAX_CTX;
          ii = offset[tmp];
          left = ii >= 0 ? pCnt[ii] : 0;
          /* probability of 0 for current branch */
          if (left + right) {
            p = ((left * 256) + ((left + right) >> 1)) / (left + right);
            if (p > 255) p = 255;
          } else
            p = oldP;

          if (update(updP, left, right, oldP, p, 8)) {
            entropy->coeffProb[i][j][k][l] = p;
            entropy->updateCoeffProbFlag = 1;
          }
          right += left;
        }
      }
  }

  /* If updating coeffProbs the defaults are no longer in use. */
  if (entropy->updateCoeffProbFlag)
    entropy->defaultCoeffProbFlag = 0;

  /* skip prob */
  pTmp = pCnt + ASIC_VP8_PROB_COUNT_MODE_OFFSET;
  p = pTmp[0] * 256 / inst->mbPerFrame;
  entropy->skipFalseProb = CLIP3(256 - (int32_t)p, 0, 255);

  /* intra prob,, do not update if previous was I frame */
  if (!inst->picBuffer.last_pic->i_frame) {
    p = pTmp[1] * 255 / inst->mbPerFrame;
    entropy->intraProb = CLIP3(p, 0, 255);
  } else
    entropy->intraProb = 63; /* TODO default value */

  /* MV probs shouldn't be updated if previous or current frame is intra */
  if (inst->picBuffer.last_pic->i_frame || inst->picBuffer.cur_pic->i_frame)
    return;

  /* mv probs */
  pTmp = pCnt + ASIC_VP8_PROB_COUNT_MV_OFFSET;
  for (i = 0; i < 2; i++) {
    /* is short prob */
    left = *pTmp++; /* short */
    right = *pTmp++; /* long */

    p = mvprob(left, right, entropy->oldMvProb[i][0]);
    if (update(mvUpdateProb[i][0], left, right,
               entropy->oldMvProb[i][0], p, 6))
      entropy->mvProb[i][0] = p;

    /* sign prob */
    right += left; /* total mvs */
    left = *pTmp++; /* num positive */
    /* amount of negative vectors = total - positive - zero vectors */
    right -= left - pTmp[0];

    p = mvprob(left, right, entropy->oldMvProb[i][1]);
    if (update(mvUpdateProb[i][1], left, right,
               entropy->oldMvProb[i][1], p, 6))
      entropy->mvProb[i][1] = p;

    /* short mv probs, branches 2 and 3 (0/1 and 2/3) */
    for (j = 0; j < 2; j++) {
      left = *pTmp++;
      right = *pTmp++;
      p = mvprob(left, right, entropy->oldMvProb[i][4 + j]);
      if (update(mvUpdateProb[i][4 + j], left, right,
                 entropy->oldMvProb[i][4 + j], p, 6))
        entropy->mvProb[i][4 + j] = p;
      branchCnt[j] = left + right;
    }
    /* short mv probs, branch 1 */
    p = mvprob(branchCnt[0], branchCnt[1], entropy->oldMvProb[i][3]);
    if (update(mvUpdateProb[i][3], branchCnt[0], branchCnt[1],
               entropy->oldMvProb[i][3], p, 6))
      entropy->mvProb[i][3] = p;

    /* store total count for branch 0 computation */
    type = branchCnt[0] + branchCnt[1];

    /* short mv probs, branches 5 and 6 (4/5 and 6/7) */
    for (j = 0; j < 2; j++) {
      left = *pTmp++;
      right = *pTmp++;
      p = mvprob(left, right, entropy->oldMvProb[i][7 + j]);
      if (update(mvUpdateProb[i][7 + j], left, right,
                 entropy->oldMvProb[i][7 + j], p, 6))
        entropy->mvProb[i][7 + j] = p;
      branchCnt[j] = left + right;
    }
    /* short mv probs, branch 4 */
    p = mvprob(branchCnt[0], branchCnt[1], entropy->oldMvProb[i][6]);
    if (update(mvUpdateProb[i][6], branchCnt[0], branchCnt[1],
               entropy->oldMvProb[i][6], p, 6))
      entropy->mvProb[i][6] = p;

    /* short mv probs, branch 0 */
    p = mvprob(type, branchCnt[0] + branchCnt[1], entropy->oldMvProb[i][2]);
    if (update(mvUpdateProb[i][2], type, branchCnt[0] + branchCnt[1],
               entropy->oldMvProb[i][2], p, 6))
      entropy->mvProb[i][2] = p;
  }
}
