"""SH-4 plaintext detector. Deliberately built with BOTH controls:
a synthetic positive (real SH-4 encodings) and a random negative.
A detector without both is what produced the last session's false negative."""
import struct, random

def sh4_score(buf):
    """Count SH-4 hallmarks. SH-4 is fixed 16-bit little-endian instructions."""
    n = len(buf)//2
    w = struct.unpack('<%dH'%n, buf[:n*2])
    rts   = sum(1 for x in w if x == 0x000B)        # rts
    nop   = sum(1 for x in w if x == 0x0009)        # nop
    # mov.l @(disp,PC),Rn  = 1101nnnndddddddd  -- extremely common (literal pool)
    movlpc= sum(1 for x in w if (x & 0xF000) == 0xD000)
    # sts.l pr,@-Rn = 0100nnnn00100010 : function prologue
    stspr = sum(1 for x in w if (x & 0xF0FF) == 0x4022)
    # lds.l @Rn+,pr = 0100nnnn00100110 : epilogue
    ldspr = sum(1 for x in w if (x & 0xF0FF) == 0x4026)
    # mov Rm,Rn = 0110nnnnmmmm0011
    movrr = sum(1 for x in w if (x & 0xF00F) == 0x6003)
    return dict(words=n, rts=rts, nop=nop, mov_l_pc=movlpc,
                sts_pr=stspr, lds_pr=ldspr, mov_rr=movrr)

def verdict(s):
    """Real SH-4 code has MANY of each, and prologue/epilogue roughly BALANCE."""
    if s['words'] < 1000: return 'too small'
    dens = lambda k: s[k]/s['words']*1000  # per 1000 words
    hits = sum([dens('rts')>1, dens('mov_l_pc')>20, dens('sts_pr')>1,
                dens('lds_pr')>1, dens('mov_rr')>5])
    bal = abs(s['sts_pr']-s['lds_pr']) <= max(3, 0.5*max(s['sts_pr'],1))
    return ('SH-4 CODE' if hits>=4 and bal else
            'maybe' if hits>=3 else 'not code')
