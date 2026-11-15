# Bench test log: ECG on EVAL-ADICUP3029 + EVAL-AD5940BIOZ

Record of the tests done on 2026-11-14 and 2026-11-15 to get an ECG out of this hardware, starting
from the Analog Devices AD5940_ECG example, in the order they were done. Voltages at the "output"
are the AD8233 output (AIN6); the chain gain is about 220 for a narrow pulse (nominal 296), so
100 mV at the output is about 0.45 mV at the electrodes. The ADC clips at about 2.94 V.

## Summary of causes

1. The stock example hangs on the first byte received on the UART (no `UART_Int_Handler`).
2. The stand-alone example does not route the F+ lead, where the positive probe is.
3. P10 was not powering the AD8233.
4. P17 biased the - input opposite to the + input (0.32 V offset, output at a rail).
5. P13: fast restore was enabled and kept re-triggering (regular pulses, no ECG band).
6. Laptop on mains (charger and dock): interference buried the heartbeat. On battery it is clear.

## 2026-11-14

### Stock example gives no samples

- Listening on the UART (230400 baud): nothing in 6 s.
- GCC build with a `UART_Int_Handler`, error reporting and a polling fallback: chip IDs
  `ADIID 0x4144`, `CHIPID 0x5502`, LFOSC 32245 Hz, init 0, start 0, samples at 250 per second, all
  through the GP0 interrupt.
- A/B test: a build without the handler streams, then stops after one byte sent to the board. The
  debugger shows the CPU at `Default_Handler` (`while(1)`), exception number 30 (UART interrupt).
  The build with the handler keeps streaming after 7 bytes.
- The DAPLink does not reset the target after programming; after a drag-and-drop the CPU sits in
  the flash loader at 0x20000000 until reset.
- Later the GP0 interrupt was missed once (FIFO at 754 samples, threshold flag set); the polling
  fallback recovered it.

### Output saturated (65532)

DC diagnostic (`EcgPathDiag`), no electrodes, then electrodes on:

| Node | Value |
|---|---|
| AD8233 +IN (AFE2) | 2.945 V (clipped) |
| AD8233 -IN (AFE3) | 0.2 to 0.3 V |
| AD8233 output | 2.945 V (clipped) |
| AVDD/2 | 1.619 V |

- After a first P10 change the rail read 2.07 V. Driving the AD8233 control pins: both low 0 V,
  one high 1.75 V, both high 2.07 V. So the AD8233 was back-powered through GP5/GP1 and not
  through P10. After correcting P10 the rail reads 2.94 V.
- With a third switch setting that adds RE0 (F+) to the + input, the + input dropped from 2.07 V
  to 0.63 V next to the - input at 0.51 V: the positive electrode is on F+, not on S+.

### New streaming firmware (`EcgStream`)

- 250 SPS, FIFO polled, F+ and S+ both routed, self check at reset.
- Fingers on the electrodes: mains hum 90 % of the power at 49.84 Hz (about 180 mV peak to peak at
  the output); the AD5940 notch did not remove it. A 5 sample moving average brought it to 0 %.
- With P17 pulling the - input down: inputs 1.19 V and 0.87 V (0.32 V offset), output stuck at
  0 V for 25 s. After moving P17 like P16: inputs within 40 to 80 mV, output mid range.

### Probe identification (`LeadId`)

Probe tips clipped together, off the body:

| Clipped | Reading | Result |
|---|---|---|
| red + black | F+ follows the right leg drive: 0.14 V on, 2.93 V off | F+ with F- |
| red + blue | F+ reads 1.19 V with S- held at 1.11 V | F+ with S- |

So red = F+ (input +), blue = S- (input -), black = F- (right leg drive).

### Regular pulses instead of an ECG

Probes on the body:

- Clean pulses of 400 to 1000 mV, about 36 ms wide, on a rigid grid: 175.5 samples (0.7020 s)
  with extra pulses at multiples of 0.175 s. Jitter 0.40 ms over 16 intervals; the period equals
  35 mains cycles (mains measured at 49.85 Hz in board time).
