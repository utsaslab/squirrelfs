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
import edu.mit.csail.sdg.alloy4.ConstList;
import edu.mit.csail.sdg.alloy4.SafeList;
import edu.mit.csail.sdg.alloy4.Err;
import edu.mit.csail.sdg.ast.Module;
import edu.mit.csail.sdg.ast.Expr;
import edu.mit.csail.sdg.ast.ExprBinary;
import edu.mit.csail.sdg.ast.ExprCall;
import edu.mit.csail.sdg.ast.ExprChoice;
import edu.mit.csail.sdg.ast.ExprConstant;
import edu.mit.csail.sdg.ast.ExprCustom;
import edu.mit.csail.sdg.ast.ExprHasName;
import edu.mit.csail.sdg.ast.ExprITE;
import edu.mit.csail.sdg.ast.ExprLet;
import edu.mit.csail.sdg.ast.ExprList;
import edu.mit.csail.sdg.ast.ExprQt;
import edu.mit.csail.sdg.ast.ExprUnary;
import edu.mit.csail.sdg.ast.ExprVar;
import edu.mit.csail.sdg.ast.Sig;
import edu.mit.csail.sdg.ast.Sig.Field;
import edu.mit.csail.sdg.ast.Func;
import edu.mit.csail.sdg.ast.Decl;
import edu.mit.csail.sdg.parser.CompUtil;

import java.util.ArrayList;
import java.util.Arrays;
import java.util.HashMap;
import java.util.HashSet;
import java.lang.RuntimeException;

public final class Check {

    public static void main(String[] args) throws Err {
        A4Reporter rep = null;
        String transitions_filename = "transitions.als";
        String defs_filename = "defs.als";
        String model_filename = "model2.als";

        // Parse+typecheck the model
        Module t_world = CompUtil.parseEverything_fromFile(rep, null, transitions_filename);
        Module d_world = CompUtil.parseEverything_fromFile(rep, null, defs_filename);
        Module m_world = CompUtil.parseEverything_fromFile(rep, null, model_filename);

        HashMap<String, Predicate> def_preds = new HashMap<String, Predicate>();

        // need sigs and predicates from defs
        ConstList<Sig> d_sigs = d_world.getAllReachableUserDefinedSigs();
        SafeList<Func> d_preds = d_world.getAllFunc();

        HashMap<String, CheckedField> checked_fields = new HashMap<String, CheckedField>();
        HashSet<String> var_sets = new HashSet<String>();

        set_up_checked_fields(d_sigs, checked_fields);

        for (int i = 0; i < d_sigs.size(); i++) {
            // record variable signatures for checking
            SafeList<Field> fields = d_sigs.get(i).getFields();
            if (fields.isEmpty() && d_sigs.get(i).isVariable != null) {
                var_sets.add(d_sigs.get(i).label.replace("this/", ""));
            }
        }

        // need predicates from transitions
        SafeList<Func> t_preds = t_world.getAllFunc();

        // process defs predicates so we can easily look them up without prefixes
        for (int i = 0; i < d_preds.size(); i++) {
            if (!d_preds.get(i).isPred) {
                continue;
            }
            // skip unchanged predicates since we interpret them differently
            if (d_preds.get(i).label.contains("this/unchanged")) {
                continue;
            }
            Expr body = d_preds.get(i).getBody();
            ConstList<Decl> decls = d_preds.get(i).decls;
            Predicate pred = new Predicate(d_preds.get(i).label, body, decls, def_preds);
            // add predicate to the map if it has any frame conditions or effects
            if (!pred.changed.isEmpty() || !pred.unchanged.isEmpty()) {
                String no_prefix = pred.label.replace("this/", "");
                def_preds.put(no_prefix, pred);
            }
        }

        HashSet<String> arguments = new HashSet<String>(Arrays.asList(args));
        // process transition predicates
        for (int i = 0; i < t_preds.size(); i++) {
            String label = t_preds.get(i).label.replace("this/", "");
            // if transition names have been provided, skip all other transitions
            if (!arguments.isEmpty() && !arguments.contains(label)) {
                continue;
            }
            // some transitions have some unusual effects so we are skipping them here to
            // make life easier
            // TODO: handle them
            if (t_preds.get(i).label.contains("crash") ||
                    t_preds.get(i).label.contains("clear_inflight_state") ||
                    t_preds.get(i).label.contains("complete_creat_and_link") ||
                    t_preds.get(i).label.contains("complete_mkdir") ||
                    t_preds.get(i).label.contains("complete_unlink_keep_inode") ||
                    t_preds.get(i).label.contains("complete_rename") ||
                    t_preds.get(i).label.contains("start_recovery") ||
                    t_preds.get(i).label.contains("Default")) {
                System.out.println("Skipped " + label);
                continue;
            }
            Expr body = t_preds.get(i).getBody();
            ConstList<Decl> decls = t_preds.get(i).decls;
            System.out.println(label + ":");
            Predicate pred = new Predicate(label, body, decls, def_preds);

            pred.check_frame_conditions(checked_fields, var_sets);

            // rebuild the checked fields structures for the next loop
            checked_fields.clear();
            set_up_checked_fields(d_sigs, checked_fields);
        }
    }

