SELECT n, source FROM (SELECT  toDateTime64(number * 1000, 3) AS n, 'original' AS source  FROM numbers(10)  WHERE (number % 3) = 1 ) ORDER BY n ASC WITH FILL STEP toDateTime64(1000, 3);
SELECT n, source FROM (SELECT  toDateTime64(number * 1000, 9) AS n, 'original' AS source  FROM numbers(10)  WHERE (number % 3) = 1 ) ORDER BY n ASC WITH FILL STEP toDateTime64(1000, 9);
