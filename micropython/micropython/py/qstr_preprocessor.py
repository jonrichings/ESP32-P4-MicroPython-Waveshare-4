import sys
import subprocess
import re

def process(input_files, compiler_cmd, output_file):
    combined = ''
    for filepath in input_files:
        with open(filepath, 'r') as f:
            combined += f.read() + '\n'
            
    # sed "s/^Q(.*)/"&"/"
    # Replaces lines starting with Q(something) into "Q(something)"
    processed = re.sub(r'^(Q\(.*\))', r'"\1"', combined, flags=re.MULTILINE)
    
    # Run through the C preprocessor
    try:
        compiler_proc = subprocess.Popen(
            compiler_cmd, 
            stdin=subprocess.PIPE, 
            stdout=subprocess.PIPE, 
            stderr=subprocess.PIPE,
            text=True
        )
        stdout, stderr = compiler_proc.communicate(input=processed)
        if compiler_proc.returncode != 0:
            sys.stderr.write(f"C Preprocessor failed:\n{stderr}")
            sys.exit(1)
    except Exception as e:
        sys.stderr.write(f"Failed to run compiler: {e}\n")
        sys.exit(1)

    # sed "s/^\"\(Q(.*)\)\"/\1/"
    # Reverses the quotes we added earlier
    final_output = re.sub(r'^"(Q\(.*\))"', r'\1', stdout, flags=re.MULTILINE)
    
    with open(output_file, 'w') as f:
        f.write(final_output)

if __name__ == "__main__":
    if len(sys.argv) < 4:
        sys.exit("Usage: qstr_preprocessor.py <out_file> <compiler> [compiler_args...] -- <in_files...>")
        
    out_file = sys.argv[1]
    
    # Split arguments into compiler command and input files using "--" separator
    try:
        sep_idx = sys.argv.index('--')
        raw_compiler_cmd = sys.argv[2:sep_idx]
        input_files = sys.argv[sep_idx+1:]
    except ValueError:
        sys.exit("Missing '--' separator for input files")
        
    # GCC natively supports @response_files, so we just pass raw_compiler_cmd directly
    process(input_files, raw_compiler_cmd, out_file)
