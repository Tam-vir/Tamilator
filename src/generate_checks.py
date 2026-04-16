#!/usr/bin/env python3
import json
import sys
import os
import re

def generate_signal_checks(constraints_file='constraints.json', output_file='signal_checks.h'):
    """Generate C++ signal checks for signals in constraints.json"""
    
    if not os.path.exists(constraints_file):
        print(f"Warning: {constraints_file} not found", file=sys.stderr)
        with open(output_file, 'w') as f:
            f.write("// No constraints.json found\n\n")
            f.write("#pragma once\n\n")
            f.write("#include <iostream>\n")
            f.write("#include <unordered_map>\n")
            f.write("#include <string>\n\n")
            f.write("using Sig = CData;\n\n")
            f.write("void add_available_signals(std::unordered_map<std::string, Sig*>& signal_db, Vtop* top) {\n")
            f.write('    std::cout << "\\n=== No constraints.json ===\\n" << std::endl;\n')
            f.write("}\n")
        return
    
    with open(constraints_file, 'r') as f:
        constraints = json.load(f)
    
    # Collect all unique Verilog signals from constraints
    signals = set()
    
    # Regular signals (inputs and outputs)
    for sig in constraints.get("inputs", {}).values():
        signals.add(sig)
    
    for sig in constraints.get("outputs", {}).values():
        signals.add(sig)
    
    # Bar graph signals - add the base signal name (without bit range)
    for display_name, sig in constraints.get("bargraphs", {}).items():
        # Remove bit range to get base name
        base_name = re.sub(r'\[.*\]', '', sig)
        signals.add(base_name)
        print(f"Added bargraph signal: {base_name} from {sig}")
    
    # Dipswitch signals - add the base signal name (without bit range)
    for display_name, sig in constraints.get("dipswitches", {}).items():
        # Remove bit range to get base name
        base_name = re.sub(r'\[.*\]', '', sig)
        signals.add(base_name)
        print(f"Added dipswitch signal: {base_name} from {sig}")
    
    # Clock signal
    if "clock" in constraints:
        clk_signal = constraints["clock"]
        signals.add(clk_signal)
        print(f"Added clock signal: {clk_signal}")
    
    if not signals:
        print("No signals found in constraints.json", file=sys.stderr)
        with open(output_file, 'w') as f:
            f.write("// No signals in constraints.json\n\n")
            f.write("#pragma once\n\n")
            f.write("#include <iostream>\n")
            f.write("#include <unordered_map>\n")
            f.write("#include <string>\n\n")
            f.write("using Sig = CData;\n\n")
            f.write("void add_available_signals(std::unordered_map<std::string, Sig*>& signal_db, Vtop* top) {\n")
            f.write('    std::cout << "\\n=== No signals in constraints ===\\n" << std::endl;\n')
            f.write("}\n")
        return
    
    with open(output_file, 'w') as f:
        f.write("// Auto-generated from constraints.json\n")
        f.write("// DO NOT EDIT - Regenerate with: python3 generate_checks.py\n\n")
        f.write("#pragma once\n\n")
        f.write("#include <type_traits>\n")
        f.write("#include <iostream>\n")
        f.write("#include <unordered_map>\n")
        f.write("#include <string>\n\n")
        f.write("using Sig = CData;\n\n")
        
        # Generate has_* templates for each signal
        for sig in sorted(signals):
            # Sanitize signal name for template
            safe_sig = sig.replace('[', '_').replace(']', '_').replace('.', '_')
            f.write(f'template<typename T, typename = void>\n')
            f.write(f'struct has_{safe_sig} : std::false_type {{}};\n\n')
            f.write(f'template<typename T>\n')
            f.write(f'struct has_{safe_sig}<T, std::void_t<decltype(std::declval<T>().{sig})>> : std::true_type {{}};\n\n')
        
        f.write("void add_available_signals(std::unordered_map<std::string, Sig*>& signal_db, Vtop* top) {\n")
        f.write('    std::cout << "\\n=== Detecting signals from constraints ===\\n" << std::endl;\n\n')
        
        # Generate conditional addition for each signal
        for sig in sorted(signals):
            safe_sig = sig.replace('[', '_').replace(']', '_').replace('.', '_')
            f.write(f'    if constexpr (has_{safe_sig}<decltype(*top)>::value) {{\n')
            f.write(f'        signal_db["{sig}"] = &top->{sig};\n')
            f.write(f'        std::cout << "  ✓ Found: {sig}" << std::endl;\n')
            f.write(f'    }} else {{\n')
            f.write(f'        std::cout << "  ✗ Missing: {sig}" << std::endl;\n')
            f.write(f'    }}\n\n')
        
        f.write("}\n")
    
    print(f"✅ Generated checks for {len(signals)} signals in {output_file}")
    print(f"   Signals: {', '.join(sorted(signals))}")

if __name__ == "__main__":
    generate_signal_checks()