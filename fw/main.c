/* RA8M2 / Cortex-M85 feature-identification register dump.
 * Prints every ID register raw, then each bit-field decoded, then a
 * consolidated feature summary. Output over SEGGER RTT channel 0.
 * Send any byte on RTT down-channel 0 to re-run the dump. */
#include <stdint.h>
#include "rtt.h"

extern volatile uint32_t g_fault_skip, g_fault_hit;

/* ------------------------------------------------------------------ */
/* tiny output helpers                                                */
/* ------------------------------------------------------------------ */
static void out(const char *s) { rtt_puts(s); }
static void outc(char c) { rtt_putc(c); }
static void out_hex(uint32_t v, int digits)
{
    static const char h[] = "0123456789ABCDEF";
    for (int i = digits - 1; i >= 0; i--) outc(h[(v >> (i * 4)) & 0xF]);
}
static void out_u(uint32_t v)
{
    char b[12]; int n = 0;
    do { b[n++] = '0' + v % 10; v /= 10; } while (v);
    while (n) outc(b[--n]);
}
static void out_bin(uint32_t v, int bits)
{
    for (int i = bits - 1; i >= 0; i--) outc((v >> i) & 1 ? '1' : '0');
}
static void pad(const char *s, int w)
{
    int n = 0; while (s[n]) n++;
    out(s); while (n++ < w) outc(' ');
}

/* ------------------------------------------------------------------ */
/* fault-tolerant 32-bit read                                         */
/* ------------------------------------------------------------------ */
static uint32_t rd32(uint32_t addr, int *ok)
{
    uint32_t v = 0;
    g_fault_hit = 0;
    g_fault_skip = 1;
    __asm volatile("dsb\n isb" ::: "memory");
    __asm volatile("ldr %0, [%1]" : "=r"(v) : "r"(addr) : "memory");
    __asm volatile("dsb\n isb" ::: "memory");
    g_fault_skip = 0;
    if (ok) *ok = !g_fault_hit;
    return g_fault_hit ? 0 : v;
}

static inline uint32_t bits(uint32_t v, int hi, int lo)
{
    return (v >> lo) & ((hi - lo == 31) ? 0xFFFFFFFFu : ((1u << (hi - lo + 1)) - 1u));
}

/* print "  NAME[hi:lo] = 0xV (0bV)  meaning" */
static void field(const char *name, uint32_t v, int hi, int lo, const char *meaning)
{
    uint32_t f = bits(v, hi, lo);
    out("    "); pad(name, 16);
    out("["); out_u(hi); if (hi != lo) { out(":"); out_u(lo); } out("]");
    if (hi == lo) out("  "); if (hi < 10) out(" "); if (lo < 10 && hi != lo) out(" ");
    out(" = 0x"); out_hex(f, (hi - lo) / 4 + 1);
    out(" (0b"); out_bin(f, hi - lo + 1); out(")");
    if (meaning) { out("  "); out(meaning); }
    out("\n");
}

/* ------------------------------------------------------------------ */
/* register decoders                                                  */
/* ------------------------------------------------------------------ */
static const char *partno_name(uint32_t p)
{
    switch (p) {
    case 0xC20: return "Cortex-M0";   case 0xC60: return "Cortex-M0+";
    case 0xC21: return "Cortex-M1";   case 0xC23: return "Cortex-M3";
    case 0xC24: return "Cortex-M4";   case 0xC27: return "Cortex-M7";
    case 0xD20: return "Cortex-M23";  case 0xD21: return "Cortex-M33";
    case 0xD22: return "Cortex-M55";  case 0xD23: return "Cortex-M85";
    case 0xD24: return "Cortex-M52";
    default:    return "unknown part";
    }
}

static void dec_cpuid(uint32_t v)
{
    field("Implementer", v, 31, 24, bits(v,31,24) == 0x41 ? "Arm Ltd" : "other implementer");
    field("Variant", v, 23, 20, "major revision (rN)");
    field("Architecture", v, 19, 16, bits(v,19,16) == 0xF ? "ARMv7-M or later: see ID_* registers" : "legacy encoding");
    field("PartNo", v, 15, 4, partno_name(bits(v,15,4)));
    field("Revision", v, 3, 0, "minor revision (pN)");
}

static void dec_pfr0(uint32_t v)
{
    uint32_t s1 = bits(v,7,4);
    field("State0 (A32)", v, 3, 0, bits(v,3,0) ? "A32 supported" : "A32 not supported (M-profile)");
    field("State1 (T32)", v, 7, 4, s1 == 3 ? "T32: 16- and 32-bit Thumb-2" : s1 == 1 ? "T32: 16-bit only" : "no Thumb (?)");
    field("State2", v, 11, 8, "Jazelle (RES0 on M)");
    field("State3", v, 15, 12, "ThumbEE (RES0 on M)");
    field("RAS", v, 31, 28, bits(v,31,28) ? "RAS extension implemented (v8.1-M)" : "RAS extension not implemented");
}

static void dec_pfr1(uint32_t v)
{
    uint32_t sec = bits(v,7,4), mpm = bits(v,11,8);
    field("Security", v, 7, 4,
          sec == 0 ? "Security Extension (TrustZone) not implemented" :
          sec == 1 ? "Security Extension (TrustZone) implemented" :
          sec == 3 ? "TrustZone + non-secure FP/MVE state handling (v8.1-M)" : "reserved");
    field("MProgMod", v, 11, 8, mpm == 2 ? "M-profile two-stack programmers' model" : "reserved");
}

