PROGRAM Factorial;
VAR
   n, result, i : INTEGER;
BEGIN
   n := 10;
   result := 1;
   i := 1;
   WHILE i <= n DO
   BEGIN
      result := result * i;
      i := i + 1;
   END;
   PRINT(result);
END.
