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
            f.write("#include <string>\n")
            f.write("#include <cstdint>\n\n")
            f.write("// SignalPtr is defined in main cpp file\n")
            f.write("void add_available_signals(std::unordered_map<std::string, SignalPtr>& signal_db, Vtop* top) {\n")
            f.write("}\n")
        return
    
    with open(constraints_file, 'r') as f:
        constraints = json.load(f)
    
    signals = set()
    
    for sig in constraints.get("inputs", {}).values():
        base_name = re.sub(r'\[.*\]', '', sig)
        signals.add(base_name)
    
    for sig in constraints.get("outputs", {}).values():
        base_name = re.sub(r'\[.*\]', '', sig)
        signals.add(base_name)
    
    for display_name, sig in constraints.get("bargraphs", {}).items():
        base_name = re.sub(r'\[.*\]', '', sig)
        signals.add(base_name)
    
    for display_name, sig in constraints.get("dipswitches", {}).items():
        base_name = re.sub(r'\[.*\]', '', sig)
        signals.add(base_name)
    
    if "memory" in constraints:
        mem = constraints["memory"]
        for sig_name in ["address_bus", "data_bus", "write_enable", "read_enable"]:
            if sig_name in mem:
                sig = mem[sig_name]
                base_name = re.sub(r'\[.*\]', '', sig)
                signals.add(base_name)
    
    if "clock" in constraints:
        clk_signal = constraints["clock"]
        base_name = re.sub(r'\[.*\]', '', clk_signal)
        signals.add(base_name)
    
    if not signals:
        print("No signals found in constraints.json", file=sys.stderr)
        with open(output_file, 'w') as f:
            f.write("// No signals in constraints.json\n\n")
            f.write("#pragma once\n\n")
            f.write("#include <iostream>\n")
            f.write("#include <unordered_map>\n")
            f.write("#include <string>\n")
            f.write("#include <cstdint>\n\n")
            f.write("// SignalPtr is defined in main cpp file\n")
            f.write("void add_available_signals(std::unordered_map<std::string, SignalPtr>& signal_db, Vtop* top) {\n")
            f.write("}\n")
        return
    
    with open(output_file, 'w') as f:
        f.write("// Auto-generated from constraints.json\n")
        f.write("// DO NOT EDIT - Regenerate with: python3 generate_checks.py\n\n")
        f.write("#pragma once\n\n")
        f.write("#include <type_traits>\n")
        f.write("#include <iostream>\n")
        f.write("#include <unordered_map>\n")
        f.write("#include <string>\n")
        f.write("#include <cstdint>\n\n")
        f.write("// Note: SignalPtr is defined in the main cpp file\n\n")
        
        for sig in sorted(signals):
            safe_sig = sig.replace('[', '_').replace(']', '_').replace('.', '_')
            f.write(f'template<typename T, typename = void>\n')
            f.write(f'struct has_{safe_sig} : std::false_type {{}};\n\n')
            f.write(f'template<typename T>\n')
            f.write(f'struct has_{safe_sig}<T, std::void_t<decltype(std::declval<T>().{sig})>> : std::true_type {{}};\n\n')
        
        f.write("void add_available_signals(std::unordered_map<std::string, SignalPtr>& signal_db, Vtop* top) {\n")
        
        for sig in sorted(signals):
            safe_sig = sig.replace('[', '_').replace(']', '_').replace('.', '_')
            f.write(f'    if constexpr (has_{safe_sig}<decltype(*top)>::value) {{\n')
            f.write(f'        signal_db["{sig}"] = SignalPtr(&top->{sig});\n')
            f.write(f'    }}\n\n')
        
        f.write("}\n")

if __name__ == "__main__":
    generate_signal_checks()