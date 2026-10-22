LOAD 'pg_plan_fuzz';

drop table if exists t1;
drop table if exists t2;

create table t1 
(
    a INT,
    b TEXT
);

create table t2 
(
    c INT,
    d TEXT
);

insert into t1
values 
( 1, '1' ),
( 2, '2' ),
( 3, '3' ),
( 4, '4' ),
( 5, '5' ),
( 6, '6' ),
( 7, '7' ),
( 8, '8' ),
( 9, '9' ),
( 10, '10' ),
( 11, '11' ),
( 12, '12' ),
( 13, '13' ),
( 14, '14' ),
( 15, '15' );

insert into t2
values
( 1, '1' ),
( 2, '2' ),
( 3, '3' ),
( 4, '4' ),
( 5, '5' ),
( 6, '6' ),
( 7, '7' ),
( 8, '8' ),
( 9, '9' ),
( 10, '10' ),
( 11, '11' ),
( 12, '12' ),
( 13, '13' ),
( 14, '14' ),
( 15, '15' );

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