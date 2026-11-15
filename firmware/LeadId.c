/*
 * Lead identification for EVAL-AD5940BIOZ: clip two probe tips together
 * (not on the body) and this prints which two leads are shorted.
 *
 * - a lead shorted to F- follows the AD8233 right leg drive when it is
 *   switched on and off;
 * - a lead shorted to S- follows S- when the AD5940 HSTIA holds it at 1.11 V.
 */
#include "ad5940.h"
#include <stdio.h>

#define GP_SDN    AGPIO_Pin5
#define GP_RLDSDN AGPIO_Pin1

static void SetSwitches(uint32_t p, uint32_t n, uint32_t t)
{
  SWMatrixCfg_Type sw;
  sw.Dswitch = SWD_OPEN;
  sw.Pswitch = p;
  sw.Nswitch = n;
  sw.Tswitch = t;
  AD5940_SWMatrixCfgS(&sw);
}

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
  return AD5940_ADCCode2Volt(rd, ADCPGA_1, 1.82) + 1.11f;
}

/* Voltage of the AD8233 input a lead is routed to, right leg drive on then off */
static void RldTest(uint32_t p, uint32_t t, uint32_t mux, float *on, float *off)
{
  SetSwitches(p, SWN_OPEN, t);
  AD5940_SEQGpioCtrlS(GP_SDN|GP_RLDSDN);
  AD5940_Delay10us(50000);
  *on = Measure(mux);
  AD5940_SEQGpioCtrlS(GP_SDN);
  AD5940_Delay10us(50000);
  *off = Measure(mux);
}

/* Voltage of the AD8233 +IN with a lead routed to it while S- is held at 1.11 V */
static float SminusTest(uint32_t p)
{
  float v;
  AD5940_SEQGpioCtrlS(GP_SDN);
  SetSwitches(SWP_AFE2|p, SWN_AIN0, SWT_AIN0|SWT_TRTIA);
  AD5940_AFECtrlS(AFECTRL_HSTIAPWR, bTRUE);
  AD5940_Delay10us(50000);
  v = Measure(ADCMUXP_VAFE2);
  AD5940_AFECtrlS(AFECTRL_HSTIAPWR, bFALSE);
  return v;
}

void AD5940_Main(void)
{
  AGPIOCfg_Type gpio_cfg;
  AFERefCfg_Type aferef_cfg;
  ADCBaseCfg_Type adc_base;
  ADCFilterCfg_Type adc_filter;
  HSTIACfg_Type hstia_cfg;

  AD5940_HWReset();
  AD5940_Initialize();
  printf("# lead id, ADIID:0x%04x CHIPID:0x%04x\n", (unsigned)AD5940_GetADIID(), (unsigned)AD5940_GetChipID());
  AD5940_AFEPwrBW(AFEPWR_LP, AFEBW_250KHZ);

  gpio_cfg.FuncSet = GP6_SYNC|GP5_SYNC|GP4_GPIO|GP2_TRIG|GP1_SYNC|GP0_INT;
  gpio_cfg.InputEnSet = AGPIO_Pin2|AGPIO_Pin4;
  gpio_cfg.OutputEnSet = AGPIO_Pin0|AGPIO_Pin1|AGPIO_Pin5|AGPIO_Pin6;
  gpio_cfg.OutVal = 0;
  gpio_cfg.PullEnSet = AGPIO_Pin2;
  AD5940_AGPIOCfg(&gpio_cfg);
  AD5940_SEQGpioCtrlS(GP_SDN|GP_RLDSDN);

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

  hstia_cfg.DiodeClose = bFALSE;
  hstia_cfg.HstiaBias = HSTIABIAS_1P1;
  hstia_cfg.HstiaCtia = 31;
  hstia_cfg.HstiaDeRload = HSTIADERLOAD_OPEN;
  hstia_cfg.HstiaDeRtia = HSTIADERTIA_OPEN;
  hstia_cfg.HstiaRtiaSel = HSTIARTIA_1K;
  hstia_cfg.ExtRtia = 0;
  AD5940_HSTIACfgS(&hstia_cfg);

  adc_base.ADCMuxP = ADCMUXP_VAFE2;
  adc_base.ADCMuxN = ADCMUXN_VSET1P1;
  adc_base.ADCPga = ADCPGA_1;
  AD5940_ADCBaseCfgS(&adc_base);
  adc_filter.ADCSinc3Osr = ADCSINC3OSR_4;
  adc_filter.ADCSinc2Osr = ADCSINC2OSR_800;
  adc_filter.ADCAvgNum = ADCAVGNUM_2;
  adc_filter.ADCRate = ADCRATE_800KHZ;
  adc_filter.BpNotch = bTRUE;
  adc_filter.BpSinc3 = bFALSE;
  adc_filter.Sinc2NotchEnable = bTRUE;
  AD5940_ADCFilterCfgS(&adc_filter);

  AD5940_AFECtrlS(AFECTRL_HPREFPWR, bTRUE);
  AD5940_INTCCfg(AFEINTC_1, AFEINTSRC_ALLINT, bTRUE);
  AD5940_ADCPowerCtrlS(bTRUE);
  AD5940_ADCConvtCtrlS(bTRUE);

  while(1)
  {
    float sp_on, sp_off, fp_on, fp_off, sm_on, sm_off, sp_sm, fp_sm;
    const char *res = "none (or S+ with F+)";

    RldTest(SWP_AFE2|SWP_DE0, SWT_OPEN, ADCMUXP_VAFE2, &sp_on, &sp_off);
    RldTest(SWP_AFE2|SWP_RE0, SWT_OPEN, ADCMUXP_VAFE2, &fp_on, &fp_off);
    RldTest(SWP_OPEN, SWT_AIN0|SWT_AFE3LOAD, ADCMUXP_VAFE3, &sm_on, &sm_off);
    sp_sm = SminusTest(SWP_DE0);
    fp_sm = SminusTest(SWP_RE0);

    if(sp_off - sp_on > 1.0f)      res = "S+ with F-";
    else if(fp_off - fp_on > 1.0f) res = "F+ with F-";
    else if(sm_off - sm_on > 1.0f) res = "S- with F-";
    else if(sp_sm < 1.8f)          res = "S+ with S-";
    else if(fp_sm < 1.8f)          res = "F+ with S-";

    printf("# rld on/off: S+ %.2f/%.2f  F+ %.2f/%.2f  S- %.2f/%.2f | S- held: S+ %.2f  F+ %.2f | shorted: %s\n",
           sp_on, sp_off, fp_on, fp_off, sm_on, sm_off, sp_sm, fp_sm, res);
  }
}
