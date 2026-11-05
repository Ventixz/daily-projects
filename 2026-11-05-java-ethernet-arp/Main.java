/** Demo: two hosts resolve each other over an in-memory "wire". */
public class Main {
    public static void main(String[] args) {
        ArpHost a = new ArpHost("02:00:00:00:00:0a", "10.0.0.1", 60_000);
        ArpHost b = new ArpHost("02:00:00:00:00:0b", "10.0.0.2", 60_000);
        byte[] req = a.request("10.0.0.2");
        System.out.println("A -> broadcast: who-has 10.0.0.2 (" + req.length + " bytes)");
        byte[] rep = b.receive(req, 0);
        System.out.println("B -> A: reply (" + rep.length + " bytes)");
        a.receive(rep, 0);
        System.out.println("A cache: 10.0.0.2 is at " + a.lookup("10.0.0.2", 1).orElse("?"));
        System.out.println("B cache: 10.0.0.1 is at " + b.lookup("10.0.0.1", 1).orElse("?"));
    }
}