static void dec_dfr0(uint32_t v)
{
    uint32_t pm = bits(v,27,24);
    { uint32_t d = bits(v,23,20);
      field("MProfDbg", v, 23, 20, d == 2 ? "M-profile debug, Armv8.1-M" : d == 1 ? "M-profile debug, Armv8-M" : "no M-profile debug"); }
    field("PerfMon", v, 27, 24, pm == 0 ? "0 (Cortex-M85 reports 0 even with PMU fitted; see PMU_TYPE)" : pm == 3 ? "PMUv3 (v8.1-M PMU extension)" : "PMU present (other version)");
    field("UDE", v, 31, 28, bits(v,31,28) ? "Unprivileged Debug Extension (v8.1-M)" : "no Unprivileged Debug Ext");
}

static void dec_mmfr0(uint32_t v)
{
    uint32_t pmsa = bits(v,7,4);
    field("VMSA", v, 3, 0, bits(v,3,0) ? "VMSA present (?)" : "no VMSA (M-profile)");
    field("PMSA", v, 7, 4, pmsa == 4 ? "PMSAv8 (v8-M MPU)" : pmsa == 3 ? "PMSAv7" : pmsa == 0 ? "no PMSA" : "other PMSA");
    field("OuterShr", v, 11, 8, "outer shareability");
    field("ShareLvl", v, 15, 12, bits(v,15,12) ? "two shareability levels" : "one shareability level");
    field("TCM", v, 19, 16, bits(v,19,16) ? "TCM support (impl. defined)" : "no TCM");
    field("AuxReg", v, 23, 20, bits(v,23,20) ? "ACTLR implemented" : "no ACTLR");
    field("FCSE", v, 27, 24, "FCSE (RES0 on M)");
    field("InnerShr", v, 31, 28, "inner shareability");
}

static void dec_mmfr2(uint32_t v)
{
    field("WFIStall", v, 27, 24, bits(v,27,24) ? "WFI stalling supported" : "no WFI stalling");
}

static void dec_mmfr3(uint32_t v)
{
    field("CMaintVA", v, 3, 0, "cache maintenance by VA");
    field("CMaintSW", v, 7, 4, bits(v,7,4) ? "cache maintenance by set/way" : "none");
    field("BPMaint", v, 11, 8, bits(v,11,8) ? "branch predictor maintenance" : "none");
    field("MaintBcst", v, 15, 12, "maintenance broadcast");
    field("CohWalk", v, 23, 20, "coherent walk");
    field("CMemSz", v, 27, 24, "cached memory size");
    field("Supersec", v, 31, 28, "supersections");
}

static void dec_isar0(uint32_t v)
{
    uint32_t dv = bits(v,27,24);
    field("Swap", v, 3, 0, "swap instrs (RES0 on M)");
    field("BitCount", v, 7, 4, bits(v,7,4) ? "CLZ" : "no CLZ");
    field("BitField", v, 11, 8, bits(v,11,8) ? "BFC/BFI/SBFX/UBFX" : "no bitfield instrs");
    field("CmpBranch", v, 15, 12, bits(v,15,12) ? "CBZ/CBNZ" : "no CBZ/CBNZ");
    field("Coproc", v, 19, 16, bits(v,19,16) ? "generic coprocessor instrs" : "no generic CDP/LDC/MCR..");
    field("Debug", v, 23, 20, bits(v,23,20) ? "BKPT" : "no BKPT");
    field("Divide", v, 27, 24, dv >= 1 ? "SDIV/UDIV in Thumb" : "no hardware divide");
}

static void dec_isar1(uint32_t v)
{
    field("Endian", v, 3, 0, bits(v,3,0) ? "SETEND" : "no SETEND");
    field("Except", v, 7, 4, "A32 exception instrs");
    field("Except_AR", v, 11, 8, "A/R exception instrs");
    field("Extend", v, 15, 12, bits(v,15,12) >= 2 ? "SXTB/SXTH/UXTB/UXTH + SXTAB.. UXTB16.." : bits(v,15,12) ? "SXTB/SXTH/UXTB/UXTH" : "no extend");
    field("IfThen", v, 19, 16, bits(v,19,16) ? "IT instructions" : "no IT");
    field("Immediate", v, 23, 20, bits(v,23,20) ? "MOVT/MOVW/long immediates" : "no long immediates");
    field("Interwork", v, 27, 24, bits(v,27,24) ? "BX/BLX interworking" : "none");
    field("Jazelle", v, 31, 28, "Jazelle (RES0 on M)");
}

