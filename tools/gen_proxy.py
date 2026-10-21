import sys, struct, os

def u16(b,o): return struct.unpack_from("<H",b,o)[0]
def u32(b,o): return struct.unpack_from("<I",b,o)[0]

def read_exports(path):
    d=open(path,"rb").read()
    e=u32(d,0x3C); coff=e+4; nsect=u16(d,coff+2); optsz=u16(d,coff+16); opt=coff+20
    magic=u16(d,opt); plus = magic==0x20b
    ddoff=opt+(112 if plus else 96)
    exp_rva=u32(d,ddoff)
    secoff=opt+optsz; sects=[]
    for i in range(nsect):
        o=secoff+i*40
        sects.append((u32(d,o+12),u32(d,o+8),u32(d,o+20),u32(d,o+16)))
    def r2o(rva):
        for va,vsz,rp,rs in sects:
            if va<=rva<va+max(vsz,rs): return rp+(rva-va)
        return None
    eo=r2o(exp_rva)
    named=u32(d,eo+24); base=u32(d,eo+16)
    anames=r2o(u32(d,eo+32)); aords=r2o(u32(d,eo+36))
    out=[]
    for i in range(named):
        no=r2o(u32(d,anames+i*4))
        nm=d[no:d.index(b"\0",no)].decode("latin1")
        out.append((base+u16(d,aords+i*2), nm))
    return sorted(out)

DLL = "dwmapi"
src = r"C:\Windows\System32\dwmapi.dll"
funcs = read_exports(src)

outdir = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "pcl_unlock", "gen")
os.makedirs(outdir, exist_ok=True)

# ---- stubs.asm ----
asm = ["; AUTO-GENERATED - do not edit", "; x64 jump stubs: forwarding exports to real dwmapi", ""]
for o, n in funcs:
    asm.append("EXTERN pfn_%s:QWORD" % n)
asm.append("")
asm.append("option casemap:none")
asm.append(".code")
asm.append("")
asm.append("FailStub PROC")
asm.append("    xor eax, eax")
asm.append("    ret")
asm.append("FailStub ENDP")
asm.append("")
for o, n in funcs:
    asm.append("%s PROC" % n)
    asm.append("    jmp qword ptr [pfn_%s]" % n)
    asm.append("%s ENDP" % n)
    asm.append("")
asm.append("END")
open(os.path.join(outdir, "stubs.asm"), "w", encoding="ascii", newline="\r\n").write("\n".join(asm))

# ---- dwmapi.def ----
df = ["; AUTO-GENERATED", "LIBRARY dwmapi", "EXPORTS"]
for o, n in funcs:
    df.append("    %s @%d" % (n, o))
open(os.path.join(outdir, "proxy.def"), "w", encoding="ascii", newline="\r\n").write("\n".join(df) + "\n")

# ---- proxy_exports.inc ----
inc = ["/* AUTO-GENERATED */", "PVOID FailStub(void);", ""]
inc.append("/* forwarder slots (referenced from stubs.asm) */")
for o, n in funcs:
    inc.append('PVOID pfn_%s = (PVOID)FailStub;' % n)
inc.append("")
inc.append("typedef struct _EXPORT_ENTRY { const char* name; PVOID* slot; } EXPORT_ENTRY;")
inc.append("static const EXPORT_ENTRY g_exports[] = {")
for o, n in funcs:
    inc.append('    { "%s", &pfn_%s },' % (n, n))
inc.append("};")
open(os.path.join(outdir, "proxy_exports.inc"), "w", encoding="ascii", newline="\r\n").write("\n".join(inc))

# ---- manifest of names ----
open(os.path.join(outdir, "names.txt"), "w", encoding="ascii", newline="\r\n").write("\n".join(n for o, n in funcs) + "\n")
print("exports:", len(funcs))
