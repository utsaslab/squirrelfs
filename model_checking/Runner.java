/* Alloy Analyzer 4 -- Copyright (c) 2006-2009, Felix Chang
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files
 * (the "Software"), to deal in the Software without restriction, including without limitation the rights to use, copy, modify,
 * merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
 * OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE
 * LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF
 * OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

import edu.mit.csail.sdg.alloy4.A4Reporter;
import edu.mit.csail.sdg.alloy4.Err;
import edu.mit.csail.sdg.ast.Command;
import edu.mit.csail.sdg.ast.Module;
import edu.mit.csail.sdg.parser.CompUtil;
import edu.mit.csail.sdg.translator.A4Options;
import edu.mit.csail.sdg.translator.A4Solution;
import edu.mit.csail.sdg.translator.TranslateAlloyToKodkod;
import edu.mit.csail.sdg.alloy4viz.VizGUI;

import java.util.ArrayList;
import java.util.HashSet;
import java.util.Arrays;
import java.sql.Timestamp;
import java.text.SimpleDateFormat;
import java.io.File;
import java.io.FileWriter;
import java.io.FileReader;
import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Paths;
import java.util.concurrent.CopyOnWriteArrayList;

// TODO: would be useful to show which of the check_fs predicates fails if you run 
// check_fs and it fails. We might need to put them in a separate file to make it 
// easy to determine which predicates are used in check_fs and which are unrelated.
// Not sure how to actually accomplish that - add an evaluator and figure out how 
// to use it to evaluate just the last state in the counterexample?

public final class Runner {

    public static void main(String[] args) throws Err {
        A4Reporter rep = new A4Reporter() {
            @Override public void resultCNF(String filename) {
                System.out.println(filename);
            }
        };
        int num_threads = 2;

        Module model = CompUtil.parseEverything_fromFile(rep, null, "model2.als");
        A4Options options = new A4Options();

        options.solver = A4Options.SatSolver.Glucose41JNI;
        options.skolemDepth = 1;
        options.inferPartialInstance = false;
        options.decompose_mode = 0;

        int args_processed = 0;
        Boolean decompose_mode_set = false;
        HashSet<String> to_run = null;
        Boolean run_checks = true;
        Boolean run_sims = true;
        // process arguments
        if (args.length == 0) {
            usage(1);
        }
        try {
            // for (args_processed = 0; args_processed < args.length; args_processed++) {
            while (args_processed < args.length) {
                if (args[args_processed].charAt(0) == '-') {
                    char flag = args[args_processed].charAt(1);

                    if ((flag == 'y' || flag == 'p') && decompose_mode_set) {
                        System.out.println("-p and -h are exclusive flags");
                        System.exit(1);
                    }
                    if (flag == 'h') {
                        help();
                    } else if (flag == 'c') {
                        run_sims = false;
                        System.out.println("Skipping all simulations");
                    } else if (flag == 's') {
                        run_checks = false;
                        System.out.println("Skipping all assertions");
                    } else if (flag == 'y') {
                        options.decompose_mode = 1; // hybrid
                        decompose_mode_set = true;
                        System.out.println("Decompose mode: hybrid");
                    } else if (flag == 'p') {
                        options.decompose_mode = 2; // parallel
                        decompose_mode_set = true;
                        System.out.println("Decompose mode: parallel");
                    } else if (flag == 'n') {
                        options.solver = A4Options.SatSolver.CNF;
                    } else {
                        usage(1);
                    }

                    args_processed++;
                } else {
                    // process the number of threads
                    num_threads = Integer.parseInt(args[args_processed]);
                    args_processed++;
                    // then the command list, if one is specified
                    if (args_processed < args.length) {
                        to_run = new HashSet<String>(Arrays.asList(args[args_processed].split(",")));
                        args_processed++;
                    }
                    break;
                }
            }

        } catch (NumberFormatException e) {
            usage(1);
        }

        System.out.println(num_threads + " threads");
        if (!decompose_mode_set) {
            System.out.println("Decompose mode: batch");
        } else {
            options.decompose_threads = num_threads;
        }

        ArrayList<Command> commands = new ArrayList<Command>(model.getAllCommands());

        // Timestamp timestamp = new Timestamp(System.currentTimeMillis());
        // String str_timestamp = new SimpleDateFormat("yyyyMMddHHmmss").format(timestamp);
        String output_file = "output/model_sim_results";
        File f = new File(output_file);
        try {
            System.out.println("creating file " + output_file);
            // f.delete();
            f.createNewFile();
        } catch (IOException e) {
            System.out.println("An error occurred " + e);
            e.printStackTrace();
            return;
        }

        ArrayList<Command> commands_to_run_temp = new ArrayList<Command>();
        for (Command c : commands) {
            if (to_run == null || (to_run != null && to_run.contains(c.label))) {
                if ((c.check && run_checks) || (!c.check && run_sims)) {
                    commands_to_run_temp.add(c);
                } else {
                    System.out.println("skipping " + c.label);
                }
            }
            if (to_run != null) {
                to_run.remove(c.label);
            }
        }
        if (to_run != null && to_run.size() > 0) {
            System.out.println("Error: The following assertions to check are unknown: " + to_run);
            usage(1);
        }

        // TODO: use a more efficient concurrent list implementation
        CopyOnWriteArrayList<Command> commands_to_run = new CopyOnWriteArrayList<Command>(commands_to_run_temp);

        long startTime = System.nanoTime();
        if (!decompose_mode_set) {
            ArrayList<Thread> threads = new ArrayList<Thread>();
            for (int i = 0; i < num_threads; i++) {
                Worker worker = new Worker(commands_to_run, model, options, output_file);
                Thread thread = new Thread(worker);
                threads.add(thread);
                thread.start();
            }
            for (Thread t : threads) {
                try {
                    t.join();
                } catch (Exception e) {
                    System.out.println("failed joining thread " + e);
                }
            }
        } else {
            // TODO: it seems silly to make a new thread for running commands but that is
            // the easiest
            // way to do it in the current setup
            Worker worker = new Worker(commands_to_run, model, options, output_file);
            Thread thread = new Thread(worker);
            thread.start();
            try {
                thread.join();
            } catch (Exception e) {
                System.out.println("failed joining thread " + e);
            }
        }
        double time = (System.nanoTime() - startTime) / 1e6;
        System.out.println("Total runtime: " + time + "ms");
    }

    public static void print_usage() {
        System.out.println("Usage: Runner.java [-c | -s] [-p | -y] <number of threads> [test0,test1,...,testn]");
    }

    public static void usage(int exit_code) {
        print_usage();
        System.exit(exit_code);
    }

    public static void help() {
        print_usage();
        String[] args = {
                "-h", "-c", "-s", "-p", "-y", "-n"
        };
        String[] msg = {
                // -h
                "Display this help and exit",

                // -c
                String.format("%4s",
                        "Only run check/assertion commands. Simulation/'run' commands will be ignored."),

                // -s
                String.format("%4s",
                        "Only run simulation/'run' commands. Check/assertion commands will be ignored."),

                // -p
                String.format("%4s%n   %4s%n   %4s%n   %4s",
                        "Specify parallel decomposition in Alloy. If this flag is selected,",
                        "the number of threads argument specifies the number of decomposition",
                        "threads and commands are run sequentially. If neither -p nor -y is",
                        "selected, batch decomposition is used and commands are run in parallel"),

                // -y
                String.format("%4s%n   %4s%n   %4s%n   %4s",
                        "Specify hybrid decomposition in Alloy. If this flag is selected,",
                        "the number of threads argument specifies the number of decomposition",
                        "threads and commands are run sequentially. If neither -p nor -y is",
                        "selected, batch decomposition is used and commands are run in parallel"),
                // -n
                String.format("%4s%n   %4s",
                        "Create CNF files (stored in /tmp). When this is enabled, the runner",
                        "does not solve any commands."),
                    };
        System.out.println("Options:");
        for (int i = 0; i < msg.length; i++) {
            System.out.printf("%s %3s\n", args[i], msg[i]);
        }
        System.exit(0);
    }

}

class Worker implements Runnable {
    CopyOnWriteArrayList<Command> commands_to_run;
    Module model;
    A4Options options;
    String output_file;

    String current_command_label = null;
    private int startStep = -1, seenStep = -1, primaryVars = 0, clauses = 0, totalVars = 0, startCount = 0;

    A4Reporter rep = new A4Reporter() {
        @Override
        public void solve(int step, int primaryVars, int totalVars, int clauses) {
            if (startStep < 0) {
                startStep = step;
            }
            if (startStep == step) {
                startCount++;
            }
            seenStep = Math.max(seenStep, step);
            System.out.println(current_command_label + ": solve, " + startStep + ".." + seenStep + " steps.");
            System.out.flush();
        }

        @Override
        public void translate(String solver, int bitwidth, int maxseq, int mintrace, int maxtrace, int skolemDepth, int symmetry, String strat) {
            System.out.println(current_command_label + ": translate, Solver=" + solver + (maxtrace < 1 ? "" : " Steps=" + mintrace + ".." + maxtrace) + " Bitwidth=" + bitwidth + " MaxSeq=" + maxseq + (skolemDepth == 0 ? "" : " SkolemDepth=" + skolemDepth) + " Symmetry=" + (symmetry > 0 ? ("" + symmetry) : "OFF") + " Mode=" + strat);
            System.out.flush();
        }
    };

    public Worker(
            CopyOnWriteArrayList<Command> to_run,
            Module model,
            A4Options options,
            String output_file) {
        this.commands_to_run = to_run;
        this.model = model;
        this.options = options;
        this.output_file = output_file;
    }

    public void run() {
        while (true) {
            Command command;

            synchronized (commands_to_run) {
                if (commands_to_run.isEmpty()) {
                    break;
                }
                command = commands_to_run.remove(0);
            }
            current_command_label = command.label;
            try {
                run_test(command);
            } catch (Exception e) { 
                // if the function fails due to threading bug in Kodkod, put the command 
                // back on the queue to try again later.
                System.out.println("PUTTING COMMAND BACK ON QUEUE");
                synchronized (commands_to_run) {
                    commands_to_run.add(command);
                }
            }
        }
    }

    public void run_test(Command command) {
        // output_file) {
        System.out.println("Running " + command.label + "...");
        long startTime = System.nanoTime();
        if (options.solver == A4Options.SatSolver.CNF) {
            A4Solution ans = TranslateAlloyToKodkod.execute_command(rep, model.getAllReachableSigs(), command, options);
            return;
        }
        

        A4Solution ans = TranslateAlloyToKodkod.execute_command(rep, model.getAllReachableSigs(), command, options);

        double time = (System.nanoTime() - startTime) / 1e6;

        boolean sat = ans.satisfiable();
        // satisfiable check => counterexample found
        // unsatisfiable check => may be valid
        // satisfiable run => instance found
        // unsatisfiable run => may be inconsistent
        boolean passed;
        boolean valid;
        if (command.check) {
            if (sat) {
                valid = false;
                System.out.print(command.label + " is invalid (" + time + "ms)");
                if (command.label.contains("should_fail")) {
                    System.out.println("PASSED");
                    passed = true;
                } else {
                    System.out.println("FAILED");
                    passed = false;
                }
                ans.writeXML("output/counterexamples/" + command.label + ".xml", model.getAllFunc());
            } else {
                valid = true;
                System.out.print(command.label + " may be valid (" + time + "ms)");
                if (command.label.contains("should_fail")) {
                    System.out.println("FAILED");
                    passed = false;
                } else {
                    System.out.println("PASSED");
                    passed = true;
                }
            }

        } else {
            if (sat) {
                valid = true;
                System.out.print(command.label + " is consistent (" + time + "ms)");
                if (command.label.contains("should_fail")) {
                    System.out.println(" FAILED");
                    passed = false;
                } else {
                    System.out.println(" PASSED");
                    passed = true;
                }
                ans.writeXML("output/instances/" + command.label + ".xml", model.getAllFunc());
            } else {
                valid = false;
                System.out.print(command.label + " may be inconsistent (" + time + "ms)");
                if (command.label.contains("should_fail")) {
                    System.out.println(" PASSED");
                    passed = true;
                } else {
                    System.out.println(" FAILED");
                    passed = false;
                }
            }
        }

        try {
            FileWriter fw = new FileWriter(output_file, true);
            if (command.check) {
                if (!valid) {
                    fw.write(command.label + " is invalid (" + time + "ms)");
                    if (passed) {
                        fw.write(" PASSED\n");
                    } else {
                        fw.write(" FAILED\n");
                    }
                } else {
                    fw.write(command.label + " may be valid (" + time + "ms)");
                    if (passed) {
                        fw.write(" PASSED\n");
                    } else {
                        fw.write(" FAILED\n");
                    }
                }
            } else {
                if (valid) {
                    fw.write(command.label + " is consistent (" + time + "ms)");
                    if (passed) {
                        fw.write(" PASSED\n");
                    } else {
                        fw.write(" FAILED\n");
                    }
                } else {
                    fw.write(command.label + " may be inconsistent (" + time + "ms)");
                    if (passed) {
                        fw.write(" PASSED\n");
                    } else {
                        fw.write(" FAILED\n");
                    }
                }
            }
            fw.close();
        } catch (IOException e) {
            System.out.println("An error occurred " + e);
            e.printStackTrace();
        }
    }
}
