{ Deliberately broken program: `y` is never declared with VAR.
  Running this should fail semantic analysis before a single statement executes. }
PROGRAM Broken;
VAR
   x : INTEGER;

BEGIN
   x := 1;
   y := x + 1;
END.