    public static void set_up_checked_fields(ConstList<Sig> d_sigs, HashMap<String, CheckedField> checked_fields) {
        for (int i = 0; i < d_sigs.size(); i++) {
            SafeList<Field> fields = d_sigs.get(i).getFields();
            for (int j = 0; j < fields.size(); j++) {
                CheckedField checked_field = new CheckedField(fields.get(j));
                // use the name of the sig and field without prefix to identify the field
                // this will make it easier to look up later
                String field_id = fields.get(j).sig.label.replace("this/", "") + "." + fields.get(j).label;
                // manually skip Volatile fields that do not require direct frame conditions
                if (!field_id.equals("Volatile.children") && !field_id.equals("Volatile.parent")
                        && !field_id.equals("Volatile.owns") && fields.get(j).isVariable != null) {
                    checked_fields.put(field_id, checked_field);
                }
            }
        }
    }
}

class Predicate {

    public final String label;
    public ArrayList<Expr> changed;
    public ArrayList<Expr> unchanged;
    public HashMap<ExprHasName, Expr> arguments;
    public HashMap<String, Predicate> def_preds;

    public Predicate(String label, Expr e, ConstList<Decl> args, HashMap<String, Predicate> def_preds) {
        arguments = new HashMap<ExprHasName, Expr>();

        this.label = label;

        // get arguments to the predicate
        for (int i = 0; i < args.size(); i++) {
            Expr expr = args.get(i).expr;
            args.get(i).names.forEach(name -> {
                arguments.put(name, expr);
            });
        }

        changed = new ArrayList<Expr>();
        unchanged = new ArrayList<Expr>();
        this.def_preds = def_preds;

        ArrayList<Expr> exprs_to_process = new ArrayList<Expr>();
        exprs_to_process.add(e);

        while (!exprs_to_process.isEmpty()) {
            e = exprs_to_process.remove(0);
            if (e instanceof ExprBinary) {
                // determine if one side of the binary is a prime expr
                Expr prime_expr = check_expr_binary((ExprBinary) e);
                // if it is, save it in the changed list
                // TODO: is it safe to assume that these are changes? does it actually matter?
                if (prime_expr != null) {
                    changed.add(clean_up(prime_expr));
                }
            } else if (e instanceof ExprCall) {
                check_expr_call((ExprCall) e, changed, unchanged);
            } else if (e instanceof ExprChoice) {
                System.out.println("expr choice");
                throw new CheckerException(e, "unimplemented expr type");
            } else if (e instanceof ExprConstant) {
                // skip
            } else if (e instanceof ExprCustom) {
                System.out.println("expr custom");
                throw new CheckerException(e, "unimplemented expr type");
            } else if (e instanceof ExprHasName) {
                // skip
            } else if (e instanceof ExprITE) {
                check_expr_ite((ExprITE) e);
            } else if (e instanceof ExprLet) {
                Expr new_expr = split_expr_let((ExprLet) e);
                exprs_to_process.add(new_expr);
            } else if (e instanceof ExprList) {
                ArrayList<Expr> new_exprs = split_expr_list((ExprList) e);
                exprs_to_process.addAll(new_exprs);
            } else if (e instanceof ExprQt) {
                check_expr_qt((ExprQt) e, unchanged);
            } else if (e instanceof ExprUnary) {
                ExprUnary expr_unary = (ExprUnary) e;
                boolean result = check_expr_unary(expr_unary);
                // if (!result) {
                Expr new_expr = split_expr_unary(expr_unary);
                exprs_to_process.add(new_expr);
                // }
            } else if (e instanceof ExprVar) {
                System.out.println("expr var");
                throw new CheckerException(e, "unimplemented expr type");
            } else if (e instanceof Sig) {
                // skip
            }

            else {
                System.out.println("something else");
                System.out.println(e);
                System.out.println(e.getClass());
                throw new CheckerException(e, "unrecognized expr type");
            }
        }
    }

    public static Expr[] split_expr_binary(ExprBinary expr) {
        Expr ret[] = new Expr[2];

        ret[0] = expr.left;
        ret[1] = expr.right;
        return ret;
    }

