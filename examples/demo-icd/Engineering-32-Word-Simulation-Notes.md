# 32-word engineering simulation

Choose Device Setup → Example simulation, then explicitly start capture. The generated message is RT 3, SA 12, receive, word count 32. All data are synthetic and the example profile is not a verified hardware ICD.

For this demo only, words 1–6 are IEEE 754 binary32, words 10–21 are IEEE 754 binary64, most-significant 16-bit word first. Integer rows use 16-bit signed or unsigned values with identity scale and offset. Words 28–30 toggle for observable bit plots. Reserved words 31–32 are zero. These assumptions do not define any physical device.

Use the four plot selectors to choose one field or bit for each panel. Bits 4–19 refer to the MIL-STD-1553 word data positions: bit 4 is the most significant data bit and bit 19 the least significant data bit. Select Expand plot or click a plotted point; Back to four plots restores the panels. Time is synthetic relative time, not UTC.
