import java.util.*;

/** Tiny C compiler: lexer, recursive-descent parser, x86-64 (AT&T) code generator. */
public class Compiler {
    // ---------- Lexer ----------
    record Tok(String kind, String text) {}

    static List<Tok> lex(String src) {
        List<Tok> out = new ArrayList<>();
        int i = 0;
        String[] two = {"&&", "||", "==", "!=", "<=", ">="};
        while (i < src.length()) {
            char c = src.charAt(i);
            if (Character.isWhitespace(c)) { i++; continue; }
            if (Character.isDigit(c)) {
                int j = i;
                while (j < src.length() && Character.isDigit(src.charAt(j))) j++;
                out.add(new Tok("num", src.substring(i, j))); i = j; continue;
            }
            if (Character.isLetter(c) || c == '_') {
                int j = i;
                while (j < src.length() && (Character.isLetterOrDigit(src.charAt(j)) || src.charAt(j) == '_')) j++;
                String w = src.substring(i, j);
                out.add(new Tok(w.equals("int") || w.equals("return") ? w : "id", w)); i = j; continue;
            }
            String t = null;
            for (String s : two) if (src.startsWith(s, i)) t = s;
            if (t == null) {
                if ("(){};-~!+*/%<>=".indexOf(c) < 0) throw new RuntimeException("bad char: " + c);
                t = String.valueOf(c);
            }
            out.add(new Tok(t, t)); i += t.length();
        }
        return out;
    }

    // ---------- Parser + generator (single pass) ----------
    final List<Tok> toks; int pos = 0;
    final StringBuilder asm = new StringBuilder();
    final Map<String, Integer> vars = new HashMap<>(); // name -> offset from %rbp
    int stackOff = 0, labels = 0;

    Compiler(String src) { toks = lex(src); }

    Tok peek() { return pos < toks.size() ? toks.get(pos) : new Tok("eof", ""); }
    Tok next() { return toks.get(pos++); }
    Tok expect(String k) {
        Tok t = peek();
        if (!t.kind().equals(k)) throw new RuntimeException("expected " + k + " but got " + t.kind());
        return next();
    }
    boolean accept(String k) { if (peek().kind().equals(k)) { pos++; return true; } return false; }
    void emit(String s) { asm.append("\t").append(s).append("\n"); }
    String label() { return ".L" + labels++; }

    String program() {
        expect("int"); String name = expect("id").text();
        expect("("); expect(")"); expect("{");
        asm.append("\t.globl ").append(name).append("\n").append(name).append(":\n");
        emit("pushq %rbp"); emit("movq %rsp, %rbp");
        while (!peek().kind().equals("}")) { statement(); }
        expect("}");
        // falling off the end of main returns 0
        emit("movl $0, %eax"); emit("movq %rbp, %rsp"); emit("popq %rbp"); emit("ret");
        asm.append("\t.section .note.GNU-stack,\"\",@progbits\n");
        return asm.toString();
    }

    void statement() {
        if (accept("return")) {
            expr(); expect(";");
            emit("movq %rbp, %rsp"); emit("popq %rbp"); emit("ret");
        } else if (accept("int")) {
            String n = expect("id").text();
            if (vars.containsKey(n)) throw new RuntimeException("redeclared: " + n);
            if (accept("=")) expr(); else emit("movl $0, %eax");
            expect(";");
            stackOff -= 8; vars.put(n, stackOff);
            emit("pushq %rax");
        } else {
            expr(); expect(";");
        }
    }

    // assignment := id '=' assignment | logical-or
    void expr() {
        if (peek().kind().equals("id") && pos + 1 < toks.size() && toks.get(pos + 1).kind().equals("=")) {
            String n = next().text(); next();
            Integer off = vars.get(n);
            if (off == null) throw new RuntimeException("undeclared: " + n);
            expr();
            emit("movl %eax, " + off + "(%rbp)");
        } else orExpr();
    }

    void orExpr() {
        andExpr();
        while (accept("||")) {
            String t = label(), end = label();
            emit("cmpl $0, %eax"); emit("jne " + t);
            andExpr();
            emit("cmpl $0, %eax"); emit("movl $0, %eax"); emit("setne %al"); emit("jmp " + end);
            asm.append(t).append(":\n"); emit("movl $1, %eax");
            asm.append(end).append(":\n");
        }
    }

    void andExpr() {
        eqExpr();
        while (accept("&&")) {
            String f = label(), end = label();
            emit("cmpl $0, %eax"); emit("je " + f);
            eqExpr();
            emit("cmpl $0, %eax"); emit("movl $0, %eax"); emit("setne %al"); emit("jmp " + end);
            asm.append(f).append(":\n"); emit("movl $0, %eax");
            asm.append(end).append(":\n");
        }
    }

    interface Rule { void run(); }

    void binary(Rule sub, Map<String, String> ops) {
        sub.run();
        while (ops.containsKey(peek().kind())) {
            String op = next().kind();
            emit("pushq %rax"); sub.run(); emit("movl %eax, %ecx"); emit("popq %rax");
            // now left in %eax, right in %ecx
            switch (op) {
                case "+" -> emit("addl %ecx, %eax");
                case "-" -> emit("subl %ecx, %eax");
                case "*" -> emit("imull %ecx, %eax");
                case "/" -> { emit("cltd"); emit("idivl %ecx"); }
                case "%" -> { emit("cltd"); emit("idivl %ecx"); emit("movl %edx, %eax"); }
                default -> { emit("cmpl %ecx, %eax"); emit("movl $0, %eax"); emit(ops.get(op) + " %al"); }
            }
        }
    }

    static final Map<String, String> EQ = Map.of("==", "sete", "!=", "setne");
    static final Map<String, String> REL = Map.of("<", "setl", ">", "setg", "<=", "setle", ">=", "setge");
    static final Map<String, String> ADD = Map.of("+", "", "-", "");
    static final Map<String, String> MUL = Map.of("*", "", "/", "", "%", "");

    void eqExpr() { binary(this::relExpr, EQ); }
    void relExpr() { binary(this::addExpr, REL); }
    void addExpr() { binary(this::mulExpr, ADD); }
    void mulExpr() { binary(this::unary, MUL); }

    void unary() {
        if (accept("-")) { unary(); emit("negl %eax"); }
        else if (accept("~")) { unary(); emit("notl %eax"); }
        else if (accept("!")) { unary(); emit("cmpl $0, %eax"); emit("movl $0, %eax"); emit("sete %al"); }
        else primary();
    }

    void primary() {
        Tok t = next();
        switch (t.kind()) {
            case "num" -> emit("movl $" + t.text() + ", %eax");
            case "id" -> {
                Integer off = vars.get(t.text());
                if (off == null) throw new RuntimeException("undeclared: " + t.text());
                emit("movl " + off + "(%rbp), %eax");
            }
            case "(" -> { expr(); expect(")"); }
            default -> throw new RuntimeException("unexpected token: " + t.kind());
        }
    }

    public static String compile(String src) { return new Compiler(src).program(); }

    public static void main(String[] args) throws Exception {
        if (args.length != 1) { System.err.println("usage: java Compiler file.c  (writes asm to stdout)"); System.exit(2); }
        System.out.print(compile(java.nio.file.Files.readString(java.nio.file.Path.of(args[0]))));
    }
}
