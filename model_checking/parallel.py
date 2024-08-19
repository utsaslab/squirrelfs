

OPS = ["Write", "Create", "Mkdir", "Ftruncate", "Unlink", "Rename", "Rmdir"]

def prelude() -> str:
    s = "open defs\nopen transitions\nopen model2\nopen util/integer\n\n"

    with open("aux.als", "r") as f:
        for line in f.readlines():
            s += line
        s += "\n\n"

    return s

def generate_exhaustiveness_check() -> str:
    s = "check ops_are_exhaustive {\n"
    s += "  all op: Operation |\n"
    for op in OPS:
        s += f"    op in {op} or " + "\n"
    s = s[:-4] + "\n"
    s += "} for 1 Volatile, exactly 10 PMObj, exactly 1 Root, exactly 1 NoInode, exactly 1 NoDentry, 1 Operation, 1..1 steps" + "\n\n"
    return s

def generate_permutations() -> str:
    permutations = []
    for op1 in OPS:
        for op2 in OPS:
            s = f"check check_fs_{op1}_{op2} {{\n"
            s += f"  ((first & Operation) in {op1} and (first & Operation).next in {op2}) =>\n"
            s += "    check_fs_pred\n"
            s += "} for 1 Volatile, exactly 10 PMObj, exactly 1 Root, exactly 1 NoInode, exactly 1 NoDentry, 2 Operation, 30..30 steps\n\n"
            permutations += [s]
    return "".join(permutations)


with open("model_auto.als", "w") as f:
    f.write(prelude())
    f.write(generate_exhaustiveness_check())
    f.write(generate_permutations())