# Temperature Calibration

The heater uses a compensated VBE-related measurement from the 2SC5200 transistor as a temperature-dependent signal.

## Current calibration parameters

```cpp
const float TEMP_OFFSET_C  = 13.0f;
const float TEMP_CAL_REF_C = 26.0f;
const float TEMP_SLOPE     = 1.065f;
```

## Formula

```text
tempRawCal =
(0.775 − uBaseReal) × 430 + TEMP_OFFSET_C
```

Then:

```text
tempNow =
TEMP_CAL_REF_C +
(tempRawCal − TEMP_CAL_REF_C) × TEMP_SLOPE
```

## Meaning

- `TEMP_OFFSET_C` corrects the low-temperature offset for the specific transistor and analog measurement chain.
- `TEMP_CAL_REF_C` is the temperature around which the slope correction pivots.
- `TEMP_SLOPE` corrects the response at higher temperatures.

## Recommended procedure

1. Let the transistor reach thermal equilibrium with the room.
2. Measure the ambient temperature with a reliable thermometer.
3. Adjust the offset until the display matches the reference.
4. Use a reliable high-temperature reference to verify the upper part of the range.
5. Adjust the slope carefully.
6. Recheck both the low and high points after every change.

Different transistors and measurement setups may require different calibration values.
