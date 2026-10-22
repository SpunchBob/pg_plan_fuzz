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