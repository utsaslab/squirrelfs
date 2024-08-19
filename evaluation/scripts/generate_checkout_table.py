import prettytable 
import sys 
import csv
import numpy

fs = ["ext4", "nova", "winefs", "squirrelfs"]
versions = ["v3.0", "v4.0", "v5.0", "v6.0"]

def parse_data(input_file):
    results_dict = {v: {f: [] for f in fs} for v in versions}
    with open(input_file, "r") as f:
        csv_reader = csv.reader(f, delimiter=",")
        current_version = ""
        for row in csv_reader:
            if len(row) > 0 and row[0] in versions:
                current_version = row[0]
            elif len(row) > 0:
                results_dict[current_version]["ext4"].append(float(row[1]))
                results_dict[current_version]["nova"].append(float(row[2]))
                results_dict[current_version]["winefs"].append(float(row[3]))
                results_dict[current_version]["squirrelfs"].append(float(row[4]))
    avg_results = {v: {t: round(numpy.average(numpy.array(results_dict[v][t])), 2) for t in fs} for v in versions }
    return avg_results

def build_table(results_dict, output_file):
    table = prettytable.PrettyTable(["Linux version", "System", "Time to checkout (s)"])
    for v in versions:
        for f in fs:
            row = [v]
            row.append(f)
            row.append(results_dict[v][f])
            if f == "squirrelfs":
                divider = True
            else:
                divider = False
            table.add_row(row, divider=divider)

    table_string = table.get_string()
    with open(output_file, "w") as f:
        f.write(table_string)

def main():
    if len(sys.argv) < 2:
        print("Usage: generate_compilation_table.py input_file output_file")
        exit(1)

    input_file = sys.argv[1]
    output_file = sys.argv[2]

    results_dict = parse_data(input_file)
    build_table(results_dict, output_file)


main()