static void dec_isar2(uint32_t v)
{
    field("LoadStore", v, 3, 0, bits(v,3,0) >= 2 ? "LDRD/STRD + LDREX/STREX variants" : bits(v,3,0) ? "LDRD/STRD" : "none");
    field("MemHint", v, 7, 4, bits(v,7,4) >= 3 ? "PLD/PLI/PLDW" : bits(v,7,4) ? "PLD" : "no hints");
    field("MultiAccessInt", v, 11, 8, "interruptible LDM/STM");
    field("Mult", v, 15, 12, bits(v,15,12) >= 2 ? "MUL/MLA/MLS" : bits(v,15,12) ? "MUL/MLA" : "MUL");
    field("MultS", v, 19, 16, bits(v,19,16) >= 3 ? "SMULL/SMLAL + SMLABB.. + SMLAD..(DSP)" : bits(v,19,16) ? "SMULL/SMLAL" : "none");
    field("MultU", v, 23, 20, bits(v,23,20) >= 2 ? "UMULL/UMLAL/UMAAL" : bits(v,23,20) ? "UMULL/UMLAL" : "none");
    field("PSR_AR", v, 27, 24, "A/R PSR instrs (RES0 on M)");
    field("Reversal", v, 31, 28, bits(v,31,28) >= 2 ? "REV/REV16/REVSH + RBIT" : bits(v,31,28) ? "REV/REV16/REVSH" : "none");
}

static void dec_isar3(uint32_t v)
{
    field("Saturate", v, 3, 0, bits(v,3,0) ? "SSAT/USAT (+Q flag)" : "no saturate");
    field("SIMD", v, 7, 4, bits(v,7,4) >= 3 ? "DSP extension: SIMD/PKHBT/QADD8.." : bits(v,7,4) ? "partial SIMD" : "no SIMD/DSP");
    field("SVC", v, 11, 8, bits(v,11,8) ? "SVC" : "no SVC");
    field("SynchPrim", v, 15, 12, bits(v,15,12) >= 2 ? "LDREX/STREX + B/H/D variants + CLREX" : bits(v,15,12) ? "LDREX/STREX" : "none");
    field("TabBranch", v, 19, 16, bits(v,19,16) ? "TBB/TBH" : "no table branch");
    field("T32Copy", v, 23, 20, bits(v,23,20) ? "Thumb MOV(register) low->low" : "none");
    field("TrueNOP", v, 27, 24, bits(v,27,24) ? "true NOP" : "none");
    field("T32EE", v, 31, 28, "ThumbEE (RES0 on M)");
}

static void dec_isar4(uint32_t v)
{
    field("Unpriv", v, 3, 0, bits(v,3,0) >= 2 ? "LDRBT/LDRHT/LDRSBT/LDRSHT/LDRT/STR*T" : bits(v,3,0) ? "LDRBT/LDRT/STRBT/STRT" : "none");
    field("WithShifts", v, 7, 4, bits(v,7,4) >= 3 ? "shifts of loads/stores + data-proc, regs & imm" : "limited shifts");
    field("Writeback", v, 11, 8, bits(v,11,8) ? "all writeback addressing modes" : "basic writeback");
    field("SMC", v, 15, 12, "SMC (RES0 on M)");
    field("Barrier", v, 19, 16, bits(v,19,16) ? "DMB/DSB/ISB instructions" : "CP15 barriers only");
    field("SynchPrim_frac", v, 23, 20, "synch primitives fraction");
    field("PSR_M", v, 27, 24, bits(v,27,24) ? "M-profile CPS/MRS/MSR" : "none");
    field("SWP_frac", v, 31, 28, "SWP fraction (RES0 on M)");
}

static void dec_isar5(uint32_t v)
{
    field("SEVL", v, 3, 0, bits(v,3,0) ? "SEVL" : "no SEVL");
    field("AES", v, 7, 4, "AES (RES0 on M)");
    field("SHA1", v, 11, 8, "SHA1 (RES0 on M)");
    field("SHA2", v, 15, 12, "SHA2 (RES0 on M)");
    field("CRC32", v, 19, 16, "CRC32 (RES0 on M)");
    field("PACBTI", v, 23, 20, bits(v,23,20) ? "Pointer Auth + Branch Target Id (v8.1-M PACBTI)" : "no PACBTI");
    field("RDM", v, 27, 24, "rounding double multiply (RES0 on M)");
    field("VCMA", v, 31, 28, "complex number instrs (RES0 on M)");
}

static const char *ctype_name(uint32_t c)
{
    switch (c) { case 0: return "no cache"; case 1: return "instruction cache only";
                 case 2: return "data cache only"; case 3: return "separate I and D caches";
                 case 4: return "unified cache"; default: return "reserved"; }
}

static void dec_clidr(uint32_t v)
{
    for (int l = 0; l < 7; l++) {
        char n[8] = "Ctype1"; n[5] = '1' + l;
        uint32_t c = bits(v, 3*l+2, 3*l);
        if (l == 0 || c) field(n, v, 3*l+2, 3*l, ctype_name(c));
    }
    field("LoUIS", v, 23, 21, "level of unification inner shareable");
    field("LoC", v, 26, 24, "level of coherence");
    field("LoUU", v, 29, 27, "level of unification uniprocessor");
    field("ICB", v, 31, 30, "inner cache boundary");
}

static void dec_ctr(uint32_t v)
{
    uint32_t imin = bits(v,3,0), dmin = bits(v,19,16);
    field("IminLine", v, 3, 0, "log2(words) smallest I-cache line");
    out("                       -> "); out_u(4u << imin); out(" bytes\n");
    field("L1Ip", v, 15, 14, bits(v,15,14) == 3 ? "PIPT" : bits(v,15,14) == 2 ? "VIPT" : "other");
    field("DminLine", v, 19, 16, "log2(words) smallest D-cache line");
    out("                       -> "); out_u(4u << dmin); out(" bytes\n");
    field("ERG", v, 23, 20, "exclusives reservation granule log2(words)");
    field("CWG", v, 27, 24, "cache writeback granule log2(words)");
    field("IDC", v, 28, 28, bits(v,28,28) ? "I-cache needs no D-cache clean for coherence" : "D-cache clean to PoU needed");
    field("DIC", v, 29, 29, bits(v,29,29) ? "no I-cache invalidate needed" : "I-cache invalidate needed");
    field("Format", v, 31, 29, bits(v,31,29) == 4 ? "ARMv7+ CTR format" : "legacy");
}

