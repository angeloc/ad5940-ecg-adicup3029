/*
 * ECG streaming for EVAL-AD5940BIOZ: the AD8233 output (AIN6) is sampled
 * continuously at 250 SPS and printed in mV, one sample per line, after a
 * 5 sample moving average (one 50 Hz period) that removes the mains hum.
 * Lines starting with '#' are status. No sequencer, no hibernate, no
 * interrupt: the FIFO is polled.
 *
 * Leads: F+ or S+ = ECG IN+, S- = ECG IN-, F- = right leg drive. On the
 * three probe cable: red = F+, blue = S-, black = F-.
 */
#include "ad5940.h"
#include <stdio.h>

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

  AD5940_HWReset();
  AD5940_Initialize();
  printf("# ECG stream, ADIID:0x%04x CHIPID:0x%04x\n", (unsigned)AD5940_GetADIID(), (unsigned)AD5940_GetChipID());

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

  SelfCheck();

  /* Stream: both IN+ leads (F+ on RE0, S+ on DE0) to the AD8233 +IN, S- to -IN */
  AD5940_ADCConvtCtrlS(bFALSE);
  SetSwitches(SWP_RE0|SWP_AFE2|SWP_DE0, SWN_AIN2|SWN_SE0, SWT_AIN0|SWT_AFE3LOAD);
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
  printf("# streaming AD8233 output in mV at 250 SPS\n");

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
      if(mv < 250 || mv > 2900)
        sat++;
      printf("%.2f\n", MainsFilter(mv));
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
