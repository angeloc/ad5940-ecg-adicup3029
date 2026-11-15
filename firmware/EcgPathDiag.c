/* DC diagnostic of the ECG path on EVAL-AD5940BIOZ: same switch matrix and
   AD8233 control pins as the ECG example, then measure node voltages. */
#include "ad5940.h"
#include <stdio.h>

static float measure(uint32_t muxp)
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

void AD5940_Main(void)
{
  static const struct { const char *name; uint32_t mux; } nodes[] = {
    {"AIN6(AD8233 out)", ADCMUXP_AIN6}, {"AFE2(ECG_P)", ADCMUXP_VAFE2},
    {"AFE3(ECG_N)", ADCMUXP_VAFE3}, {"DE0", ADCMUXP_VDE0}, {"RE0", ADCMUXP_VRE0},
    {"SE0", ADCMUXP_VSE0}, {"AIN0", ADCMUXP_AIN0}, {"AIN1", ADCMUXP_AIN1},
    {"AIN2", ADCMUXP_AIN2}, {"AIN3", ADCMUXP_AIN3}, {"CE0", ADCMUXP_VCE0},
    {"AVDD/2", ADCMUXP_AVDD_2},
  };
  ADCBaseCfg_Type adc_base;
  ADCFilterCfg_Type adc_filter;
  AFERefCfg_Type aferef_cfg;
  SWMatrixCfg_Type sw_matrix;
  AGPIOCfg_Type gpio_cfg;
  uint32_t pass = 0;

  AD5940_HWReset();
  AD5940_Initialize();
  printf("# DIAG ADIID:0x%04x CHIPID:0x%04x\n", (unsigned)AD5940_GetADIID(), (unsigned)AD5940_GetChipID());
  AD5940_AFEPwrBW(AFEPWR_LP, AFEBW_250KHZ);

  gpio_cfg.FuncSet = GP6_SYNC|GP5_SYNC|GP4_GPIO|GP2_TRIG|GP1_SYNC|GP0_INT;
  gpio_cfg.InputEnSet = AGPIO_Pin2|AGPIO_Pin4;
  gpio_cfg.OutputEnSet = AGPIO_Pin0|AGPIO_Pin1|AGPIO_Pin5|AGPIO_Pin6;
  gpio_cfg.OutVal = 0;
  gpio_cfg.PullEnSet = AGPIO_Pin2;
  AD5940_AGPIOCfg(&gpio_cfg);
  AD5940_SEQGpioCtrlS(AGPIO_Pin5|AGPIO_Pin1);   /* AD8233 on, RLD on */

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
  adc_filter.ADCSinc3Osr = ADCSINC3OSR_4;
  adc_filter.ADCSinc2Osr = ADCSINC2OSR_1333;
  adc_filter.ADCAvgNum = ADCAVGNUM_2;
  adc_filter.ADCRate = ADCRATE_800KHZ;
  adc_filter.BpNotch = bTRUE;
  adc_filter.BpSinc3 = bFALSE;
  adc_filter.Sinc2NotchEnable = bTRUE;
  AD5940_ADCFilterCfgS(&adc_filter);

  sw_matrix.Dswitch = SWD_OPEN;
  sw_matrix.Pswitch = SWP_RE1|SWP_DE0;
  sw_matrix.Nswitch = SWN_AIN2|SWN_SE0;
  sw_matrix.Tswitch = SWT_AIN0|SWT_AFE3LOAD;
  AD5940_SWMatrixCfgS(&sw_matrix);

  AD5940_AFECtrlS(AFECTRL_HPREFPWR, bTRUE);
  AD5940_INTCCfg(AFEINTC_1, AFEINTSRC_ALLINT, bTRUE);
  AD5940_ADCPowerCtrlS(bTRUE);
  AD5940_ADCConvtCtrlS(bTRUE);

  while(1)
  {
    /* cycle: example switches / all open / example plus RE0 (F+ lead) */
    uint32_t mode = pass % 3;
    uint32_t open = (mode == 1);
    sw_matrix.Dswitch = SWD_OPEN;
    sw_matrix.Pswitch = open ? SWP_OPEN : (SWP_RE1|SWP_DE0|(mode == 2 ? SWP_RE0 : 0));
    sw_matrix.Nswitch = open ? SWN_OPEN : (SWN_AIN2|SWN_SE0);
    sw_matrix.Tswitch = open ? SWT_OPEN : (SWT_AIN0|SWT_AFE3LOAD);
    AD5940_SWMatrixCfgS(&sw_matrix);
    AD5940_Delay10us(50000);
    printf("# pass %u switches:%s GPIN:0x%02x SWCON:0x%08x\n", (unsigned)pass, open ? "open" : (mode == 2 ? "ecg+re0" : "ecg"),
           (unsigned)AD5940_AGPIOIn(), (unsigned)AD5940_ReadReg(REG_AFE_SWCON));
    for(unsigned i = 0; i < sizeof(nodes)/sizeof(nodes[0]); i++)
      printf("#  %-17s %.3f V\n", nodes[i].name, measure(nodes[i].mux));
    pass++;
    AD5940_Delay10us(100000);
  }
}
