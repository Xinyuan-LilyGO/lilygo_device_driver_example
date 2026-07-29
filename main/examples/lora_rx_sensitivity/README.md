# LoRa RX Sensitivity Test

**English | [中文](./README_CN.md)**

This example performs a conducted LoRa receiver sensitivity test with an RF
signal generator capable of generating LoRa waveforms. It only receives
packets and does not transmit RF signals.

The program selects the radio according to the current device configuration
and automatic radio detection:

| Radio | Test frequency |
| --- | ---: |
| SX1262 | 920 MHz |
| LR2021 | 433 MHz |
| LR1121 | 2450 MHz |

These frequencies match the current hardware RF paths. Do not change them
without verifying the RF matching network and applicable regulatory
requirements.

## Test Criteria

The program uses the LoRa sensitivity conditions defined in the Semtech
datasheets:

- Bandwidth: 125 kHz
- Spreading factor: Set by `kSpreadingFactor`, default SF12
- Coding rate: 4/5
- Preamble: 8 symbols
- Header: Explicit
- Payload: 64 bytes
- Payload CRC: Enabled
- IQ: Standard
- Sync mode: Public, corresponding to `0x34`
- Low data rate optimization: Selected automatically from SF and bandwidth
- Receiver gain: Boosted
- Pass criterion: PER no greater than 1%

Change `kSpreadingFactor` in `lora_rx_sensitivity.h` to set the actual receive
SF used by LR2021, SX1262, and LR1121 together. The valid range is 5 through
12. These are the default program settings. LR1121 operation at 2.4 GHz with
BW125 is outside its datasheet range and cannot directly use an official
sensitivity figure; see the exception in the table below.

The RF signal generator must transmit exactly 1000 packets. PER is calculated
as:

```text
PER = (Expected packets - Correct packets) / Expected packets × 100%
```

Packets with CRC, header, length, payload-content, or read errors are not
counted as correct packets. Packets not observed by the receiver are inferred
as missing from the configured expected packet count.

The serial port prints a status message every 5 seconds while waiting for the
first packet. During an active test, the 5-second interim result includes
packet counts, error breakdown, completion, observed error ratio, RSSI, and
SNR. Formal PER is calculated only after the test ends.

## RF Signal Generator Configuration

Configure the test instrument as a LoRa signal source and make every item
match the receiver:

| Instrument setting | Value |
| --- | --- |
| Frequency | Match the radio frequency in the table above |
| Spreading Factor | Match `kSpreadingFactor`, default SF12 |
| Bandwidth | 125 kHz |
| Coding Rate | 4/5 |
| Preamble Length | 8 |
| Header | Explicit |
| Data Source | All 1 |
| Payload Length | 64 bytes |
| Payload CRC | On |
| Sync Mode | Public, corresponding to `0x34` |
| IQ | Standard |
| Payload Reduced Coding Mode | On |
| Packet/Sequence Count | 1000 |

Select `All 1` as the data source. With a payload length of 64 bytes, every
expected payload byte is `FF`:

```text
FF FF FF FF FF FF FF FF FF FF FF FF FF FF FF FF
FF FF FF FF FF FF FF FF FF FF FF FF FF FF FF FF
FF FF FF FF FF FF FF FF FF FF FF FF FF FF FF FF
FF FF FF FF FF FF FF FF FF FF FF FF FF FF FF FF
```

Do not use infinite repeat mode. Transmit the exact configured packet count
again at every input-power point. The frame period must be longer than the
LoRa packet time on air and must leave enough time for the receiver to
re-enter RX. Packets sent too closely together can cause processing-related
packet loss even at a strong input level.

## RF Connection and Power Calibration

A conducted connection is recommended:

```text
RF signal generator RF OUT -> Fixed attenuator -> RF cable -> DUT RF port
```

The actual power at the DUT input is:

```text
DUT input power =
    RF generator output power - Fixed attenuation - Cable and adapter loss
```

If the test instrument already applies an external-attenuation value or user
correction, use its corrected port level and do not subtract the same loss
twice.

## RF Level Limits and Official Sensitivity References

The test instrument controls RF output power as `RF Level`, in dBm. It does
not set the RSSI reported by the receiver. RSSI is an estimate made by the
radio only after it detects a packet.

When an R&S SMBV100B is used, its standard RF output-level specifications
are shown below. For another signal-generator model, use that instrument's
own specifications and calibration report.

| Instrument item | Official specification | Test meaning |
| --- | ---: | --- |
| Setting range from 1 MHz to 6 GHz | -145 dBm to +20 dBm | Values accepted by the instrument |
| Specified level range from 10 MHz to 6 GHz | -127 dBm to +18 dBm | Range covered by the stated level specifications |
| Level setting resolution | 0.01 dB | This is not 0.01 dB absolute accuracy over the complete range |

The SMBV100B can therefore be set to `-145 dBm`, but direct output below
`-127 dBm` is outside its specified level range. For reproducible
quantitative measurements, add a fixed attenuator so that the generator
stays above `-127 dBm`, then obtain the lower DUT input power through the
known attenuation.

