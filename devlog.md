## pg_plan_fuzz -- 22.10.2026
    - Current state: Prototype
    - Current functionality: Not fully controllable join and scan nodes mutations

### CURRENT TASKS
---
1. Create instrument for specific join node transformation

We need a procedure, that will change specific node in plan. For this we
can use GUC's or costing like in *pg_hint_plan*. Join node transformations
will be placed in *join_search_hook*. I check *pathlist* , that presented in
*join_serach_hook*, and PostgreSQL do not create any other paths for join nodes,
so i think that we cannot use costings. In this this case, we can modify 
*pg_plan_fuzz_join_transformation* procedure. We need to add recursion level.
In case of using *join_search_hook* we not need to use walker, like this was in
*pg_scan_plan*, because this hooks is called every time, when PostgreSQL meet
join-node, not like in *planner_hook*.

Let's implement *join_recourse_level* like a global variable and use it to specify
current trasnform join node.

2. Create instrument for specific scan node transformation
3. Create or integrate from `pg_scan_plan` tools for query analyzing 

## pg_plan_fuzz -- 27.10.2026
    - Current state: Prototype
    - Current functionality: Not fully controllable join and scan nodes mutations

### About task 1
---

I spent some time to implement procedures, that will create controlable join
transfoirmations, and i get some problems with this, because of *join_search* 
hook behaviour of work. The main problem is that this hook contain information 
about path, and it is called per one joining in query. It means, that if we want
to specify a node, that need to be changed, we can not use this hook - if we 
set up GUC for some join, it will change all path, and all join nodes in query 
will have same join type. 

In *pg_hint_plan* we can transform specific node by setting up instructions in comment. How 
i understand, *pg_hint_plan* using bms to change specific node in query plan. I
added *join_pathlist* hook, because this hook contains information about all join nodes
types by relation ( relations is specified by RelOptInfo ). We can run through
all relation, and it could be solve my problem with specific nodes transformations.

There is some result of specific node mutations:

### All Mergejoin
---



```
set pg_plan_fuzz.join_recourse_level = 0;
                                           QUERY PLAN                                            
-------------------------------------------------------------------------------------------------
 Merge Join  (cost=281.62..532.95 rows=4000 width=92)
   Merge Cond: (t1.a = t2.c)
   ->  Merge Join  (cost=140.96..327.29 rows=2000 width=82)
         Merge Cond: (test_a.id_a = t1.a)
         ->  Merge Join  (cost=0.30..153.45 rows=1270 width=72)
               Merge Cond: (test_a.id_a = test_b.id_b)
               ->  Index Scan using test_a_pkey on test_a  (cost=0.15..67.20 rows=1270 width=36)
               ->  Index Scan using test_b_pkey on test_b  (cost=0.15..67.20 rows=1270 width=36)
         ->  Sort  (cost=140.66..145.66 rows=2000 width=10)
               Sort Key: t1.a
               ->  Seq Scan on t1  (cost=0.00..31.00 rows=2000 width=10)
   ->  Sort  (cost=140.66..145.66 rows=2000 width=10)
         Sort Key: t2.c
         ->  Seq Scan on t2  (cost=0.00..31.00 rows=2000 width=10)
(14 rows)
```
### One hash join in the end
---



```
set pg_plan_fuzz.join_recourse_level = 5;

                                        QUERY PLAN                                         
-------------------------------------------------------------------------------------------
 Merge Join  (cost=325.32..490.54 rows=4000 width=92)
   Merge Cond: (t1.a = t2.c)
   ->  Merge Join  (cost=184.66..284.88 rows=2000 width=82)
         Merge Cond: (test_b.id_b = t1.a)
         ->  Index Scan using test_b_pkey on test_b  (cost=0.15..67.20 rows=1270 width=36)
         ->  Sort  (cost=184.51..189.51 rows=2000 width=46)
               Sort Key: t1.a
               ->  Hash Join  (cost=38.58..74.85 rows=2000 width=46)
                     Hash Cond: (t1.a = test_a.id_a)
                     ->  Seq Scan on t1  (cost=0.00..31.00 rows=2000 width=10)
                     ->  Hash  (cost=22.70..22.70 rows=1270 width=36)
                           ->  Seq Scan on test_a  (cost=0.00..22.70 rows=1270 width=36)
   ->  Sort  (cost=140.66..145.66 rows=2000 width=10)
         Sort Key: t2.c
         ->  Seq Scan on t2  (cost=0.00..31.00 rows=2000 width=10)
(15 rows)

postgres=# 
```

### Two hash joins in the end
---



