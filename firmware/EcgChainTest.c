/*
 * Reference test of the ECG chain on EVAL-AD5940BIOZ: the AD5940 high speed
 * DAC drives the AD8233 +IN (through the SE0 pin and the P bus) while the
 * HSTIA holds -IN at 1.11 V, so a known differential voltage is applied at
 * the AD8233 inputs, independent of leads and body. A synthetic heartbeat
 * (1 mV R wave, 0.2 mV T wave, once per second) is played and the AD8233
 * output is printed in mV at 250 SPS, unfiltered. Expected gain: 296.
 */
#include "ad5940.h"
#include <stdio.h>
#include <math.h>

#define VREF1P11  1.11f
#define VREF1P82  1.82f
#define AD8233_ON (AGPIO_Pin5|AGPIO_Pin1)   /* GP5: SDN, GP1: RLD SDN */

#define MAINS_TAPS 5                        /* 250 SPS / 50 Hz */

static uint32_t FifoBuff[256];

/* Moving average over one mains period: nulls 50 Hz and its harmonics */
static float MainsFilter(float mv)
{
  static float hist[MAINS_TAPS], sum;
  static uint32_t idx, fill;
  sum += mv - hist[idx];
  hist[idx] = mv;
  idx = (idx + 1) % MAINS_TAPS;
  if(fill < MAINS_TAPS)
    fill++;
  return sum/fill;
}

static void SetSwitches(uint32_t p, uint32_t n, uint32_t t)
{
  SWMatrixCfg_Type sw;
  sw.Dswitch = SWD_OPEN;
  sw.Pswitch = p;
  sw.Nswitch = n;
  sw.Tswitch = t;
  AD5940_SWMatrixCfgS(&sw);
}

static void SetFilter(BoolFlag notch)
{
  ADCFilterCfg_Type f;
  f.ADCSinc3Osr = ADCSINC3OSR_4;
  f.ADCSinc2Osr = ADCSINC2OSR_800;    /* 800 kSPS / 4 / 800 = 250 SPS */
  f.ADCAvgNum = ADCAVGNUM_2;
  f.ADCRate = ADCRATE_800KHZ;
  f.BpNotch = notch ? bFALSE : bTRUE;
  f.BpSinc3 = bFALSE;
  f.Sinc2NotchEnable = bTRUE;
  AD5940_ADCFilterCfgS(&f);
}

/* DC voltage of one ADC mux input, in V (valid from about 0.2 V to 2.9 V) */
static float Measure(uint32_t muxp)
{
  uint32_t rd = 0;
  AD5940_ADCMuxCfgS(muxp, ADCMUXN_VSET1P1);
  AD5940_Delay10us(2000);
  for(int i = 0; i < 4; i++)
  {
    AD5940_INTCClrFlag(AFEINTSRC_SINC2RDY);
    while(AD5940_INTCTestFlag(AFEINTC_1, AFEINTSRC_SINC2RDY) == bFALSE);
    rd = AD5940_ReadAfeResult(AFERESULT_SINC2);
  }
  return AD5940_ADCCode2Volt(rd, ADCPGA_1, VREF1P82) + VREF1P11;
}

/* Is the lead on the P bus on the same body as the S- lead? Both inputs are
   pulled up to the rail when open and sit at mid supply on a body. */
static void CheckLead(const char *name, uint32_t psw, float rail)
{
  float p, n;
  SetSwitches(SWP_AFE2|psw, SWN_OPEN, SWT_AIN0|SWT_AFE3LOAD);
  AD5940_Delay10us(50000);
  p = Measure(ADCMUXP_VAFE2);
  n = Measure(ADCMUXP_VAFE3);
  printf("# lead %s: IN+ %.2f V, IN- %.2f V: %s\n", name, p, n,
         (p - n < 0.5f && n - p < 0.5f && p < rail - 0.4f) ? "connected" : "OPEN");
}

static void SelfCheck(void)
{
  float rail;
  SetSwitches(SWP_OPEN, SWN_OPEN, SWT_OPEN);
  AD5940_Delay10us(50000);
  rail = Measure(ADCMUXP_VAFE2);    /* IN+ is pulled up to the AD8233 supply */
  printf("# AD8233 input pull-up rail: %.2f V%s\n", rail,
         rail < 2.8f ? " (LOW: AD8233 not powered, check P10)" : "");
  CheckLead("S+ (IN+)", SWP_DE0, rail);
  CheckLead("F+ (IN+, red)", SWP_RE0, rail);
}

