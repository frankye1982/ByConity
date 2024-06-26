SET optimize_move_functions_out_of_any = 1;
SET enable_optimizer = 0;

SELECT any(number * number) AS n FROM numbers(100) FORMAT CSVWithNames;
EXPLAIN SYNTAX SELECT any(number * number) AS n FROM numbers(100);

SELECT any((number, number * 2)) as n FROM numbers(100) FORMAT CSVWithNames;
EXPLAIN SYNTAX SELECT any((number, number * 2)) as n FROM numbers(100);