    public static Expr[] split_expr_ite(ExprITE expr) {
        Expr ret[] = new Expr[2];

        ret[0] = expr.left;
        ret[1] = expr.right;
        return ret;
    }

    public static Expr split_expr_let(ExprLet expr) {
        return expr.sub;
    }

    public static ArrayList<Expr> split_expr_list(ExprList expr) {
        ArrayList<Expr> expr_list = new ArrayList<Expr>();
        for (int i = 0; i < expr.args.size(); i++) {
            expr_list.add(expr.args.get(i));
        }
        return expr_list;
    }

    public static Expr split_expr_unary(ExprUnary expr) {
        return expr.sub;
    }

    public static Expr check_expr_binary(ExprBinary expr) throws RuntimeException {
        if (expr.op == ExprBinary.Op.EQUALS || expr.op == ExprBinary.Op.NOT_EQUALS || expr.op == ExprBinary.Op.IN
                || expr.op == ExprBinary.Op.NOT_IN) {
            // prime is a unary expression so we need to determine if either side contains
            // it as a subexpression
            boolean prime_left = contains_prime_expr(expr.left);
            boolean prime_right = contains_prime_expr(expr.right);

            Expr prime_expr = null;
            if (prime_left) {
                prime_expr = expr.left;
            }
            if (prime_right) {
                prime_expr = expr.right;
            }
            if (prime_left && prime_right) {
                // TODO: are there any places where we do this? is it actually illegal?
                throw new CheckerException(expr, "prime_left and prime_right are both true");
            }

            return prime_expr;
        }

        return null;
    }

    public void check_expr_call(ExprCall expr,
            ArrayList<Expr> changed_list,
            ArrayList<Expr> unchanged_list) {
        if (expr.fun.label.contains("/unchanged")) {
            // unchanged only takes one argument
            Expr arg = expr.args.get(0);
            unchanged_list.add(clean_up(arg));
        } else {
            if (def_preds != null) {
                String no_prefix = expr.fun.label.replace("defs/", "");
                no_prefix = no_prefix.replace("this/", "");
                if (def_preds.containsKey(no_prefix)) {
                    Predicate called_pred = def_preds.get(no_prefix);
                    changed_list.addAll(called_pred.changed);
                    unchanged_list.addAll(called_pred.unchanged);
                }
            }
        }
    }

