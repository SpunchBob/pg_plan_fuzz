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

set enable_hashjoin = on;
set enable_mergejoin = off;
set enable_nestloop = off;

explain  
select * from t1
join t2 on t1.a = t2.c;

select * from t1
join t2 on t1.a = t2.c;

set pg_plan_fuzz.force_mergejoin = on;

explain  
select * from t1
join t2 on t1.a = t2.c;

select * from t1
join t2 on t1.a = t2.c;

set pg_plan_fuzz.force_mergejoin = off;

explain  
select * from t1
join t2 on t1.a = t2.c
join t3 on t2.c = t3.e;

select * from t1
join t2 on t1.a = t2.c
join t3 on t2.c = t3.e;

set pg_plan_fuzz.force_mergejoin = on;
set pg_plan_fuzz.join_recourse_level = 0;

explain  
select * from t1
join t2 on t1.a = t2.c
join t3 on t2.c = t3.e;

select * from t1
join t2 on t1.a = t2.c
join t3 on t2.c = t3.e;

set pg_plan_fuzz.join_recourse_level = 5;

explain  
select * from t1
join t2 on t1.a = t2.c
join t3 on t2.c = t3.e;

select * from t1
join t2 on t1.a = t2.c
join t3 on t2.c = t3.e;

set pg_plan_fuzz.join_recourse_level = 10;

explain  
select * from t1
join t2 on t1.a = t2.c
join t3 on t2.c = t3.e;

select * from t1
join t2 on t1.a = t2.c
join t3 on t2.c = t3.e;

set pg_plan_fuzz.join_recourse_level = 15;

explain  
select * from t1
join t2 on t1.a = t2.c
join t3 on t2.c = t3.e;

select * from t1
join t2 on t1.a = t2.c
join t3 on t2.c = t3.e;

set pg_plan_fuzz.join_recourse_level = 20;

explain  
select * from t1
join t2 on t1.a = t2.c
join t3 on t2.c = t3.e;

select * from t1
join t2 on t1.a = t2.c
join t3 on t2.c = t3.e;

set pg_plan_fuzz.join_recourse_level = 30;

explain  
select * from t1
join t2 on t1.a = t2.c
join t3 on t2.c = t3.e;

select * from t1
join t2 on t1.a = t2.c
join t3 on t2.c = t3.e;

set pg_plan_fuzz.join_recourse_level = 40;

explain  
select * from t1
join t2 on t1.a = t2.c
join t3 on t2.c = t3.e;

select * from t1
join t2 on t1.a = t2.c
join t3 on t2.c = t3.e;

set pg_plan_fuzz.force_mergejoin = off;

explain  
select * from t1
join t2 on t1.a = t2.c
join t3 on t2.c = t3.e
join t4 on t3.e = t4.g;

select * from t1
join t2 on t1.a = t2.c
join t3 on t2.c = t3.e
join t4 on t3.e = t4.g;

set pg_plan_fuzz.force_mergejoin = on;

explain  
select * from t1
join t2 on t1.a = t2.c
join t3 on t2.c = t3.e
join t4 on t3.e = t4.g;

select * from t1
join t2 on t1.a = t2.c
join t3 on t2.c = t3.e
join t4 on t3.e = t4.g;

set pg_plan_fuzz.join_recourse_level = 5;

explain  
select * from t1
join t2 on t1.a = t2.c
join t3 on t2.c = t3.e
join t4 on t3.e = t4.g;

select * from t1
join t2 on t1.a = t2.c
join t3 on t2.c = t3.e
join t4 on t3.e = t4.g;

set pg_plan_fuzz.join_recourse_level = 50;

explain  
select * from t1
join t2 on t1.a = t2.c
join t3 on t2.c = t3.e
join t4 on t3.e = t4.g;

select * from t1
join t2 on t1.a = t2.c
join t3 on t2.c = t3.e
join t4 on t3.e = t4.g;

set pg_plan_fuzz.join_recourse_level = 80;

explain  
select * from t1
join t2 on t1.a = t2.c
join t3 on t2.c = t3.e
join t4 on t3.e = t4.g;

select * from t1
join t2 on t1.a = t2.c
join t3 on t2.c = t3.e
join t4 on t3.e = t4.g;

set pg_plan_fuzz.join_recourse_level = 100;
explain  
select * from t1
join t2 on t1.a = t2.c
join t3 on t2.c = t3.e
join t4 on t3.e = t4.g;

select * from t1
join t2 on t1.a = t2.c
join t3 on t2.c = t3.e
join t4 on t3.e = t4.g;