import sys

def check_results(results_file):
    with open(results_file, "r") as f:
        passed = 0
        failed = 0
        total = 0
        # the first word on each line is the name of the simulation,
        # last one is whether it passed or not
        for line in f.readlines():
            words = line.rstrip().split(" ")
            sim_name = words[0]
            result = words[-1]

            if result == "FAILED":
                print(sim_name, result, u'\u274c')
                failed += 1
            else:
                # print(sim_name, result, u'\u2713')
                passed += 1
            
            total += 1
        print(f"Passed: {passed} Failed: {failed}")
        print(f"Total simulations run: {total}")

def main():
    if len(sys.argv) < 2:
        print("Usage: check_sim_results.py results_file")
    
    results_file = sys.argv[1]
    check_results(results_file)

main()