    public void check_expr_ite(ExprITE expr) {
        Expr if_branch = expr.left;
        Expr else_branch = expr.right;

        ArrayList<Expr> if_changed = new ArrayList<Expr>();
        ArrayList<Expr> if_unchanged = new ArrayList<Expr>();
        ArrayList<Expr> else_changed = new ArrayList<Expr>();
        ArrayList<Expr> else_unchanged = new ArrayList<Expr>();

        ArrayList<Expr> exprs_to_process = new ArrayList<Expr>();
        exprs_to_process.add(if_branch);

        while (!exprs_to_process.isEmpty()) {
            if_branch = exprs_to_process.remove(0);
            // get changed and unchanged from if branch
            if (if_branch instanceof ExprBinary) {
                Expr result = check_expr_binary((ExprBinary) if_branch);
                if (result != null) {
                    if_changed.add(clean_up(result));
                }
            } else if (if_branch instanceof ExprCall) {
                check_expr_call((ExprCall) if_branch, if_changed, if_unchanged);
            } else if (if_branch instanceof ExprQt) {
                check_expr_qt((ExprQt) if_branch, if_unchanged);
            } else if (if_branch instanceof ExprList) {
                ArrayList<Expr> new_exprs = split_expr_list((ExprList) if_branch);
                exprs_to_process.addAll(new_exprs);
            }
        }

        exprs_to_process.add(else_branch);
        while (!exprs_to_process.isEmpty()) {
            else_branch = exprs_to_process.remove(0);
            // get changed and unchanged from else branch
            if (else_branch instanceof ExprBinary) {
                Expr result = check_expr_binary((ExprBinary) else_branch);
                if (result != null) {
                    else_changed.add(clean_up(result));
                }
            } else if (else_branch instanceof ExprCall) {
                check_expr_call((ExprCall) else_branch, else_changed, else_unchanged);
            } else if (else_branch instanceof ExprQt) {
                check_expr_qt((ExprQt) else_branch, else_unchanged);
            } else if (else_branch instanceof ExprList) {
                ArrayList<Expr> new_exprs = split_expr_list((ExprList) else_branch);
                exprs_to_process.addAll(new_exprs);
            }
        }

        // convert the arraylists to sets since we don't care about order
        HashSet<Expr> if_set = new HashSet<Expr>(if_changed);
        if_set.addAll(if_unchanged);
        HashSet<Expr> else_set = new HashSet<Expr>(else_changed);
        else_set.addAll(else_unchanged);

        // TODO: perform this check without having to do two nested loops
        // the HashSet .equals() method doesn't use the right comparison method
        boolean found_issue = false;
        boolean found_match;
        for (Expr if_element : if_set) {
            found_match = false;
            for (Expr else_element : else_set) {
                if (if_element.isSame(else_element)) {
                    found_match = true;
                    break;
                }
            }
            if (!found_match) {
                System.out.println("\tWARNING: if and else branches of an if-then-else statement may not match");
                System.out.println("\tChanges in if branch: ");
                for (Expr e : if_changed) {
                    System.out.println("\t\t" + e);
                }
                System.out.println("\tFrame conditions in if branch: ");
                for (Expr e : if_unchanged) {
                    System.out.println("\t\t" + e);
                }
                System.out.println("\tChanges in else branch: ");
                for (Expr e : else_changed) {
                    System.out.println("\t\t" + e);
                }
                System.out.println("\tFrame conditions in else branch: ");
                for (Expr e : else_unchanged) {
                    System.out.println("\t\t" + e);
                }
                found_issue = true;
                break;
            }
        }
        if (!found_issue) {
            for (Expr else_element : else_set) {
                found_match = false;
                for (Expr if_element : if_set) {
                    if (if_element.isSame(else_element)) {
                        found_match = true;
                        break;
                    }
                }
                if (!found_match) {
                    System.out.println("\tWARNING: if and else branches of an if-then-else statement may not match");
                    System.out.println("\tChanges in if branch: " + if_changed);
                    System.out.println("\tFrame conditions in if branch: " + if_unchanged);
                    System.out.println("\tChanges in else branch: " + else_changed);
                    System.out.println("\tFrame conditions in else branch: " + else_unchanged);
                    break;
                }
            }
        }

        // TODO: since changed and unchanged are ArrayLists, this will result in
        // redundant entries being added in most cases. They should probably be sets
        changed.addAll(if_changed);
        changed.addAll(else_changed);
        unchanged.addAll(if_unchanged);
        unchanged.addAll(else_unchanged);
    }

    public void check_expr_qt(ExprQt expr, ArrayList<Expr> unchanged_list) {
        if (expr.op == ExprQt.Op.ALL) {
            if (contains_unchanged_call(expr.sub)) {
                unchanged_list.add(clean_up(expr));
            }
        }
    }

    public boolean check_expr_unary(ExprUnary expr) {
        if (expr.op == ExprUnary.Op.NO) {
            boolean result = contains_prime_expr(expr);
            if (result) {
                changed.add(((ExprUnary) clean_up(expr)).sub);
                return true;
            }
        }
        return false;
    }

    public static boolean contains_unchanged_call(Expr e) {
        ArrayList<Expr> exprs_to_process = new ArrayList<Expr>();
        exprs_to_process.add(e);
        while (!exprs_to_process.isEmpty()) {
            e = exprs_to_process.remove(0);
            if (e instanceof ExprBinary) {
                Expr[] new_exprs = split_expr_binary((ExprBinary) e);
                if (new_exprs != null) {
                    exprs_to_process.add(new_exprs[0]);
                    exprs_to_process.add(new_exprs[1]);
                }
            } else if (e instanceof ExprCall) {
                ExprCall expr_call = (ExprCall) e;
                if (expr_call.fun.label.contains("/unchanged")) {
                    return true;
                }
                // otherwise skip

            } else if (e instanceof ExprList) {
                ArrayList<Expr> new_exprs = split_expr_list((ExprList) e);
                exprs_to_process.addAll(new_exprs);
            } else if (e instanceof ExprUnary) {
                ExprUnary expr_unary = (ExprUnary) e;
                exprs_to_process.add(expr_unary.sub);
            } else if (e instanceof ExprITE) {
                Expr[] new_exprs = split_expr_ite((ExprITE) e);
                exprs_to_process.add(new_exprs[0]);
                exprs_to_process.add(new_exprs[1]);
            } else if (e instanceof ExprConstant || e instanceof ExprVar || e instanceof Sig
                    || e instanceof ExprHasName || e instanceof ExprQt) {
                // skip
                // TODO: handle quantified expressions
            } else {
                System.out.println(e);
                System.out.println(e.getClass());
                throw new CheckerException(e, "unrecognized expr type");
            }
        }
        return false;
    }

