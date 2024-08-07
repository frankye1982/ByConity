set enable_optimizer = 0; -- max_block_size inside sub queries is not supported.
SELECT min(number) FROM system.numbers WHERE toUInt64(number % 1000) IN (SELECT DISTINCT blockSize() FROM system.numbers SETTINGS max_block_size = 123, max_rows_to_read = 1000, read_overflow_mode = 'break') SETTINGS max_rows_to_read = 1000000, read_overflow_mode = 'break';
SELECT * FROM (SELECT DISTINCT blockSize() AS x FROM system.numbers SETTINGS max_block_size = 123, max_rows_to_read = 1000, read_overflow_mode = 'break');
SELECT * FROM (SELECT DISTINCT blockSize() AS x FROM system.numbers SETTINGS max_block_size = 123, max_rows_to_read = 1000, read_overflow_mode = 'break', access_table_names='system.one'); -- {serverError 291}
SELECT * FROM (SELECT DISTINCT blockSize() AS x FROM system.numbers SETTINGS max_block_size = 123, max_rows_to_read = 1000, read_overflow_mode = 'break', access_table_names='system.numbers');