static void dec_ccsidr(uint32_t v, uint32_t *size_out)
{
    uint32_t ls = bits(v,2,0), ways = bits(v,12,3) + 1, sets = bits(v,27,13) + 1;
    uint32_t lb = 1u << (ls + 4);
    field("LineSize", v, 2, 0, "log2(words) - 2");
    out("                       -> "); out_u(lb); out(" bytes/line\n");
    field("Associativity", v, 12, 3, "ways - 1");
    field("NumSets", v, 27, 13, "sets - 1");
    field("WA", v, 28, 28, "write-allocate");
    field("RA", v, 29, 29, "read-allocate");
    field("WB", v, 30, 30, "write-back");
    field("WT", v, 31, 31, "write-through");
    uint32_t size = lb * ways * sets;
    out("                       -> "); out_u(ways); out(" ways x "); out_u(sets);
    out(" sets x "); out_u(lb); out(" B = "); out_u(size / 1024); out(" KiB\n");
    if (size_out) *size_out = size;
}

static void dec_mvfr0(uint32_t v)
{
    uint32_t sp = bits(v,7,4), dp = bits(v,11,8);
    field("SIMDReg", v, 3, 0, bits(v,3,0) == 2 ? "32 x 64-bit FP regs (D0-D31)" : bits(v,3,0) == 1 ? "16 x 64-bit FP regs (D0-D15)" : "no FP regs");
    field("FPSP", v, 7, 4, sp >= 2 ? "single precision, VFPv3+ (incl. VMOV imm)" : sp ? "single precision VFPv2" : "no single precision");
    field("FPDP", v, 11, 8, dp >= 2 ? "double precision, VFPv3+" : dp ? "double precision VFPv2" : "no double precision");
    field("FPTrap", v, 15, 12, bits(v,15,12) ? "FP exception trapping" : "no FP trapping");
    field("FPDivide", v, 19, 16, bits(v,19,16) ? "VDIV" : "no VDIV");
    field("FPSqrt", v, 23, 20, bits(v,23,20) ? "VSQRT" : "no VSQRT");
    field("FPShVec", v, 27, 24, bits(v,27,24) ? "short vectors" : "no short vectors");
    field("FPRound", v, 31, 28, bits(v,31,28) ? "all IEEE rounding modes" : "round-to-nearest only");
}

static void dec_mvfr1(uint32_t v)
{
    uint32_t mve = bits(v,11,8);
    field("FPFtZ", v, 3, 0, bits(v,3,0) ? "flush-to-zero mode" : "no FtZ");
    field("FPDNaN", v, 7, 4, bits(v,7,4) ? "default NaN mode" : "no DN");
    field("MVE", v, 11, 8, mve == 2 ? "MVE integer + floating-point (Helium, MVE-F)" :
                             mve == 1 ? "MVE integer only (MVE-I)" : "no MVE (Helium)");
    field("FP16", v, 23, 20, bits(v,23,20) ? "half-precision arithmetic (FP16)" : "no FP16 arithmetic");
    field("FPHP", v, 27, 24, bits(v,27,24) >= 2 ? "half<->single & double conversions" : bits(v,27,24) ? "half<->single conversions" : "no half-precision");
    field("FMAC", v, 31, 28, bits(v,31,28) ? "fused multiply-accumulate (VFMA/VFMS..)" : "no FMA");
}

static void dec_mvfr2(uint32_t v)
{
    field("SIMDMisc", v, 3, 0, "SIMD misc (RES0 on M)");
    field("FPMisc", v, 7, 4, bits(v,7,4) >= 4 ? "VSEL/VMAXNM/VMINNM/VCVTA..+VRINT*" : bits(v,7,4) ? "some FP misc instrs" : "none");
}

static void dec_mputype(uint32_t v)
{
    field("SEPARATE", v, 0, 0, bits(v,0,0) ? "separate I/D regions" : "unified regions");
    field("DREGION", v, 15, 8, "number of MPU regions");
    field("IREGION", v, 23, 16, "instruction regions (RES0 on M)");
}

static void dec_sautype(uint32_t v)
{
    field("SREGION", v, 7, 0, "number of SAU regions");
}

static void dec_ictr(uint32_t v)
{
    uint32_t n = bits(v,3,0);
    field("INTLINESNUM", v, 3, 0, "NVIC lines = 32*(N+1)");
    out("                       -> up to "); out_u(32 * (n + 1)); out(" external interrupts\n");
}

static void dec_dwt(uint32_t v)
{
    field("NUMCOMP", v, 31, 28, "number of DWT comparators");
    field("NOTRCPKT", v, 27, 27, bits(v,27,27) ? "no trace sampling/exception trace" : "trace sampling & exception trace supported");
    field("NOEXTTRIG", v, 26, 26, bits(v,26,26) ? "no external match signals" : "external match signals (CMPMATCH)");
    field("NOCYCCNT", v, 25, 25, bits(v,25,25) ? "no cycle counter" : "cycle counter (CYCCNT)");
    field("NOPRFCNT", v, 24, 24, bits(v,24,24) ? "no profiling counters" : "profiling counters supported");
}

