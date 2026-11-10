drop table if exists t1;
drop table if exists t2;
drop table if exists t3;
drop table if exists t4;

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

create table t3 
(
    e INT,
    f TEXT
);

create table t4 
(
    g INT,
    h TEXT
);

create index t1_a on t1 ( a );
create index t2_c on t2 ( c );
create index t3_e on t3 ( e );
create index t4_g on t4 ( g );

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

insert into t3
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

insert into t4
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

LOAD 'pg_plan_fuzz';

set pg_plan_fuzz.logging_mode = off;

set enable_hashjoin = on;
set enable_mergejoin = off;
set enable_nestloop = off;

set enable_indescan = off;
set enable_indexonlyscan = off;

explain select * from t1 join t2 on t1.a = t2.c join t3 on t2.c = t3.e join t4 on t3.e = t4.g;
select * from t1 join t2 on t1.a = t2.c join t3 on t2.c = t3.e join t4 on t3.e = t4.g;

set pg_plan_fuzz.force_mergejoin = on;
set pg_plan_fuzz.force_indexscan = on;

set pg_plan_fuzz.join_recourse_level = 1;
set pg_plan_fuzz.scan_recourse_level = 1;

explain select * from t1 join t2 on t1.a = t2.c join t3 on t2.c = t3.e join t4 on t3.e = t4.g;
select * from t1 join t2 on t1.a = t2.c join t3 on t2.c = t3.e join t4 on t3.e = t4.g;

set pg_plan_fuzz.scan_recourse_level = 2;

explain select * from t1 join t2 on t1.a = t2.c join t3 on t2.c = t3.e join t4 on t3.e = t4.g;
select * from t1 join t2 on t1.a = t2.c join t3 on t2.c = t3.e join t4 on t3.e = t4.g;

set pg_plan_fuzz.scan_recourse_level = 3;

explain select * from t1 join t2 on t1.a = t2.c join t3 on t2.c = t3.e join t4 on t3.e = t4.g;
select * from t1 join t2 on t1.a = t2.c join t3 on t2.c = t3.e join t4 on t3.e = t4.g;

set pg_plan_fuzz.scan_recourse_level = 4;

explain select * from t1 join t2 on t1.a = t2.c join t3 on t2.c = t3.e join t4 on t3.e = t4.g;
select * from t1 join t2 on t1.a = t2.c join t3 on t2.c = t3.e join t4 on t3.e = t4.g;
