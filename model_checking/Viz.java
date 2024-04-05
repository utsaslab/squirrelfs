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

import org.alloytools.alloy.core.AlloyCore;
import edu.mit.csail.sdg.alloy4.Err;
import edu.mit.csail.sdg.alloy4.ErrorType;
import edu.mit.csail.sdg.alloy4.ErrorFatal;
import edu.mit.csail.sdg.alloy4.A4Reporter;
import edu.mit.csail.sdg.alloy4.Computer;
import edu.mit.csail.sdg.alloy4.Version;
import edu.mit.csail.sdg.alloy4.XMLNode;
import edu.mit.csail.sdg.alloy4viz.VizGUI;
import edu.mit.csail.sdg.ast.Expr;
import edu.mit.csail.sdg.ast.ExprConstant;
import edu.mit.csail.sdg.ast.ExprVar;
import edu.mit.csail.sdg.ast.Module;
import edu.mit.csail.sdg.ast.Sig;
import edu.mit.csail.sdg.ast.Sig.Field;
import edu.mit.csail.sdg.parser.CompUtil;
import edu.mit.csail.sdg.sim.SimInstance;
import edu.mit.csail.sdg.sim.SimTuple;
import edu.mit.csail.sdg.sim.SimTupleset;
import edu.mit.csail.sdg.translator.A4Solution;
import edu.mit.csail.sdg.translator.A4SolutionReader;
import edu.mit.csail.sdg.translator.A4Tuple;
import edu.mit.csail.sdg.translator.A4TupleSet;
import static edu.mit.csail.sdg.alloy4.A4Preferences.ImplicitThis;
import edu.mit.csail.sdg.alloy4.A4Preferences.Verbosity;
import static edu.mit.csail.sdg.alloy4.A4Preferences.VerbosityPref;
import kodkod.engine.fol2sat.HigherOrderDeclException;

import java.io.File;
import java.util.ArrayList;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;

public class Viz {

    private static final String main_model_name = "model2.als";

    public static void main(String[] args) throws Err {
        VizGUI viz = null;
        // Computer evaluator = new Computer();

        if (args.length != 1) {
            System.out.println("Error: please provide exactly one argument");
        } else {
            // viz = new VizGUI(false, args[0], null);
            viz = new VizGUI(true, args[0], null, null, evaluator, 1);
        }
    }

    // the rest of these methods are copied with minor modifications from Alloy's SimpleGUI.java file 
    // they are private there, but we need them in order to run the evaluator on arbitrary XMLs.
    // exported XMLs don't include the path to the main model file, which is necessary for creating the
    // evaluator, but we always know where it is, so we skip the lookup that the Alloy implementation does 
    // and just set the mainname directly.
    private static Computer evaluator = new Computer() {

        private String filename = null;

        @Override
        public final Object compute(final Object input) throws Exception {
            if (input instanceof File) {
                filename = ((File) input).getAbsolutePath();
                return "";
            }
            if (!(input instanceof String[]))
                return "";
            // [electrum] evaluator takes two arguments, the second is the focused state
            final String[] strs = (String[]) input;
            if (strs[0].trim().length() == 0)
                return ""; // Empty line
            Module root = null;
            A4Solution ans = null;
            try {
                Map<String,String> fc = new LinkedHashMap<String,String>();
                XMLNode x = new XMLNode(new File(filename));
                if (!x.is("alloy"))
                    throw new Exception();
                String mainname = System.getProperty("user.dir") + "/" + main_model_name;
                for (XMLNode sub : x)
                    if (sub.is("source")) {
                        String name = sub.getAttribute("filename");
                        String content = sub.getAttribute("content");
                        fc.put(name, content);
                    }
                root = CompUtil.parseEverything_fromFile(A4Reporter.NOP, fc, mainname, (Version.experimental && ImplicitThis.get()) ? 2 : 1);
                ans = A4SolutionReader.read(root.getAllReachableSigs(), x);
                for (ExprVar a : ans.getAllAtoms()) {
                    root.addGlobal(a.label, a);
                }
                for (ExprVar a : ans.getAllSkolems()) {
                    root.addGlobal(a.label, a);
                }
            } catch (Throwable ex) {
                throw new ErrorFatal("Failed to read or parse the XML file.");
            }
            try {
                Expr e = CompUtil.parseOneExpression_fromString(root, strs[0]);
                if (AlloyCore.isDebug() && VerbosityPref.get() == Verbosity.FULLDEBUG) {
                    SimInstance simInst = convert(root, ans);
                    if (simInst.wasOverflow())
                        return simInst.visitThis(e).toString() + " (OF)";
                }
                return ans.eval(e, Integer.valueOf(strs[1])).toString();
            } catch (HigherOrderDeclException ex) {
                throw new ErrorType("Higher-order quantification is not allowed in the evaluator.");
            }
        }
    };

    /** Converts an A4TupleSet into a SimTupleset object. */
    private static SimTupleset convert(Object object) throws Err {
        if (!(object instanceof A4TupleSet))
            throw new ErrorFatal("Unexpected type error: expecting an A4TupleSet.");
        A4TupleSet s = (A4TupleSet) object;
        if (s.size() == 0)
            return SimTupleset.EMPTY;
        List<SimTuple> list = new ArrayList<SimTuple>(s.size());
        int arity = s.arity();
        for (A4Tuple t : s) {
            String[] array = new String[arity];
            for (int i = 0; i < t.arity(); i++)
                array[i] = t.atom(i);
            list.add(SimTuple.make(array));
        }
        return SimTupleset.make(list);
    }

    /** Converts an A4Solution into a SimInstance object. */
    private static SimInstance convert(Module root, A4Solution ans) throws Err {
        SimInstance ct = new SimInstance(root, ans.getBitwidth(), ans.getMaxSeq());
        for (Sig s : ans.getAllReachableSigs()) {
            if (!s.builtin)
                ct.init(s, convert(ans.eval(s)));
            for (Field f : s.getFields())
                if (!f.defined)
                    ct.init(f, convert(ans.eval(f)));
        }
        for (ExprVar a : ans.getAllAtoms())
            ct.init(a, convert(ans.eval(a)));
        for (ExprVar a : ans.getAllSkolems())
            ct.init(a, convert(ans.eval(a)));
        return ct;
    }
}
