/*
 * Board support for EVAL-ADICUP3029 (ADuCM3029): clock, debug UART, and the
 * SPI and pin functions the AD5940 library expects from its port layer.
 *
 * UART0: P0.10 TX, P0.11 RX, 230400 8N1 (routed to the DAPLink serial port).
 * SPI0:  P0.0 SCLK, P0.1 MOSI, P0.2 MISO, chip select on P1.10.
 * AD5940 reset: P2.6 (Arduino A3).
 *
 * The UART receiver is not used and its interrupt stays disabled: bytes sent
 * by the host are ignored.
 */
#include <stdio.h>
#include "ADuCM3029.h"
#include "ad5940.h"

#define CORE_CLOCK_HZ 26000000u     /* 26 MHz crystal, no dividers */
#define CS_PIN        (1u << 10)    /* P1.10 */
#define RESET_PIN     (1u << 6)     /* P2.6 */

void AD5940_Main(void);             /* the application */

static void ClockInit(void)
{
  pADI_WDT0->CTL = 0xC9;            /* watchdog off */
  pADI_CLKG0_OSC->KEY = 0xCB14;
  pADI_CLKG0_OSC->CTL = BITM_CLKG_OSC_CTL_HFOSCEN | BITM_CLKG_OSC_CTL_HFXTALEN;
  while((pADI_CLKG0_OSC->CTL & BITM_CLKG_OSC_CTL_HFXTALOK) == 0);
  pADI_CLKG0_OSC->KEY = 0xCB14;
  pADI_CLKG0_CLK->CTL0 = 0x201;     /* root clock: 26 MHz crystal */
  pADI_CLKG0_CLK->CTL1 = 0;         /* HCLK = PCLK = ACLK = root clock */
  pADI_CLKG0_CLK->CTL5 = 0;         /* all peripheral clocks on */
}

static void UartInit(void)
{
  /* P0.10 and P0.11 to the UART function */
  pADI_GPIO0->CFG = (pADI_GPIO0->CFG & ~((3u << 20) | (3u << 22))) | (1u << 20) | (1u << 22);
  /* 26 MHz / (32 * 2 * (1 + 1563/2048)) = 230402 baud */
  pADI_UART0->COMLCR2 = 3;          /* oversampling by 32 */
  pADI_UART0->COMDIV = 2;
  pADI_UART0->COMFBR = 0x8000 | (1u << 11) | 1563;
  pADI_UART0->COMIEN = 0;
  pADI_UART0->COMLCR = 3;           /* 8 data bits, no parity, 1 stop bit */
  pADI_UART0->COMFCR = BITM_UART_COMFCR_FIFOEN | BITM_UART_COMFCR_RFCLR | BITM_UART_COMFCR_TFCLR;
  pADI_UART0->COMFCR = BITM_UART_COMFCR_FIFOEN;
}

/* printf output */
int _write(int fd, char *buf, int len)
{
  (void)fd;
  for(int i = 0; i < len; i++)
  {
    pADI_UART0->COMTX = buf[i];
    while((pADI_UART0->COMLSR & 0x20) == 0);   /* wait for room in the transmitter */
  }
  return len;
}

uint32_t AD5940_MCUResourceInit(void *pCfg)
{
  (void)pCfg;
  pADI_GPIO0->PE = 0xFFFF;          /* pull-ups on all pins */
  pADI_GPIO1->PE = 0xFFFF;
  pADI_GPIO2->PE = 0xFFFF;

  pADI_GPIO2->OEN |= RESET_PIN;
  pADI_GPIO2->SET = RESET_PIN;
  pADI_GPIO1->CFG &= ~(3u << 20);   /* P1.10 as GPIO */
  pADI_GPIO1->OEN |= CS_PIN;
  pADI_GPIO1->SET = CS_PIN;

  /* P0.0, P0.1, P0.2 to SPI0; master, 13 MHz, transfer started by writing TX */
  pADI_GPIO0->CFG = (pADI_GPIO0->CFG & ~0x3Fu) | 0x15u;
  pADI_SPI0->DIV = 0;
  pADI_SPI0->CTL = BITM_SPI_CTL_CSRST | BITM_SPI_CTL_MASEN | BITM_SPI_CTL_OEN |
                   BITM_SPI_CTL_RXOF | BITM_SPI_CTL_TIM | BITM_SPI_CTL_SPIEN;
  pADI_SPI0->CNT = 1;
  return 0;
}

void AD5940_ReadWriteNBytes(unsigned char *pSendBuffer, unsigned char *pRecvBuff, unsigned long length)
{
  unsigned long sent = 0, received = 0;

  pADI_SPI0->CNT = length;
  while(received < length)
  {
    uint32_t fifo = pADI_SPI0->FIFO_STAT;
    if(fifo & 0xF00)                          /* bytes in the receive FIFO */
    {
      *pRecvBuff++ = pADI_SPI0->RX;
      received++;
    }
    if(sent < length && (fifo & 0xF) < 8)     /* room in the transmit FIFO */
    {
      pADI_SPI0->TX = *pSendBuffer++;
      sent++;
    }
  }
  while((pADI_SPI0->STAT & BITM_SPI_STAT_XFRDONE) == 0);
}

void AD5940_CsClr(void)  { pADI_GPIO1->CLR = CS_PIN; }
void AD5940_CsSet(void)  { pADI_GPIO1->SET = CS_PIN; }
void AD5940_RstClr(void) { pADI_GPIO2->CLR = RESET_PIN; }
void AD5940_RstSet(void) { pADI_GPIO2->SET = RESET_PIN; }

/* Busy wait in units of 10 us on the 24 bit SysTick counter */
void AD5940_Delay10us(uint32_t time)
{
  const uint32_t ticks_per_10us = CORE_CLOCK_HZ/100000;
  const uint32_t max_units = 0xFFFFFFu/ticks_per_10us;

  while(time)
  {
    uint32_t units = time > max_units ? max_units : time;
    SysTick->LOAD = units*ticks_per_10us;
    SysTick->VAL = 0;
    SysTick->CTRL = (1u << 2) | (1u << 0);      /* core clock, enable */
    while((SysTick->CTRL & (1u << 16)) == 0);   /* until it counts to zero */
    SysTick->CTRL = 0;
    time -= units;
  }
}

/* The applications poll the AD5940, its interrupt line is not used */
uint32_t AD5940_GetMCUIntFlag(void) { return 0; }
uint32_t AD5940_ClrMCUIntFlag(void) { return 1; }

int main(void)
{
  ClockInit();
  UartInit();
  AD5940_MCUResourceInit(0);
  setvbuf(stdout, NULL, _IONBF, 0);
  printf("# %s %s\n", __DATE__, __TIME__);
  AD5940_Main();
  return 0;
}
