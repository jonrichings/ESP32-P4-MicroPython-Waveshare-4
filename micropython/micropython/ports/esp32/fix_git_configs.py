import os

root_dir = r'C:/esp/v5.5.3/esp-idf/.git/modules'

def fix_config(path):
    with open(path, 'r') as f:
        lines = f.readlines()
    
    changed = False
    new_lines = []
    for line in lines:
        if line.strip().startswith('worktree = '):
            if '\\' in line:
                line = line.replace('\\', '/')
                changed = True
        new_lines.append(line)
    
    if changed:
        print(f"Fixing {path}")
        with open(path, 'w') as f:
            f.writelines(new_lines)

for root, dirs, files in os.walk(root_dir):
    if 'config' in files:
        fix_config(os.path.join(root, 'config'))