The absolute best sensitivity conditions and commonly compared conditions
are shown below. The absolute-best conditions use a narrower supported
bandwidth and can achieve a lower typical sensitivity, but they also increase
time on air and sensitivity to frequency error and clock drift. They are
therefore not used as the common default in this example. Official
sensitivity is the typical DUT input power that meets `PER = 1%` under the
stated conditions; it is not an exact RSSI value that must appear in the
serial log.

| Radio | Absolute best sensitivity condition | Official typical sensitivity | Common comparison condition | Common-condition sensitivity |
| --- | --- | ---: | --- | ---: |
| LR2021 | SF12 / BW31 kHz / RX Boost Mode 7 | -147 dBm | SF12 / BW125 kHz / RX Boost Mode 7 | -141.5 dBm |
| SX1262 | SF12 / BW10.4 kHz / RX Boosted | -148 dBm | SF12 / BW125 kHz / RX Boosted | -137 dBm |
| LR1121 Sub-GHz | SF12 / BW62.5 kHz / RX Boosted | -144 dBm | SF12 / BW125 kHz / RX Boosted | -141 dBm |
| LR1121 2.4 GHz | Minimum supported BW203 kHz | No corresponding official SF12 value | BW125 cannot be used | No corresponding figure |

These values are typical IC RF-port results under the datasheet conditions.
They exclude losses from RF switches, filters, matching networks, the PCB,
cables, and adapters, and they are not guaranteed worst-case limits.

The `-141 dBm` value in the Semtech product table must not be applied to the
LR1121 row because it is the SF12/BW125 Sub-GHz reference. The LR1121
datasheet defines a 203 kHz to 812 kHz LoRa bandwidth range at 2.4 GHz, so
the current 2450 MHz/BW125 combination is outside the official range. One
published 2.4 GHz reference is `-129 dBm @ SF7/BW406`. Do not claim an
official sensitivity for the current combination until both the program and
instrument use an officially supported 2.4 GHz bandwidth.

The following examples show generator settings with no path loss and with a
20 dB fixed attenuator. The 20 dB value only demonstrates the calculation;
use the measured total loss of the attenuator, cables, and adapters in an
actual test.

| Radio | Target DUT sweep | RF Level with 0 dB total loss | RF Level with 20 dB total loss | Expected Signal RSSI order when packets decode |
| --- | ---: | ---: | ---: | ---: |
| LR2021 | -130 dBm to -145 dBm | -130 dBm to -145 dBm | -110 dBm to -125 dBm | Approximately -130 dBm to -145 dBm |
| SX1262 | -125 dBm to -140 dBm | -125 dBm to -140 dBm | -105 dBm to -120 dBm | Approximately -125 dBm to -140 dBm |
| LR1121 | No official range for the current combination | Not an official pass/fail basis | Not an official pass/fail basis | Not an official pass/fail basis |

Set the instrument using:

```text
RF Level = Target DUT input power + Measured total path loss
```

For example, with 20 dB total path loss and an LR2021 target DUT input of
`-141.5 dBm`, set the instrument to `-121.5 dBm`. If the instrument already
applies external-attenuation compensation and displays the corrected DUT
port level, do not add the same 20 dB again.

`Signal RSSI` will normally be of the same order as the calibrated DUT input
power, but it is affected by the radio estimator, noise, negative SNR, and
RF-path error. `Packet RSSI` can also differ noticeably from Signal RSSI.
Final sensitivity must be judged by calibrated DUT input power and
`PER <= 1%`. No valid RSSI is produced when the receiver cannot demodulate a
packet, so RSSI itself is not a sensitivity pass criterion.

## Procedure

1. Select `lora_rx_sensitivity` under
   `menuconfig -> Example Configuration`.
2. Set the test-instrument SF to match `kSpreadingFactor` and configure
   exactly 1000 packets.
3. Start the example and wait for the serial output to report that the
   receiver is ready.
4. Enable modulation and RF output on the test instrument, then transmit the
   exact configured packet count. The first observed packet event
   automatically starts statistics for the current power point.
5. The program finishes automatically after observing 1000 packet events or
   after the RF packet stream remains idle for 10 seconds.
6. Record the final PER and reduce the instrument output power. The next RF
   packet stream automatically starts statistics for the next power point.
7. Press BOOT once at any time to discard the current statistics, restart the
   receiver, and wait for a new 1000-packet test stream. Restart the test
   instrument sequence after pressing the button.

Receiver sensitivity is the lowest calibrated DUT input power that still
satisfies `PER <= 1%`. Packet RSSI, Signal RSSI, and SNR are recorded only as
diagnostic information and are not pass criteria.

First verify near-zero PER at a level 10 to 15 dB above the datasheet typical
sensitivity. Reduce power in 1 dB steps, then use 0.5 dB steps near the 1% PER
boundary.

## Notes

- The test instrument packet count must exactly match the program's expected
  packet count.
- If a power point produces no receive event at all, the receiver cannot
  independently determine that the instrument has completed transmission.
  Record that power point as 100% PER.
- Ensure no other transmitter nearby uses the same frequency, sync word, and
  LoRa parameters.
- Do not enable unrelated high-load functions while running this test.
- Datasheet sensitivity values are typical IC values. Results at the complete
  RF port also include losses from switches, filters, matching networks, and
  the PCB.
