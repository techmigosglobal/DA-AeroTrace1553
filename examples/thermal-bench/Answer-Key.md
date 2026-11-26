# Thermal Bench assistant answer key

These synthetic fixtures use subsystem Thermal Bench, RT 7, subaddress 12,
BC to RT, channel 1, Bus A, and five payload words.

| Question | Expected evidence |
| --- | --- |
| How is Temperature converted? | Revision B page 2. Unsigned word 1, scale 0.5 and offset -10 degC. |
| What does 0064 mean for Temperature? | Revision B page 2. 100 raw counts gives 40 degC. Revision A gives 30 degC. |
| Which raw word represents -2 Signed correction? | Revision B page 2. Word 2, signed two's-complement, FFFE. |
| What is the byte order and value of 4148 0000 for Reference pressure? | Revision B page 2. Float32, MSW first, 12.5 kPa. |
| What is the nonlinear index for 000A and 0014? | Revision B page 2. Unsigned word 5, x times x times 0.01. Results 1.0 and 4.0. |
| What is the Quasargate Unobtainium threshold? | Insufficient evidence. No inferred setting. |

The raster scan contains page images only. Its citations should identify
page 2. Source-derived conversion settings remain subject to operator review.
