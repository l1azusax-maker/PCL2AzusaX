using System;
using System.Linq;
using Mono.Cecil;
using Mono.Cecil.Cil;

class Dump
{
    static void Main(string[] args)
    {
        string path = args[0];
        string filter = args.Length > 1 ? args[1] : null;
        var asm = AssemblyDefinition.ReadAssembly(path);
        Console.WriteLine("assembly: " + asm.Name + " runtime=" + asm.MainModule.Runtime);

        foreach (var t in asm.MainModule.Types)
        {
            foreach (var m in t.Methods)
            {
                if (filter != null && m.Name.IndexOf(filter, StringComparison.OrdinalIgnoreCase) < 0) continue;
                Console.WriteLine();
                Console.WriteLine("=== " + t.FullName + " :: " + m.Name + "  (" + m.Parameters.Count + " params)");
                if (!m.HasBody) { Console.WriteLine("  <no body>"); continue; }
                foreach (var v in m.Body.Variables)
                    Console.WriteLine("  .local " + v.Index + " : " + v.VariableType);
                foreach (var i in m.Body.Instructions)
                    Console.WriteLine("  IL_" + i.Offset.ToString("X4") + "  " + i.OpCode + "  " + Format(i));
            }
        }
    }

    static string Format(Instruction i)
    {
        if (i.Operand == null) return "";
        if (i.Operand is string) return "\"" + (string)i.Operand + "\"";
        if (i.Operand is Instruction) return "IL_" + ((Instruction)i.Operand).Offset.ToString("X4");
        var arr = i.Operand as Instruction[];
        if (arr != null) return string.Join(",", arr.Select(x => "IL_" + x.Offset.ToString("X4")).ToArray());
        return i.Operand.ToString();
    }
}
