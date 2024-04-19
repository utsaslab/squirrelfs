import prettytable 
import sys 
import csv
import numpy

fs = ["ext4", "nova", "winefs", "squirrelfs"]

def parse_data(input_file):
    results_dict = {f: [] for f in fs}
    with open(input_file, "r") as f:
        csv_reader = csv.reader(f, delimiter=",")
        for row in csv_reader:
            if len(row) > 0 and row[0] not in fs:
                results_dict["ext4"].append(float(row[0]))
                results_dict["nova"].append(float(row[1]))
                results_dict["winefs"].append(float(row[2]))
                results_dict["squirrelfs"].append(float(row[3]))
    avg_results = { t: round(numpy.average(numpy.array(results_dict[t])), 2) for t in fs }
    return avg_results

def build_table(results_dict, output_file):
    table = prettytable.PrettyTable(["System", "Compile time (s)"])
    table.add_row(["Ext4", results_dict["ext4"]])
    table.add_row(["NOVA", results_dict["nova"]])
    table.add_row(["WineFS", results_dict["winefs"]])
    table.add_row(["SquirrelFS", results_dict["squirrelfs"]])

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