    // this should be called on both sides of a binary expression that we think may
    // enforce a frame condition or an effect
    public static boolean contains_prime_expr(Expr e) {
        ArrayList<Expr> exprs_to_process = new ArrayList<Expr>();
        exprs_to_process.add(e);
        while (!exprs_to_process.isEmpty()) {
            e = exprs_to_process.remove(0);
            if (e instanceof ExprBinary) {
                Expr[] new_exprs = split_expr_binary((ExprBinary) e);
                if (new_exprs != null) {
                    exprs_to_process.add(new_exprs[0]);
                    exprs_to_process.add(new_exprs[1]);
                }
            } else if (e instanceof ExprUnary) {
                ExprUnary unary_expr = (ExprUnary) e;
                if (unary_expr.op == ExprUnary.Op.PRIME) {
                    return true;
                }
                Expr new_expr = split_expr_unary((ExprUnary) e);
                exprs_to_process.add(new_expr);

            }
            // TODO: are there any other types we need to handle?
        }
        return false;
    }

    // this one is recursive :3
    public Expr clean_up(Expr e) {
        if (e instanceof ExprBinary) {
            ExprBinary expr_binary = (ExprBinary) e;
            Expr left = clean_up(expr_binary.left);
            Expr right = clean_up(expr_binary.right);
            ExprBinary.Op op = (ExprBinary.Op) expr_binary.op;
            return op.make(expr_binary.pos, null, left, right);
        } else if (e instanceof ExprCall) {
            ExprCall expr_call = (ExprCall) e;
            Func fun = expr_call.fun;
            String label = fun.label.replace("this/", "").replace("defs/", "");
            Expr body = clean_up(fun.getBody());
            Func new_fun = new Func(fun.pos, label, fun.decls, fun.returnDecl, body);
            return ExprCall.make(expr_call.pos, null, new_fun, expr_call.args, expr_call.extraWeight);
        } else if (e instanceof ExprHasName) {
            return e;
        } else if (e instanceof ExprITE) {
            ExprITE expr_ite = (ExprITE) e;
            Expr cond = clean_up(expr_ite.cond);
            Expr left = clean_up(expr_ite.left);
            Expr right = clean_up(expr_ite.right);
            return ExprITE.make(expr_ite.pos, cond, left, right);
        } else if (e instanceof ExprList) {
            ExprList expr_list = (ExprList) e;
            ArrayList<Expr> exprs = new ArrayList<Expr>();
            for (int i = 0; i < expr_list.args.size(); i++) {
                Expr new_expr = clean_up(expr_list.args.get(i));
                exprs.add(new_expr);
            }
            return ExprList.make(expr_list.pos, null, expr_list.op, exprs);
        } else if (e instanceof ExprQt) {
            ExprQt expr_qt = (ExprQt) e;
            ArrayList<Decl> decls = new ArrayList<Decl>();
            for (int i = 0; i < expr_qt.decls.size(); i++) {
                Decl decl = expr_qt.decls.get(i);
                Expr new_expr = clean_up(decl.expr);
                Decl new_decl = new Decl(decl.isPrivate, decl.disjoint, decl.disjoint2, decl.isVar, decl.names,
                        new_expr);
                decls.add(new_decl);
            }
            Expr sub = clean_up(expr_qt.sub);
            ExprQt.Op op = (ExprQt.Op) expr_qt.op;
            return op.make(expr_qt.pos, null, decls, sub);
        } else if (e instanceof ExprUnary) {
            ExprUnary expr_unary = (ExprUnary) e;
            Expr sub = clean_up(expr_unary.sub);
            // sub might be a Field here. If it is, then ideally we would make sure it
            if (expr_unary.op == ExprUnary.Op.PRIME) {
                return sub;
            } else {
                ExprUnary.Op op = (ExprUnary.Op) expr_unary.op;
                return op.make(expr_unary.pos, sub);
            }
        } else if (e instanceof Sig) {
            return e;
        } else {
            System.out.println(e);
            System.out.println(e.getClass());
            throw new CheckerException(e, "unhandled expr type in clean_up");
        }
    }

    public CheckedField get_or_throw(HashMap<String, CheckedField> checked_fields, String key) {
        CheckedField ret = checked_fields.get(key);
        if (ret == null) {
            throw new RuntimeException(
                    String.format("%s is not a field that requires a frame condition (is it marked as Var?)", key));
        }
        return ret;
    }

