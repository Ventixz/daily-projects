import java.io.*;
import java.nio.file.*;
import java.util.*;

/** Compiles each case with Compiler, assembles with gcc, runs, checks the exit code. */
public class TestRunner {
    record Case(String name, String src, int exit) {}
    static boolean fails(String src) {
        try { Compiler.compile(src); return false; } catch (RuntimeException e) { return true; }
    }
    public static void main(String[] a) throws Exception {
        List<Case> cases = List.of(
            new Case("return const", "int main(){return 42;}", 42),
            new Case("negate", "int main(){return -(-7);}", 7),
            new Case("bitwise not", "int main(){return ~~5;}", 5),
            new Case("logical not", "int main(){return !0 + !5;}", 1),
            new Case("precedence", "int main(){return 2+3*4;}", 14),
            new Case("parens", "int main(){return (2+3)*4;}", 20),
            new Case("left assoc sub", "int main(){return 10-3-2;}", 5),
            new Case("division", "int main(){return 17/5;}", 3),
            new Case("modulo", "int main(){return 17%5;}", 2),
            new Case("negative div", "int main(){return -7/2 + 10;}", 7),
            new Case("compare", "int main(){return (3<4)+(4<=4)+(5>6)+(2>=2);}", 3),
            new Case("equality", "int main(){return (3==3)*10 + (3!=3);}", 10),
            new Case("and", "int main(){return (1&&2)+(1&&0)+(0&&1);}", 1),
            new Case("or", "int main(){return (0||0)+(0||3)+(2||0);}", 2),
            new Case("short circuit and", "int main(){int a=0; 0 && (a=5); return a;}", 0),
            new Case("short circuit or", "int main(){int a=0; 1 || (a=5); return a;}", 0),
            new Case("variables", "int main(){int a=3; int b=4; return a*a+b*b;}", 25),
            new Case("assign", "int main(){int a; a=9; int b=a=4; return a+b;}", 8),
            new Case("uninit zero", "int main(){int a; return a;}", 0),
            new Case("no return", "int main(){int a=5;}", 0),
            new Case("expr statement", "int main(){int a=1; a=a+1; a=a*10; return a;}", 20)
        );
        Path dir = Files.createTempDirectory("ccomp");
        int failed = 0;
        for (Case c : cases) {
            Path s = dir.resolve("t.s"), exe = dir.resolve("t");
            Files.writeString(s, Compiler.compile(c.src()));
            Process g = new ProcessBuilder("gcc", "-no-pie", s.toString(), "-o", exe.toString()).inheritIO().start();
            if (g.waitFor() != 0) { System.out.println("FAIL (gcc) " + c.name()); failed++; continue; }
            int code = new ProcessBuilder(exe.toString()).start().waitFor();
            if (code == c.exit()) System.out.println("ok   " + c.name());
            else { System.out.println("FAIL " + c.name() + ": got " + code + ", want " + c.exit()); failed++; }
        }
        String[] bad = {"int main(){return x;}", "int main(){int a; int a; return 0;}", "int main(){return 1}", "int main(){return @;}"};
        for (String b : bad) {
            if (fails(b)) System.out.println("ok   rejects: " + b);
            else { System.out.println("FAIL should reject: " + b); failed++; }
        }
        System.out.println(failed == 0 ? "all passed" : failed + " failed");
        System.exit(failed == 0 ? 0 : 1);
    }
}
