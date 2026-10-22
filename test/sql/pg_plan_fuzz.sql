LOAD 'pg_plan_fuzz';

explain select * from t1 join t2 on t1.a = t2.c;
select * from t1 join t2 on t1.a = t2.c;

set pg_plan_fuzz.force_hashjoin = on;

explain select * from t1 join t2 on t1.a = t2.c;
select * from t1 join t2 on t1.a = t2.c;

set pg_plan_fuzz.force_hashjoin = off;
set pg_plan_fuzz.force_mergejoin = on;

explain select * from t1 join t2 on t1.a = t2.c;
select * from t1 join t2 on t1.a = t2.c;

set pg_plan_fuzz.force_mergejoin = off;
set pg_plan_fuzz.force_nestloop = on;

explain select * from t1 join t2 on t1.a = t2.c;
select * from t1 join t2 on t1.a = t2.c;