void AD5940_Main(void)
{
  CLKCfg_Type clk_cfg;
  AGPIOCfg_Type gpio_cfg;
  AFERefCfg_Type aferef_cfg;
  ADCBaseCfg_Type adc_base;
  FIFOCfg_Type fifo_cfg;
  uint32_t lod = 0, lodchg = 0, total = 0, sat = 0;
  uint32_t beat = 0;
  float uvcode = 4.9f;

  AD5940_HWReset();
  AD5940_Initialize();
  printf("# ECG chain test, ADIID:0x%04x CHIPID:0x%04x\n", (unsigned)AD5940_GetADIID(), (unsigned)AD5940_GetChipID());

  clk_cfg.ADCClkDiv = ADCCLKDIV_1;
  clk_cfg.ADCCLkSrc = ADCCLKSRC_HFOSC;
  clk_cfg.SysClkDiv = SYSCLKDIV_1;
  clk_cfg.SysClkSrc = SYSCLKSRC_HFOSC;
  clk_cfg.HfOSC32MHzMode = bFALSE;
  clk_cfg.HFOSCEn = bTRUE;
  clk_cfg.HFXTALEn = bFALSE;
  clk_cfg.LFOSCEn = bTRUE;
  AD5940_CLKCfg(&clk_cfg);
  AD5940_AFEPwrBW(AFEPWR_LP, AFEBW_250KHZ);

  /* GP5/GP1 drive the AD8233 shutdown pins, GP4 reads its leads-off output */
  gpio_cfg.FuncSet = GP6_SYNC|GP5_SYNC|GP4_GPIO|GP2_TRIG|GP1_SYNC|GP0_INT;
  gpio_cfg.InputEnSet = AGPIO_Pin2|AGPIO_Pin4;
  gpio_cfg.OutputEnSet = AGPIO_Pin0|AGPIO_Pin1|AGPIO_Pin5|AGPIO_Pin6;
  gpio_cfg.OutVal = 0;
  gpio_cfg.PullEnSet = AGPIO_Pin2;
  AD5940_AGPIOCfg(&gpio_cfg);
  AD5940_SEQGpioCtrlS(AD8233_ON);

  AD5940_AFECtrlS(AFECTRL_ALL, bFALSE);
  aferef_cfg.HpBandgapEn = bTRUE;
  aferef_cfg.Hp1V1BuffEn = bTRUE;
  aferef_cfg.Hp1V8BuffEn = bTRUE;
  aferef_cfg.Disc1V1Cap = bFALSE;
  aferef_cfg.Disc1V8Cap = bFALSE;
  aferef_cfg.Hp1V8ThemBuff = bFALSE;
  aferef_cfg.Hp1V8Ilimit = bFALSE;
  aferef_cfg.Lp1V1BuffEn = bFALSE;
  aferef_cfg.Lp1V8BuffEn = bFALSE;
  aferef_cfg.LpBandgapEn = bFALSE;
  aferef_cfg.LpRefBufEn = bFALSE;
  aferef_cfg.LpRefBoostEn = bFALSE;
  AD5940_REFCfgS(&aferef_cfg);

  adc_base.ADCMuxP = ADCMUXP_AIN6;
  adc_base.ADCMuxN = ADCMUXN_VSET1P1;
  adc_base.ADCPga = ADCPGA_1;
  AD5940_ADCBaseCfgS(&adc_base);
  SetFilter(bFALSE);

  AD5940_AFECtrlS(AFECTRL_HPREFPWR, bTRUE);
  AD5940_INTCCfg(AFEINTC_1, AFEINTSRC_ALLINT, bTRUE);
  AD5940_ADCPowerCtrlS(bTRUE);
  AD5940_ADCConvtCtrlS(bTRUE);

  /* Excitation amplifier forces V(P) - V(N) = DAC; P = SE0 + AD8233 +IN, N/T = -IN */
  {
    HSLoopCfg_Type hs;
    float p0, n0, p1, p2;
    AD5940_StructInit(&hs, sizeof(hs));
    hs.HsDacCfg.ExcitBufGain = EXCITBUFGAIN_0P25;
    hs.HsDacCfg.HsDacGain = HSDACGAIN_0P2;
    hs.HsDacCfg.HsDacUpdateRate = 7;
    hs.HsTiaCfg.DiodeClose = bFALSE;
    hs.HsTiaCfg.HstiaBias = HSTIABIAS_1P1;
    hs.HsTiaCfg.HstiaCtia = 31;
    hs.HsTiaCfg.HstiaDeRload = HSTIADERLOAD_OPEN;
    hs.HsTiaCfg.HstiaDeRtia = HSTIADERTIA_OPEN;
    hs.HsTiaCfg.HstiaRtiaSel = HSTIARTIA_1K;
    hs.SWMatCfg.Dswitch = SWD_SE0;
    hs.SWMatCfg.Pswitch = SWP_SE0|SWP_AFE2;
    hs.SWMatCfg.Nswitch = SWN_AFE3LOAD;
    hs.SWMatCfg.Tswitch = SWT_AFE3LOAD|SWT_TRTIA;
    hs.WgCfg.WgType = WGTYPE_MMR;
    hs.WgCfg.GainCalEn = bFALSE;
    hs.WgCfg.OffsetCalEn = bFALSE;
    hs.WgCfg.WgCode = 0x800;
    AD5940_HSLoopCfgS(&hs);
    AD5940_AFECtrlS(AFECTRL_DACREFPWR, bTRUE);
    AD5940_AFECtrlS(AFECTRL_EXTBUFPWR|AFECTRL_INAMPPWR|AFECTRL_HSTIAPWR|AFECTRL_HSDACPWR, bTRUE);
    AD5940_AFECtrlS(AFECTRL_WG, bTRUE);
    AD5940_Delay10us(50000);
    p0 = Measure(ADCMUXP_VAFE2);
    n0 = Measure(ADCMUXP_VAFE3);
    AD5940_WriteReg(REG_AFE_HSDACDAT, 0x800 + 1600);
    AD5940_Delay10us(20000);
    p1 = Measure(ADCMUXP_VAFE2);
    AD5940_WriteReg(REG_AFE_HSDACDAT, 0x800 - 1600);
    AD5940_Delay10us(20000);
    p2 = Measure(ADCMUXP_VAFE2);
    AD5940_WriteReg(REG_AFE_HSDACDAT, 0x800);
    uvcode = (p1 - p2)*1e6f/3200;
    printf("# inputs at mid code: IN+ %.4f V, IN- %.4f V; +1600 codes: %.2f mV, -1600 codes: %.2f mV; %.2f uV per code\n",
           p0, n0, (p1 - p0)*1000, (p2 - p0)*1000, uvcode);
    if(uvcode < 1 && uvcode > -1)
      uvcode = 4.9f;
  }

  AD5940_ADCConvtCtrlS(bFALSE);
  AD5940_ADCMuxCfgS(ADCMUXP_AIN6, ADCMUXN_VSET1P1);
  SetFilter(bTRUE);
  fifo_cfg.FIFOEn = bFALSE;
  fifo_cfg.FIFOMode = FIFOMODE_FIFO;
  fifo_cfg.FIFOSize = FIFOSIZE_4KB;
  fifo_cfg.FIFOSrc = FIFOSRC_SINC2NOTCH;
  fifo_cfg.FIFOThresh = 4;
  AD5940_FIFOCfg(&fifo_cfg);
  fifo_cfg.FIFOEn = bTRUE;
  AD5940_FIFOCfg(&fifo_cfg);
  AD5940_ADCConvtCtrlS(bTRUE);
  printf("# streaming AD8233 output in mV at 250 SPS, synthetic beat at the inputs\n");

  while(1)
  {
    uint32_t cnt = AD5940_FIFOGetCnt();
    uint32_t now;
    if(cnt == 0)
      continue;
    if(cnt > sizeof(FifoBuff)/sizeof(FifoBuff[0]))
      cnt = sizeof(FifoBuff)/sizeof(FifoBuff[0]);
    AD5940_FIFORd(FifoBuff, cnt);
    for(uint32_t i = 0; i < cnt; i++)
    {
      float mv = (AD5940_ADCCode2Volt(FifoBuff[i]&0xffff, ADCPGA_1, VREF1P82) + VREF1P11)*1000;
      float in_uv = 0;
      uint32_t k = beat % 250;
      if(mv < 250 || mv > 2900)
        sat++;
      /* R wave: 40 ms triangle of 1 mV; T wave: 160 ms half sine of 0.2 mV */
      if(k < 5)        in_uv = 1000.0f*(k + 1)/5;
      else if(k < 10)  in_uv = 1000.0f*(9 - k)/5;
      else if(k >= 60 && k < 100) in_uv = 200.0f*sinf(3.14159265f*(k - 60)/40);
      beat++;
      AD5940_WriteReg(REG_AFE_HSDACDAT, (uint32_t)(0x800 + (int32_t)(in_uv/uvcode)));
      printf("%.2f %.0f\n", mv, in_uv);
    }
    total += cnt;
    now = (AD5940_AGPIOIn()>>4)&1;
    if(now != lod)
    {
      lod = now;
      lodchg++;
    }
    if(total >= 1250)   /* every 5 s */
    {
      if(lodchg || lod)
        printf("# leads-off pin (GP4): %u, %u changes in 5 s\n", (unsigned)lod, (unsigned)lodchg);
      if(sat > total/2)
        printf("# output saturated: check the electrodes and P10\n");
      total = 0;
      sat = 0;
      lodchg = 0;
    }
  }
}
