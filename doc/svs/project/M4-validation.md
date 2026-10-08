# M4 real-format validation

Numeric limits remain 0.5 LibreSVIP tick / 0.5 cent. LOSS is not full pitch fidelity PASS.

| Format / alias | Real output | Real input | Original edited pitch |
| --- | --- | --- | --- |
| ace/ace | PASS | PASS | PASS |
| acep/acep | PASS | PASS | LOSS (full fidelity FAIL) |
| acep/acet | PASS | PASS | LOSS (full fidelity FAIL) |
| aisp/aisp | PASS | PASS | LOSS (full fidelity FAIL) |
| ass/ass | PASS | N/A | N/A (declared output kind) |
| ccs/ccs | PASS | PASS | LOSS (full fidelity FAIL) |
| ds/ds | PASS | PASS | LOSS (full fidelity FAIL) |
| dspx/dspx | PASS | PASS | PASS |
| dv/dv | PASS | PASS | PASS |
| dv/sk | PASS | PASS | PASS |
| json/json | PASS | PASS | PASS |
| lrc/lrc | PASS | N/A | N/A (declared output kind) |
| mid/mid | PASS | PASS | PASS |
| mid/midi | PASS | PASS | PASS |
| mtp/mtp | PASS | PASS | PASS |
| musicxml/musicxml | PASS | PASS | LOSS (full fidelity FAIL) |
| musicxml/xml | PASS | PASS | LOSS (full fidelity FAIL) |
| musicxml/mxl | PASS | PASS | LOSS (full fidelity FAIL) |
| nn/nn | PASS | PASS | LOSS (full fidelity FAIL) |
| ppsf/ppsf | PASS | PASS | PASS |
| ps_project/ps_project | PASS | PASS | LOSS (full fidelity FAIL) |
| s5p/s5p | PASS | PASS | PASS |
| srt/srt | PASS | N/A | N/A (declared output kind) |
| svg/svg | PASS | N/A | N/A (declared output kind) |
| svip/svip | PASS | PASS | PASS |
| svip3/svip3 | PASS | PASS | PASS |
| svp/svp | PASS | PASS | PASS |
| tlp/tlp | PASS | PASS | PASS |
| tlpx/tlpx | PASS | PASS | PASS |
| tsmsln/tsmsln | PASS | PASS | LOSS (full fidelity FAIL) |
| tssln/tssln | PASS | PASS | LOSS (full fidelity FAIL) |
| ufdata/ufdata | PASS | PASS | PASS |
| ust/ust | PASS | PASS | LOSS (full fidelity FAIL) |
| ustx/ustx | PASS | PASS | PASS |
| vfp/vfp | PASS | PASS | LOSS (full fidelity FAIL) |
| vog/vog | PASS | PASS | LOSS (full fidelity FAIL) |
| vpr/vpr | PASS | PASS | PASS |
| vshp | N/A | PASS | native fixture assertions PASS |
| vspx/vspx | PASS | PASS | LOSS (full fidelity FAIL) |
| vsq/vsq | PASS | PASS | PASS |
| vsqx/vsqx | PASS | PASS | PASS |
| vvproj/vvproj | PASS | PASS | LOSS (full fidelity FAIL) |
| vxf/vxf | PASS | PASS | LOSS (full fidelity FAIL) |
| xvsq/xvsq | PASS | PASS | PASS |
| y77/y77 | PASS | PASS | LOSS (full fidelity FAIL) |

Detailed measured errors, declared fields and unexplained failures: `M4/fidelity-validation.json`.