static void dec_fpctrl(uint32_t v)
{
    uint32_t nc = (bits(v,14,12) << 4) | bits(v,7,4);
    field("REV", v, 31, 28, bits(v,31,28) == 1 ? "FPB v2 (breakpoints anywhere)" : "FPB v1 (flash patch)");
    field("NUM_CODE[6:4]", v, 14, 12, "");
    field("NUM_LIT", v, 11, 8, "literal comparators");
    field("NUM_CODE[3:0]", v, 7, 4, "");
    out("                       -> "); out_u(nc); out(" code (breakpoint) comparators, ");
    out_u(bits(v,11,8)); out(" literal comparators\n");
    field("ENABLE", v, 0, 0, bits(v,0,0) ? "FPB enabled" : "FPB disabled");
}

static void dec_pmutype(uint32_t v)
{
    field("N", v, 7, 0, "number of event counters (excl. cycle counter)");
    field("SIZE", v, 13, 8, "counter size - 1 bits");
    field("CC", v, 14, 14, bits(v,14,14) ? "dedicated cycle counter" : "no cycle counter");
    field("TRO", v, 17, 17, bits(v,17,17) ? "trace on overflow supported" : "no trace-on-overflow");
}

static void dec_fpccr(uint32_t v)
{
    field("LSPACT", v, 0, 0, "lazy state preservation active");
    field("USER", v, 1, 1, "");
    field("S", v, 2, 2, "FP context secure");
    field("THREAD", v, 3, 3, "");
    field("MMRDY", v, 5, 5, "");
    field("BFRDY", v, 6, 6, "");
    field("SFRDY", v, 7, 7, "");
    field("TS", v, 26, 26, bits(v,26,26) ? "FP regs treated as secure" : "FP regs not secure");
    field("CLRONRET", v, 28, 28, "");
    field("LSPENS", v, 29, 29, "");
    field("LSPEN", v, 30, 30, bits(v,30,30) ? "lazy FP state preservation enabled" : "lazy stacking disabled");
    field("ASPEN", v, 31, 31, bits(v,31,31) ? "auto FP state preservation enabled" : "auto FP stacking disabled");
}

static void dec_dhcsr(uint32_t v)
{
    field("C_DEBUGEN", v, 0, 0, bits(v,0,0) ? "halting debug ENABLED (debugger attached)" : "halting debug disabled");
    field("S_HALT", v, 17, 17, "");
    field("S_SLEEP", v, 18, 18, "");
    field("S_LOCKUP", v, 19, 19, "");
    field("S_SDE", v, 20, 20, bits(v,20,20) ? "secure debug enabled" : "secure debug disabled");
    field("S_NSUIDE", v, 21, 21, "");
    field("S_SUIDE", v, 22, 22, "");
    field("S_FPD", v, 23, 23, "");
    field("S_RESET_ST", v, 25, 25, "");
    field("S_RESTART_ST", v, 26, 26, "");
}

static void dec_dauth(uint32_t v)
{
    field("SID", v, 1, 0, "secure invasive debug: b1x = enabled");
    field("SNID", v, 3, 2, "secure non-invasive debug");
    field("NSID", v, 5, 4, "non-secure invasive debug");
    field("NSNID", v, 7, 6, "non-secure non-invasive debug");
    field("SUID", v, 9, 8, "secure unprivileged invasive debug (v8.1-M)");
    field("NSUID", v, 11, 10, "non-secure unprivileged invasive debug");
}

static void dec_aircr(uint32_t v)
{
    field("ENDIANNESS", v, 15, 15, bits(v,15,15) ? "big-endian data" : "little-endian data");
    field("PRIS", v, 14, 14, "prioritize secure exceptions");
    field("BFHFNMINS", v, 13, 13, bits(v,13,13) ? "BusFault/HardFault/NMI target non-secure" : "BusFault/HardFault/NMI secure");
    field("SYSRESETREQS", v, 3, 3, "");
    field("PRIGROUP", v, 10, 8, "priority grouping");
}

/* ------------------------------------------------------------------ */
/* dump driver                                                        */
/* ------------------------------------------------------------------ */
typedef void (*dec_fn)(uint32_t);
typedef struct { const char *name; uint32_t addr; const char *desc; dec_fn dec; } reg_t;

static uint32_t show(const reg_t *r, int *okp)
{
    int ok; uint32_t v = rd32(r->addr, &ok);
    out("  "); pad(r->name, 12); out("@0x"); out_hex(r->addr, 8); out(" = ");
    if (!ok) { out("<fault: not accessible from this state>"); }
    else     { out("0x"); out_hex(v, 8); }
    out("   "); out(r->desc); out("\n");
    if (ok && r->dec) r->dec(v);
    if (okp) *okp = ok;
    return ok ? v : 0;
}

/* Secure state test: TT on our own flash address. In Secure state with
 * the SAU disabled (reset default) the S bit (22) is 1; from Non-secure
 * state TT always returns S=0. */
