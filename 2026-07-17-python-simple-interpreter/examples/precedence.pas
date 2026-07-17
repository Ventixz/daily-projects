PROGRAM Precedence;
VAR
   result, nested : INTEGER;

BEGIN
   result := 2 + 3 * 4 - 6 DIV 2;   { 2 + 12 - 3 = 11 }
   nested := (2 + 3) * (4 - -2);    { 5 * 6 = 30 }
END.
