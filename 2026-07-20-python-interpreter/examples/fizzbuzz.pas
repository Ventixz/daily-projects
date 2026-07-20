PROGRAM FizzBuzz;
VAR
   i : INTEGER;
BEGIN
   i := 1;
   WHILE i <= 20 DO
   BEGIN
      IF i MOD 15 = 0 THEN
         PRINT('FizzBuzz')
      ELSE IF i MOD 3 = 0 THEN
         PRINT('Fizz')
      ELSE IF i MOD 5 = 0 THEN
         PRINT('Buzz')
      ELSE
         PRINT(i);
      i := i + 1;
   END;
END.
