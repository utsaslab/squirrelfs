import sys 
import os
import re

tests = ["empty", "full", "init"]
fs = ["ext4", "nova", "squirrelfs", "winefs"]

def read_results(results_dir, iterations):
    results_dict = {f: {t: [] for t in tests} for f in fs}
    pattern = "MemAvailable:\s+(\d+) kB"

    for f in results_dict:
        for t in results_dict[f]:
            path = os.path.join(results_dir, f, "memory_usage", t)
            for i in range(1, iterations+1):
                run_path = os.path.join(path, "Run" + str(i))
                with open(run_path, "r") as file:
                    before_line = file.readline()
                    after_line = file.readline()

                    before_match = int(re.search(pattern, before_line).group(1))
                    after_match = int(re.search(pattern, after_line).group(1))
                    
                    kb_used = before_match - after_match
                    # print(kb_used)
                    results_dict[f][t].append(kb_used)

    avg_results = {f: {t: sum(results_dict[f][t])/len(results_dict[f][t]) / 1024 for t in tests} for f in fs}
    print(avg_results)


def main():
    if len(sys.argv) < 3:
        print("Usage: python3 calculate_memory_usage.py results_dir iterations")
        return

    results_dir = sys.argv[1]
    iterations = int(sys.argv[2])

    read_results(results_dir, iterations)

main()