    public void check_frame_conditions(HashMap<String, CheckedField> checked_fields, HashSet<String> var_sets) {
        HashSet<String> unknown_var_sets = new HashSet<String>(var_sets);
        boolean bug = false;

        // go through the unchanged list and make checked fields
        for (int i = 0; i < unchanged.size(); i++) {
            Expr u = unchanged.get(i);
            if (u instanceof ExprQt) {
                // determine what we quantify over and which fields are unchanged
                ExprQt e = (ExprQt) u;
                if (e.op != ExprQt.Op.ALL) {
                    throw new CheckerException(e, "Unsupported quantifier");
                }
                if (e.decls.size() > 1) {
                    throw new CheckerException(e, "too many decls");
                }
                Decl var_decl = e.decls.get(0);
                if (var_decl.names.size() > 1) {
                    throw new CheckerException(e, "too many names");
                }
                ExprHasName var = var_decl.names.get(0);
                // for some reason the expr is always preceded by 'one'
                // which we do not care about, so we remove it
                Expr quant_expr = ((ExprUnary) var_decl.expr).sub;

                // it should be safe to assume that the expr in the quantifier is
                // a single unchanged[] call or the AND of two or more.
                // crash and clear_inflight_fields might have other ones but we
                // do not check them right now so we can ignore them here
                Expr subexpr = e.sub;
                ArrayList<Expr> expr_args = new ArrayList<Expr>();
                ArrayList<Field> unchanged_fields = new ArrayList<Field>();
                while (expr_args.isEmpty()) {
                    if (subexpr instanceof ExprList) {
                        ExprList expr_list = (ExprList) subexpr;
                        if (expr_list.op != ExprList.Op.AND) {
                            throw new CheckerException(e, "unhandled expr list type");
                        }
                        for (int j = 0; j < expr_list.args.size(); j++) {
                            Expr arg = expr_list.args.get(j);
                            if (arg instanceof ExprCall) {
                                ExprCall expr_call = (ExprCall) arg;
                                expr_args.addAll(expr_call.args);
                            } else {
                                System.out.println(arg);
                                throw new CheckerException(e, "non-call expr in quantified expression");
                            }
                        }
                    } else if (subexpr instanceof ExprCall) {
                        ExprCall expr_call = (ExprCall) subexpr;
                        expr_args.addAll(expr_call.args);
                    } else if (subexpr instanceof ExprUnary) {
                        ExprUnary expr_unary = (ExprUnary) subexpr;
                        if (expr_unary.op == ExprUnary.Op.NOOP) {
                            subexpr = expr_unary.sub;
                        } else {
                            throw new CheckerException(e, "unrecognized expr unary type");
                        }
                    } else {
                        System.out.println(subexpr.getClass());
                        throw new CheckerException(e, "unhandled expr type");
                    }
                }

                // obtain the fields from expr_args
                // since we're in a quantified expr, it should be safe to assume
                // that all args are JOINs where the left side is the field we are
                // interested in
                for (int j = 0; j < expr_args.size(); j++) {
                    // XXX: argument may be an ExprUnary - currently we throw a
                    // ClassCastException but maybe there's a better thing to do?
                    ExprBinary expr_binary = (ExprBinary) expr_args.get(j);
                    if (expr_binary.op != ExprBinary.Op.JOIN) {
                        throw new CheckerException(e, "non-join op in quantified expr args");
                    }
                    ExprUnary right = (ExprUnary) expr_binary.right;
                    Field f = (Field) right.sub;

                    unchanged_fields.add(f);
                }

                // get the checked field for each unchanged field
                // and process it with the quant expr
                for (int j = 0; j < unchanged_fields.size(); j++) {
                    Field f = unchanged_fields.get(j);
                    String field_id = f.sig.label.replace("this/", "").replace("defs/", "") + "." + f.label;
                    CheckedField cf = get_or_throw(checked_fields, field_id);
                    cf.update_unknowns(quant_expr);
                }
            } else if (u instanceof ExprUnary) {
                // this expr is an argument to unchanged[]
                ExprUnary expr_unary = (ExprUnary) u;
                if (expr_unary.op != ExprUnary.Op.NOOP) {
                    throw new CheckerException(u, "expr unary op is not NOOP");
                }
                Expr arg = expr_unary.sub;
                if (arg instanceof Sig) {
                    Sig sig = (Sig) arg;
                    unknown_var_sets.remove(sig.label.replace("this/", "").replace("defs/", ""));
                } else {
                    throw new CheckerException(u, "arg is not a sig");
                }
            } else if (u instanceof ExprBinary) {
                // this expr is an argument to unchanged[]
                ExprBinary expr_binary = (ExprBinary) u;
                if (expr_binary.op != ExprBinary.Op.JOIN) {
                    throw new CheckerException(u, "expr binary op is not JOIN");
                }
                // update the checked field corresponding to this expression
                Field right = (Field) ((ExprUnary) expr_binary.right).sub;
                Expr left = ((ExprUnary) expr_binary.left).sub;
                String field_id = right.sig.label.replace("this/", "").replace("defs/", "") + "." + right.label;
                CheckedField cf = get_or_throw(checked_fields, field_id);
                cf.update_unknowns(left);
            }
        }

        for (int i = 0; i < changed.size(); i++) {
            Expr c = changed.get(i);
            if (c instanceof ExprBinary) {
                ExprBinary expr_binary = (ExprBinary) c;
                if (expr_binary.op == ExprBinary.Op.JOIN) {
                    // assume that left side is variable name and
                    // right side is a field
                    // System.out.println(expr_binary.left);
                    if (expr_binary.left instanceof ExprUnary) {
                        ExprUnary left_unary = (ExprUnary) expr_binary.left;
                        if (left_unary.sub instanceof ExprHasName) {
                            ExprHasName left = (ExprHasName) left_unary.sub;
                            Field right = (Field) ((ExprUnary) expr_binary.right).sub;
                            String field_id = right.sig.label.replace("this/", "").replace("defs/", "") + "."
                                    + right.label;
                            CheckedField cf = get_or_throw(checked_fields, field_id);
                            cf.update_unknowns(left);
                        } else {
                            Sig left = (Sig) left_unary.sub;
                            Field right = (Field) ((ExprUnary) expr_binary.right).sub;
                            String field_id = right.sig.label.replace("this/", "").replace("defs/", "") + "."
                                    + right.label;
                            CheckedField cf = get_or_throw(checked_fields, field_id);
                            cf.update_unknowns(left);
                        }
                    }
                    // TODO: don't skip cases where the op is not unary
                } else {
                    throw new CheckerException(c, "unrecognized expr binary op type");
                }
            } else if (c instanceof ExprUnary) {
                // changed var set
                ExprUnary expr_unary = (ExprUnary) c;
                if (expr_unary.op == ExprUnary.Op.NOOP) {
                    Sig sig = (Sig) expr_unary.sub;
                    unknown_var_sets.remove(sig.label.replace("this/", "").replace("defs/", ""));
                } else {
                    System.out.println(expr_unary);
                    System.out.println(expr_unary.op);
                    throw new CheckerException(c, "expr unary op is not NOOP");
                }
            } else {
                throw new CheckerException(c, "unrecognized expr type");
            }
        }

        for (String key : checked_fields.keySet()) {
            if (!checked_fields.get(key).unknown.isEmpty()) {
                bug = true;
                System.out
                        .println("\t" + key + " may require a frame condition for " + checked_fields.get(key).unknown);
            }
        }

        if (!unknown_var_sets.isEmpty()) {
            bug = true;
            System.out.println("\tFrame conditions may be missing for " + unknown_var_sets);
        }

        if (!bug) {
            System.out.println("\tNo frame condition issues detected");
        }

    }
}

