# BC-250 Register Offset Validation Audit

This document tracks the validation of MMIO offsets for the BC-250 (Cyan Skillfish) hardware bring-up. 

**Rule:** No offset is considered `VALIDATED` until a matching value is found in the upstream Linux `amdgpu` driver or Mesa source for the specific hardware revision.

## 1. GFX / Command Processor (CP)
| Register | Candidate Index | Offset (Idx*4) | Linux/Mesa Ref | Status | Notes |
| :--- | :---: | :---: | :---: | :---: | :--- |
| **GC_BASE** | - | **0x1260** | `cyan_skillfish_ip_offset.h` | `VALIDATED` | Segment 0 Base Address |
| CP_RB0_RPTR | 0x0F60 | 0x3D80 | TBD | `RISK` | Offset is relative to GC_BASE (0x1260) |
| CP_RB0_BASE | 0x1DE0 | 0x7B80 | TBD | `RISK` | Offset is relative to GC_BASE (0x1260) |
| CP_RB0_CNTL | 0x1DE1 | 0x7B84 | TBD | `RISK` | Offset is relative to GC_BASE (0x1260) |
| CP_RB0_WPTR | 0x1DF4 | 0x7BDE | TBD | `RISK` | Offset is relative to GC_BASE (0x1260) |

## 2. SDMA (System DMA)
| Register | Candidate Index | Offset (Idx*4) | Linux/Mesa Ref | Status | Notes |
| :--- | :---: | :---: | :---: | :---: | :--- |
| SDMA0_RB_CNTL | 0x012E | 0x04B8 | TBD | `HYPOTHESIS` | SDMA5 candidate |
| SDMA0_RB_BASE | 0x012F | 0x04BC | TBD | `HYPOTHESIS` | SDMA5 candidate |
| SDMA1_RB_CNTL | 0x0188 | 0x0620 | TBD | `HYPOTHESIS` | SDMA5 candidate |

## 3. PSP (Platform Security Processor)
| Register | Index/Offset | Linux/Mesa Ref | Status | Notes |
| :--- | :---: | :---: | :---: | :--- |
| C2PMSG_64 | TBD | TBD | `HYPOTHESIS` | Status/Ready register |
| C2PMSG_67 | TBD | TBD | `HYPOTHESIS` | Ring Write Pointer |
| C2PMSG_69/70| TBD | TBD | `HYPOTHESIS` | Ring Physical Address |

---
*Last Updated: 2026-10-06*