- Average of 18 pulses: single spike of about 560 mV, baseline flat within 3 mV before and after,
  no T wave.
- Three phase test: normal routing gives the pulses; leads disconnected and inputs shorted gives
  1.3 mV rms (the board does not generate them); right leg drive off saturates the output.
- Raw output without the moving average: median 1.1 V, 190 mV peak to peak of 50 Hz, never near
  a rail.
- Logging the leads-off pin (GP4) with every sample: no transition while the pulses continued.

## 2026-11-15

### Fast restore (P13)

- After reconnecting leads that carry 120 to 180 mV of DC offset the output was back on its
  baseline within 0.2 s, and from saturation within 0.02 s: only fast restore recovers that fast.
- P13 moved to 2-3 (FR to ground): the regular pulses disappeared and the baseline wanders slowly
  (176 mV over 20 s), as expected with the 0.34 Hz high-pass.
- Still no heartbeat: noise of about 20 mV rms in 8 to 30 Hz, peaks of 25 to 80 mV at random.

### Direct ADC measurement of the electrodes

- ADC on F+ (RE0) minus S- (AIN0), gain 2: 120 mV DC offset, 0.65 mV rms of noise in 3 to 35 Hz.
  Not usable: the ADC is too noisy on these high impedance nodes.

### Contact resistance

- With S- held at 1.11 V and the right leg drive off, the + input read 1.44 V: loop of about
  2.2 MOhm including the two 300 kOhm resistors, so about 0.8 MOhm per dry pad. Red to black
  through the body is connected as well.

### Reference test of the chain (`EcgChainTest`)

DAC scale measured at 9.99 uV per code (+1600 codes = 16.07 mV, -1600 = -15.90 mV); inputs at mid
code 1.0801 V and 1.0800 V. Synthetic beat once per second, 44 beats averaged:

| Applied at the inputs | Output | Gain |
|---|---|---|
| R wave, 1 mV, 40 ms triangle | 222 mV | 222 |
| T wave, 0.2 mV, 160 ms half sine | 44 mV | 218 |

Beat to beat spread 2.2 mV, baseline noise 1.9 mV rms (about 6.5 uV at the inputs). The AD8233,
the ADC and the firmware are fine.

### On the body, laptop on mains

Red left wrist, blue right wrist, black ankle, sticky pads:

| Pads | Noise 8 to 30 Hz | Heartbeat |
|---|---|---|
| dry | 20.6 mV rms | none; a matched filter finds no regular beat above about 0.35 mV |
| wet | 22.4 mV rms | none |

Wetting the pads changed nothing, so the noise does not come from the pad contact.

### On the body, laptop on battery

Charger and dock unplugged, board plugged directly into the laptop, same pads and placement:

- 55 beats in 51 s, 64 per minute, intervals from 0.76 to 1.24 s, 98 % within 20 % of the median.
- R peaks 140 to 177 mV at the output (about 0.83 mV at the electrodes).
- Average beat: small P wave before the spike, R of 163 mV, T wave of 54 mV about 240 ms later.

The ECG is visible only without the mains connection.

## Not explained

- A narrow component near 22 Hz in the noise on mains.
- Why the AD5940 50 Hz notch did not remove the hum (the moving average does).
- The leads-off pin (GP4) toggling about 100 times per second in some captures; it may float if
  P20 is not fitted.

## New project layout (2026-11-15)

- The firmware was moved to its own repository with its own board support
  (`firmware/board_adicup3029.c`) in place of the port files of the Analog Devices example.
- `EcgChainTest` built from it gives the same result on the board: 221 mV for the 1 mV R wave,
  44 mV for the 0.2 mV T wave, noise 1.1 mV rms.
- `EcgStream` runs steadily (2508 samples in 10 s) and the viewer finds the board by its USB id.
