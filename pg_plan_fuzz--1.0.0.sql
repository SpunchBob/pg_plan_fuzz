CREATE SCHEMA IF NOT EXISTS plan_fuzz;

CREATE FUNCTION set_bitvector_value(integer, integer)
RETURNS void
AS 'MODULE_PATHNAME', 'pg_plan_fuzz_set_bitvector_value'
LANGUAGE C STRICT;