static int in_secure_state(void)
{
    uint32_t r, a = 0x02000000u;
    __asm volatile("tt %0, %1" : "=r"(r) : "r"(a));
    return (r >> 22) & 1;
}

static void dump(void)
{
    static const reg_t core[] = {
        { "CPUID",    0xE000ED00, "CPU identification",                     dec_cpuid },
        { "ID_PFR0",  0xE000ED40, "processor feature 0",                    dec_pfr0 },
        { "ID_PFR1",  0xE000ED44, "processor feature 1",                    dec_pfr1 },
        { "ID_DFR0",  0xE000ED48, "debug feature 0",                        dec_dfr0 },
        { "ID_AFR0",  0xE000ED4C, "auxiliary feature 0 (impl. defined)",    0 },
        { "ID_MMFR0", 0xE000ED50, "memory model feature 0",                 dec_mmfr0 },
        { "ID_MMFR1", 0xE000ED54, "memory model feature 1",                 0 },
        { "ID_MMFR2", 0xE000ED58, "memory model feature 2",                 dec_mmfr2 },
        { "ID_MMFR3", 0xE000ED5C, "memory model feature 3",                 dec_mmfr3 },
        { "ID_ISAR0", 0xE000ED60, "instruction set attribute 0",            dec_isar0 },
        { "ID_ISAR1", 0xE000ED64, "instruction set attribute 1",            dec_isar1 },
        { "ID_ISAR2", 0xE000ED68, "instruction set attribute 2",            dec_isar2 },
        { "ID_ISAR3", 0xE000ED6C, "instruction set attribute 3",            dec_isar3 },
        { "ID_ISAR4", 0xE000ED70, "instruction set attribute 4",            dec_isar4 },
        { "ID_ISAR5", 0xE000ED74, "instruction set attribute 5",            dec_isar5 },
        { "CLIDR",    0xE000ED78, "cache level ID",                         dec_clidr },
        { "CTR",      0xE000ED7C, "cache type",                             dec_ctr },
        { "MVFR0",    0xE000EF40, "media/VFP feature 0",                    dec_mvfr0 },
        { "MVFR1",    0xE000EF44, "media/VFP feature 1",                    dec_mvfr1 },
        { "MVFR2",    0xE000EF48, "media/VFP feature 2",                    dec_mvfr2 },
    };
    static const reg_t sys[] = {
        { "ICTR",     0xE000E004, "interrupt controller type",              dec_ictr },
        { "ACTLR",    0xE000E008, "auxiliary control (impl. defined)",      0 },
        { "CPPWR",    0xE000E00C, "coprocessor power control",              0 },
        { "MPU_TYPE", 0xE000ED90, "MPU type (secure MPU)",                  dec_mputype },
        { "MPU_TYPE_NS",0xE002ED90,"MPU type (non-secure MPU via NS alias)",dec_mputype },
        { "SAU_TYPE", 0xE000EDD4, "SAU type (secure only)",                 dec_sautype },
        { "SAU_CTRL", 0xE000EDD0, "SAU control",                            0 },
        { "AIRCR",    0xE000ED0C, "app interrupt/reset control",            dec_aircr },
        { "CPACR",    0xE000ED88, "coprocessor access control",             0 },
        { "NSACR",    0xE000ED8C, "non-secure access control",              0 },
        { "FPCCR",    0xE000EF34, "FP context control",                     dec_fpccr },
        { "CCR",      0xE000ED14, "configuration & control",                0 },
    };
    static const reg_t dbg[] = {
        { "DHCSR",    0xE000EDF0, "debug halting control/status",           dec_dhcsr },
        { "DEMCR",    0xE000EDFC, "debug exception & monitor control",      0 },
        { "DAUTHSTAT",0xE000EDF8, "debug authentication status",            dec_dauth },
        { "DWT_CTRL", 0xE0001000, "DWT control",                            dec_dwt },
        { "DWT_DEVARCH",0xE0001FBC,"DWT CoreSight DEVARCH",                 0 },
        { "FP_CTRL",  0xE0002000, "flash patch & breakpoint control",       dec_fpctrl },
        { "PMU_TYPE", 0xE0003E00, "PMU type (v8.1-M PMU)",                  dec_pmutype },
        { "PMU_CEID0",0xE0003E20, "PMU common event ID 0 (events 0-31)",    0 },
        { "PMU_CEID1",0xE0003E24, "PMU common event ID 1 (events 32-63)",   0 },
        { "PMU_CEID2",0xE0003E28, "PMU common event ID 2",                  0 },
        { "PMU_CEID3",0xE0003E2C, "PMU common event ID 3",                  0 },
        { "ITM_TCR",  0xE0000E80, "ITM trace control",                      0 },
        { "ITM_DEVARCH",0xE0000FBC,"ITM CoreSight DEVARCH",                 0 },
        { "ETM_DEVARCH",0xE0041FBC,"ETM CoreSight DEVARCH",                 0 },
        { "ETM_TRCIDR0",0xE00411E0,"ETM ID register 0",                     0 },
        { "ETM_TRCIDR1",0xE00411E4,"ETM ID register 1",                     0 },
        { "ROM_ENTRY0",0xE00FF000,"CoreSight ROM table entry 0",            0 },
        { "ROM_ENTRY1",0xE00FF004,"CoreSight ROM table entry 1",            0 },
        { "ROM_ENTRY2",0xE00FF008,"CoreSight ROM table entry 2",            0 },
        { "ROM_ENTRY3",0xE00FF00C,"CoreSight ROM table entry 3",            0 },
        { "ROM_PIDR0",0xE00FFFE0, "ROM table PIDR0",                        0 },
        { "ROM_PIDR1",0xE00FFFE4, "ROM table PIDR1",                        0 },
        { "ROM_PIDR2",0xE00FFFE8, "ROM table PIDR2",                        0 },
        { "ROM_PIDR4",0xE00FFFD0, "ROM table PIDR4",                        0 },
    };
    static const reg_t vendor[] = {
        { "OFS0",     0x02C9F040, "Renesas option-setting memory word 0 (ConfigROM)", 0 },
        { "OFS1",     0x02C9F044, "Renesas option-setting memory word 1",   0 },
        { "OFS2",     0x02C9F048, "Renesas option-setting memory word 2",   0 },
        { "OFS3",     0x02C9F04C, "Renesas option-setting memory word 3",   0 },
        { "OTP_CFG0", 0x02E07400, "Renesas OTP config word 0",              0 },
        { "OTP_CFG1", 0x02E07404, "Renesas OTP config word 1",              0 },
    };

    uint32_t cpuid, pfr0, pfr1, dfr0, mmfr0, isar3, isar5, clidr, ctr, mvfr0, mvfr1;
    uint32_t mpu = 0, mpu_ns = 0, sau = 0, ictr = 0, dwt = 0, fpb = 0, pmu = 0, dhcsr = 0;
    int ok_mpu, ok_mpu_ns, ok_sau, ok_pmu, ok_etm, ok_itm;
    uint32_t etm_devarch = 0, itm_devarch = 0;
    int secure = in_secure_state();

    out("\n================================================================\n");
    out(" Renesas RA8M2 / Arm Cortex-M85 feature identification dump\n");
    out(" built with ATfE clang, output via J-Link RTT\n");
    out("================================================================\n");
    out(" running in "); out(secure ? "SECURE" : "NON-SECURE"); out(" state, ");
    out("privileged, MSP; VTOR=0x"); out_hex(*(volatile uint32_t *)0xE000ED08, 8); out("\n\n");

    out("--- Core ID registers (SCB) ---\n");
    cpuid = show(&core[0], 0);
    pfr0  = show(&core[1], 0);
    pfr1  = show(&core[2], 0);
    dfr0  = show(&core[3], 0);
    show(&core[4], 0);
    mmfr0 = show(&core[5], 0);
    show(&core[6], 0); show(&core[7], 0); show(&core[8], 0);
    show(&core[9], 0); show(&core[10], 0); show(&core[11], 0);
    isar3 = show(&core[12], 0);
    show(&core[13], 0);
    isar5 = show(&core[14], 0);
    clidr = show(&core[15], 0);
    ctr   = show(&core[16], 0);

    /* caches: walk CLIDR levels */
    uint32_t icache = 0, dcache = 0;
    for (int l = 0; l < 7; l++) {
        uint32_t c = bits(clidr, 3*l+2, 3*l);
        if (!c) break;
        for (int ind = 0; ind < 2; ind++) {            /* 0 = D/unified, 1 = I */
            if (ind == 1 && (c == 2 || c == 4)) continue;
            if (ind == 0 && c == 1) continue;
            *(volatile uint32_t *)0xE000ED84 = (uint32_t)(l << 1) | (uint32_t)ind;
            __asm volatile("dsb\n isb");
            uint32_t cc = *(volatile uint32_t *)0xE000ED80;
            out("  CCSIDR L"); out_u(l + 1); out(ind ? "-I " : (c == 4 ? "-U " : "-D "));
            out("   @0xE000ED80 = 0x"); out_hex(cc, 8); out("   cache size ID (CSSELR=");
            out_u((l << 1) | ind); out(")\n");
            uint32_t sz = 0; dec_ccsidr(cc, &sz);
            if (l == 0) { if (ind) icache = sz; else dcache = sz; }
        }
    }
    *(volatile uint32_t *)0xE000ED84 = 0;

    mvfr0 = show(&core[17], 0);
    mvfr1 = show(&core[18], 0);
    show(&core[19], 0);

    out("\n--- System / security / MPU ---\n");
    ictr = show(&sys[0], 0);
    show(&sys[1], 0); show(&sys[2], 0);
    mpu    = show(&sys[3], &ok_mpu);
    mpu_ns = show(&sys[4], &ok_mpu_ns);
    sau    = show(&sys[5], &ok_sau);
    show(&sys[6], 0); show(&sys[7], 0); show(&sys[8], 0); show(&sys[9], 0);
    show(&sys[10], 0); show(&sys[11], 0);

    out("\n--- Debug / trace components ---\n");
    dhcsr = show(&dbg[0], 0);
    show(&dbg[1], 0); show(&dbg[2], 0);
    dwt = show(&dbg[3], 0);
    show(&dbg[4], 0);
    fpb = show(&dbg[5], 0);
    pmu = show(&dbg[6], &ok_pmu);
    for (int i = 7; i < 11; i++) show(&dbg[i], 0);
    show(&dbg[11], 0);
    itm_devarch = show(&dbg[12], &ok_itm);
    etm_devarch = show(&dbg[13], &ok_etm);
    for (int i = 14; i < (int)(sizeof dbg / sizeof dbg[0]); i++) show(&dbg[i], 0);

    out("\n--- Renesas vendor configuration (raw) ---\n");
    for (unsigned i = 0; i < sizeof vendor / sizeof vendor[0]; i++) show(&vendor[i], 0);

    /* ---------------- feature summary ---------------- */
    out("\n================ FEATURE SUMMARY ================\n");
    out(" core            : "); out(partno_name(bits(cpuid,15,4)));
    out(" r"); out_u(bits(cpuid,23,20)); out("p"); out_u(bits(cpuid,3,0));
    out(bits(cpuid,31,24) == 0x41 ? " (Arm)" : ""); out("\n");
    out(" architecture    : "); out(bits(cpuid,19,16) == 0xF ? "Armv8-M Mainline" : "?");
    if (bits(pfr0,31,28) || bits(mvfr1,11,8) || bits(isar5,23,20) || bits(dfr0,27,24)) out(" + v8.1-M extensions");
    out("\n");
    out(" security state  : "); out(secure ? "Secure" : "Non-secure"); out("\n");
    out(" TrustZone-M     : "); out(bits(pfr1,7,4) ? "yes" : "no");
    if (ok_sau) { out(" ("); out_u(bits(sau,7,0)); out(" SAU regions)"); } out("\n");
    out(" MPU (S)         : "); if (ok_mpu) { out_u(bits(mpu,15,8)); out(" regions"); } else out("n/a"); out("\n");
    out(" MPU (NS)        : "); if (ok_mpu_ns) { out_u(bits(mpu_ns,15,8)); out(" regions"); } else out("n/a"); out("\n");
    out(" FPU             : ");
    if (!bits(mvfr0,7,4)) out("none");
    else { out(bits(mvfr0,11,8) ? "single + double precision" : "single precision only");
           out(", "); out(bits(mvfr0,3,0) == 2 ? "32" : "16"); out(" D-regs");
           if (bits(mvfr1,31,28)) out(", FMA");
           if (bits(mvfr1,23,20)) out(", FP16"); }
    out("\n");
    out(" MVE (Helium)    : ");
    { uint32_t m = bits(mvfr1,11,8); out(m == 2 ? "integer + float" : m == 1 ? "integer only" : "no"); } out("\n");
    out(" DSP extension   : "); out(bits(isar3,7,4) >= 3 ? "yes" : "no"); out("\n");
    out(" PACBTI          : "); out(bits(isar5,23,20) ? "yes" : "no"); out("\n");
    out(" RAS extension   : "); out(bits(pfr0,31,28) ? "yes" : "no"); out("\n");
    out(" Low-overhead-branch/LOB: "); out((bits(pfr0,31,28) || bits(mvfr1,11,8)) ? "yes (v8.1-M Mainline mandatory)" : "unknown"); out("\n");
    out(" PMU             : ");
    if (ok_pmu && bits(pmu,7,0)) { out_u(bits(pmu,7,0)); out(" x "); out_u(bits(pmu,13,8) + 1);
        out("-bit event counters"); out(bits(pmu,14,14) ? " + cycle counter" : ""); out(" (per PMU_TYPE)"); }
    else out("no"); out("\n");
    out(" Unpriv debug ext: "); out(bits(dfr0,31,28) ? "yes" : "no"); out("\n");
    out(" I-cache         : "); if (icache) { out_u(icache / 1024); out(" KiB, line "); out_u(4u << bits(ctr,3,0)); out(" B"); } else out("none"); out("\n");
    out(" D-cache         : "); if (dcache) { out_u(dcache / 1024); out(" KiB, line "); out_u(4u << bits(ctr,19,16)); out(" B"); } else out("none"); out("\n");
    out(" TCM             : "); out(bits(mmfr0,19,16) ? "yes (ITCM @0x0, DTCM @0x20000000 per RA8M2 map)" : "no"); out("\n");
    out(" NVIC lines      : up to "); out_u(32 * (bits(ictr,3,0) + 1)); out("\n");
    out(" DWT             : "); out_u(bits(dwt,31,28)); out(" comparators");
    out(bits(dwt,25,25) ? ", no CYCCNT" : ", CYCCNT"); out(bits(dwt,24,24) ? "" : ", profiling counters"); out("\n");
    out(" FPB             : "); out_u((bits(fpb,14,12) << 4) | bits(fpb,7,4)); out(" breakpoints, ");
    out_u(bits(fpb,11,8)); out(" literal comparators\n");
    out(" ITM             : "); out(ok_itm && (itm_devarch >> 16) ? "present" : "absent/inaccessible"); out("\n");
    out(" ETM             : "); out(ok_etm && (etm_devarch >> 16) ? "present" : "absent/inaccessible"); out("\n");
    out(" debugger        : "); out(bits(dhcsr,0,0) ? "attached (C_DEBUGEN=1)" : "not attached"); out("\n");
    out(" endianness      : "); out(bits(*(volatile uint32_t *)0xE000ED0C,15,15) ? "big" : "little"); out("\n");
    out("=================================================\n");
    out(" send any character on RTT down-channel 0 to dump again\n\n");
}

int main(void)
{
    rtt_init();
    dump();
    for (;;) {
        if (rtt_getc() >= 0) dump();
        __asm volatile("nop");
    }
}