class CheckedField {

    public final Field field;

    public HashSet<String> unknown; // instances of the sig that have not been used in a frame cond or effect yet
    // if anything is left in unknown when we are done checking, there is a missing
    // frame condition
    // we use strings to avoid prefix mismatch issues
    // TODO: maybe don't use strings though
    public HashSet<String> known;

    public CheckedField(Field f) {
        this.field = f;
        unknown = new HashSet<String>();
        known = new HashSet<String>();
        unknown.add(f.sig.label.replace("this/", "").replace("defs/", ""));
    }

    public void update_unknowns(Expr e) {
        // I'm pretty sure we should only ever see set difference in here
        // not set union. idk how to handle set union if it does come up
        if (e instanceof ExprUnary) {
            ExprUnary expr_unary = (ExprUnary) e;
            if (expr_unary.op == ExprUnary.Op.NOOP) {
                if (expr_unary.sub instanceof Sig) {
                    Sig sig = (Sig) expr_unary.sub;
                    String label = sig.label.replace("this/", "").replace("defs/", "");
                    boolean result = unknown.remove(label);
                    // TODO: what should we do if the thing we want to remove isn't in unknown?
                    // if (!result) {
                    // throw new CheckerException(e, "unknown list did not include " + label);
                    // }
                    known.add(label);
                } else {
                    throw new CheckerException(e, "unrecognized expr unary subexpression");
                }
            } else {
                throw new CheckerException(e, "unrecognized expr unary op");
            }
        } else if (e instanceof ExprBinary) {
            ExprBinary expr_binary = (ExprBinary) e;
            if (expr_binary.op == ExprBinary.Op.MINUS) {

                // handle the left side first - it might contain multiple minus ops,
                // so we need to handle all of them
                Expr left_expr = (Expr) expr_binary.left;
                while (left_expr instanceof ExprBinary) {
                    ExprBinary left_binary = (ExprBinary) left_expr;
                    left_expr = left_binary.left;

                    // we need to reduce any nested binary expressions we encounter
                    // down to their right-most unary part.
                    // TODO: this can result in weird output indicating that we need
                    // a frame condition for something we don't. Would it be better
                    // to just skip this type of expression?
                    ExprUnary right = null;
                    if (expr_binary.right instanceof ExprBinary) {
                        ExprBinary r = (ExprBinary) expr_binary.right;
                        while (r.right instanceof ExprBinary) {
                            r = (ExprBinary) r.right;
                        }
                        right = (ExprUnary) r.right;
                    } else {
                        right = (ExprUnary) expr_binary.right;
                    }

                    if (right.sub instanceof Sig) {
                        Sig sig_right = (Sig) right.sub;
                        String label = sig_right.label.replace("this/", "").replace("defs/", "");
                        unknown.add(label);
                    } else if (right.sub instanceof ExprHasName) {
                        ExprHasName sig_right = (ExprHasName) right.sub;
                        String label = sig_right.label.replace("this/", "").replace("defs/", "");
                        unknown.add(label);
                    }

                }
                ExprUnary left = (ExprUnary) left_expr;
                // left is going to be a sig, but right could be a sig or a named var
                if (left.sub instanceof Sig) {
                    Sig sig_left = (Sig) left.sub;
                    String label = sig_left.label.replace("this/", "").replace("defs/", "");
                    boolean result = unknown.remove(label);
                    // // TODO: what should we do if the thing we want to remove isn't in unknown?
                    // if (!result) {
                    // throw new RuntimeException("unknown list did not include " + label);
                    // }
                    known.add(label);
                } else {
                    throw new CheckerException(e, "left expr is not sig");
                }

                ExprUnary right = null;
                if (expr_binary.right instanceof ExprBinary) {
                    ExprBinary r = (ExprBinary) expr_binary.right;
                    while (r.right instanceof ExprBinary) {
                        r = (ExprBinary) r.right;
                    }
                    right = (ExprUnary) r.right;
                } else {
                    right = (ExprUnary) expr_binary.right;
                }

                if (right.sub instanceof Sig) {
                    Sig sig_right = (Sig) right.sub;
                    String label = sig_right.label.replace("this/", "").replace("defs/", "");
                    unknown.add(label);
                } else if (right.sub instanceof ExprHasName) {
                    ExprHasName sig_right = (ExprHasName) right.sub;
                    String label = sig_right.label.replace("this/", "").replace("defs/", "");
                    unknown.add(label);
                }

                // System.out.println("NOW PROCESSING " + expr_binary.right);
                // System.out.println(expr_binary);
                // ExprUnary right = (ExprUnary) expr_binary.right;
                // if (right.sub instanceof Sig) {
                // Sig sig_right = (Sig) right.sub;
                // String label = sig_right.label.replace("this/", "").replace("defs/", "");
                // unknown.add(label);
                // } else if (right.sub instanceof ExprHasName) {
                // ExprHasName sig_right = (ExprHasName) right.sub;
                // String label = sig_right.label.replace("this/", "").replace("defs/", "");
                // unknown.add(label);
                // }
            } else {
                throw new CheckerException(e, "unrecognized expr binary op");
            }
        } else if (e instanceof ExprHasName) {
            ExprHasName expr_has_name = (ExprHasName) e;
            boolean result = unknown.remove(expr_has_name.label);
            // TODO: what should we do if the thing we want to remove isn't in unknown?
            // if (!result) {
            // throw new CheckerException(e, "unknown list did not include variable " +
            // expr_has_name.label);
            // }
            known.add(expr_has_name.label);
        } else if (e instanceof Sig) {
            // TODO: if we ever expand Volatile to contain more info this might not work
            // properly
            Sig sig = (Sig) e;
            String sig_name = sig.label.replace("this/", "").replace("defs/", "");
            boolean result = unknown.remove(sig_name);
            // TODO: what should we do if the thing we want to remove isn't in unknown?
            if (!result) {
                throw new CheckerException(e, "unknown list did not include " + sig_name);
            }
            known.add(sig_name);
        } else {
            throw new CheckerException(e, "unrecognized expr type");
        }

    }
}

class CheckerException extends RuntimeException {
    public CheckerException(Expr e, String msg) {
        super(String.format("Error in expr \"%s\" at %s: %s",
                e.toString(), e.pos.toShortString(), msg));
    }
}
