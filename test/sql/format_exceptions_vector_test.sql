drop table if exists t1;
drop table if exists t2;
drop table if exists t3;

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

create index t1_idx_a on t1 ( a ) ;
create index t1_idx_b on t1 ( b ) ;
create index t2_idx_c on t2 ( c ) ;
create index t2_idx_d on t2 ( d ) ;

LOAD 'pg_plan_fuzz';

set pg_plan_fuzz.vector = "000";

explain select * from t1 join t2 on t1.a = t2.c join t3 on t2.c = t3.e;

set pg_plan_fuzz.vector = "g000";

explain select * from t1 join t2 on t1.a = t2.c join t3 on t2.c = t3.e;

set pg_plan_fuzz.vector = "000000000000";

set pg_plan_fuzz.force_mergejoin = on;
set pg_plan_fuzz.join_recourse_level = 1;

explain select * from t1 join t2 on t1.a = t2.c join t3 on t2.c = t3.e;

set pg_plan_fuzz.vector = "0000ffff0000";

explain select * from t1 join t2 on t1.a = t2.c join t3 on t2.c = t3.e;