```
postgres=# set pg_plan_fuzz.join_recourse_level = 10;

                                    QUERY PLAN                                     
-----------------------------------------------------------------------------------
 Merge Join  (cost=369.01..439.01 rows=4000 width=92)
   Merge Cond: (t1.a = t2.c)
   ->  Sort  (cost=184.51..189.51 rows=2000 width=46)
         Sort Key: t1.a
         ->  Hash Join  (cost=38.58..74.85 rows=2000 width=46)
               Hash Cond: (t1.a = test_a.id_a)
               ->  Seq Scan on t1  (cost=0.00..31.00 rows=2000 width=10)
               ->  Hash  (cost=22.70..22.70 rows=1270 width=36)
                     ->  Seq Scan on test_a  (cost=0.00..22.70 rows=1270 width=36)
   ->  Sort  (cost=184.51..189.51 rows=2000 width=46)
         Sort Key: t2.c
         ->  Hash Join  (cost=38.58..74.85 rows=2000 width=46)
               Hash Cond: (t2.c = test_b.id_b)
               ->  Seq Scan on t2  (cost=0.00..31.00 rows=2000 width=10)
               ->  Hash  (cost=22.70..22.70 rows=1270 width=36)
                     ->  Seq Scan on test_b  (cost=0.00..22.70 rows=1270 width=36)
(16 rows)
```

### Still two hash joins, but different plan
---


```
postgres=# set pg_plan_fuzz.join_recourse_level = 20;

                                       QUERY PLAN                                        
-----------------------------------------------------------------------------------------
 Merge Join  (cost=369.01..439.01 rows=4000 width=92)
   Merge Cond: (t1.a = t2.c)
   ->  Sort  (cost=228.35..233.35 rows=2000 width=82)
         Sort Key: t1.a
         ->  Hash Join  (cost=77.15..118.69 rows=2000 width=82)
               Hash Cond: (t1.a = test_b.id_b)
               ->  Hash Join  (cost=38.58..74.85 rows=2000 width=46)
                     Hash Cond: (t1.a = test_a.id_a)
                     ->  Seq Scan on t1  (cost=0.00..31.00 rows=2000 width=10)
                     ->  Hash  (cost=22.70..22.70 rows=1270 width=36)
                           ->  Seq Scan on test_a  (cost=0.00..22.70 rows=1270 width=36)
               ->  Hash  (cost=22.70..22.70 rows=1270 width=36)
                     ->  Seq Scan on test_b  (cost=0.00..22.70 rows=1270 width=36)
   ->  Sort  (cost=140.66..145.66 rows=2000 width=10)
         Sort Key: t2.c
         ->  Seq Scan on t2  (cost=0.00..31.00 rows=2000 width=10)
(16 rows)
```

### Still two hash joins, and same plan
---


```
postgres=# set pg_plan_fuzz.join_recourse_level = 30;

                                       QUERY PLAN                                        
-----------------------------------------------------------------------------------------
 Merge Join  (cost=369.01..439.01 rows=4000 width=92)
   Merge Cond: (t1.a = t2.c)
   ->  Sort  (cost=228.35..233.35 rows=2000 width=82)
         Sort Key: t1.a
         ->  Hash Join  (cost=77.15..118.69 rows=2000 width=82)
               Hash Cond: (t1.a = test_b.id_b)
               ->  Hash Join  (cost=38.58..74.85 rows=2000 width=46)
                     Hash Cond: (t1.a = test_a.id_a)
                     ->  Seq Scan on t1  (cost=0.00..31.00 rows=2000 width=10)
                     ->  Hash  (cost=22.70..22.70 rows=1270 width=36)
                           ->  Seq Scan on test_a  (cost=0.00..22.70 rows=1270 width=36)
               ->  Hash  (cost=22.70..22.70 rows=1270 width=36)
                     ->  Seq Scan on test_b  (cost=0.00..22.70 rows=1270 width=36)
   ->  Sort  (cost=140.66..145.66 rows=2000 width=10)
         Sort Key: t2.c
         ->  Seq Scan on t2  (cost=0.00..31.00 rows=2000 width=10)
(16 rows)
```

### All hash joins
---


```
postgres=# set pg_plan_fuzz.join_recourse_level = 30;

                                    QUERY PLAN                                     
-----------------------------------------------------------------------------------
 Hash Join  (cost=133.15..224.69 rows=4000 width=92)
   Hash Cond: (t1.a = t2.c)
   ->  Hash Join  (cost=77.15..118.69 rows=2000 width=82)
         Hash Cond: (t1.a = test_b.id_b)
         ->  Hash Join  (cost=38.58..74.85 rows=2000 width=46)
               Hash Cond: (t1.a = test_a.id_a)
               ->  Seq Scan on t1  (cost=0.00..31.00 rows=2000 width=10)
               ->  Hash  (cost=22.70..22.70 rows=1270 width=36)
                     ->  Seq Scan on test_a  (cost=0.00..22.70 rows=1270 width=36)
         ->  Hash  (cost=22.70..22.70 rows=1270 width=36)
               ->  Seq Scan on test_b  (cost=0.00..22.70 rows=1270 width=36)
   ->  Hash  (cost=31.00..31.00 rows=2000 width=10)
         ->  Seq Scan on t2  (cost=0.00..31.00 rows=2000 width=10)
(13 rows)
```