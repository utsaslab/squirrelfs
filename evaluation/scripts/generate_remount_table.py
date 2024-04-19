import prettytable 
import sys 
import csv
import numpy

def parse_data(input_file):
    with open(input_file, "r") as f:
        csv_reader = csv.reader(f, delimiter=",")
        current_test = ""
        results = {}
        for row in csv_reader:
            if row == []:
                continue 
            if row[0] != "":
                current_test = row[0]
            else:
                if current_test in results:
                    results[current_test].append(float(row[1]))
                else:
                    results[current_test] = [float(row[1])]
    avg_results = { t: round(numpy.average(numpy.array(results[t])), 2) for t in results.keys() }
    return avg_results

def build_table(results_dict, output_file):
    table = prettytable.PrettyTable(["", "System state", "Mount time (s)"])
    table.add_row(["Normal mount", "mkfs", results_dict["init"]])
    table.add_row(["Normal mount", "Empty", results_dict["empty"]])
    table.add_row(["Normal mount", "Full", results_dict["fill_device"]], divider=True)
    table.add_row(["Recovery mount", "Empty", results_dict["empty_recovery"]])
    table.add_row(["Recovery mount", "Full", results_dict["fill_device_recovery"]])

    table_string = table.get_string()
    with open(output_file, "w") as f:
        f.write(table_string)

def main():
    if len(sys.argv) < 2:
        print("Usage: generate_remount_table.py input_file output_file")
        exit(1)

    input_file = sys.argv[1]
    output_file = sys.argv[2]

    results_dict = parse_data(input_file)
    build_table(results_dict, output_file)


main()